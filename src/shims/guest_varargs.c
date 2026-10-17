// See guest_varargs.h for the calling-convention rules this implements.

#include "guest_varargs.h"

#include <ctype.h>
#include <string.h>

#include "cpu.h"

#define VA_FIRST_REG 10  // cpu->x index of a0
#define VA_NREG      8   // a0-a7

// Guest memory is identity-mapped (see guest_mem.h), so a guest address can
// be dereferenced directly. memcpy() rather than a cast-and-dereference so a
// 4-byte read of a slot that happens to be unaligned cannot fault.
static uint32_t load_u32(uint32_t addr) {
    uint32_t v;
    memcpy(&v, (void const *)(uintptr_t)addr, sizeof(v));
    return v;
}

void guest_va_init_call(guest_va_t *va, cpu_t *cpu, int first_reg) {
    va->cpu       = cpu;
    va->by_addr   = false;
    va->reg       = first_reg < 0 ? 0 : (first_reg > VA_NREG ? VA_NREG : first_reg);
    va->stack_off = 0;
    va->addr      = 0;
}

void guest_va_init_va_list(guest_va_t *va, cpu_t *cpu, uint32_t va_list_addr) {
    va->cpu       = cpu;
    va->by_addr   = true;
    va->addr      = va_list_addr;
    va->reg       = VA_NREG;
    va->stack_off = 0;
}

// Consumes and returns the next 4-byte argument slot.
static uint32_t va_u32(guest_va_t *va) {
    if (va->by_addr) {
        uint32_t v = load_u32(va->addr);
        va->addr += 4;
        return v;
    }
    if (va->reg < VA_NREG) {
        return va->cpu->x[VA_FIRST_REG + va->reg++];
    }
    uint32_t sp = va->cpu->x[REG_SP];
    uint32_t v  = load_u32(sp + va->stack_off);
    va->stack_off += 4;
    return v;
}

// Positions the cursor at the next 8-byte argument (rules 2, 3 and 5).
static void va_align64(guest_va_t *va) {
    if (va->by_addr) {
        va->addr = (va->addr + 7u) & ~7u;
        return;
    }
    if (va->reg < VA_NREG) {
        if (va->reg & 1) {
            va->reg++;
        }
        if (va->reg + 2 <= VA_NREG) {
            return;
        }
        va->reg = VA_NREG;  // too big for the registers left: whole value on the stack
    }
    va->stack_off = (va->stack_off + 7u) & ~7u;
}

// Consumes and returns the next 8-byte argument slot pair.
static uint64_t va_u64(guest_va_t *va) {
    va_align64(va);

    uint32_t lo, hi;
    if (va->by_addr) {
        lo = load_u32(va->addr);
        hi = load_u32(va->addr + 4);
        va->addr += 8;
    } else if (va->reg < VA_NREG) {
        lo = va->cpu->x[VA_FIRST_REG + va->reg++];
        hi = va->cpu->x[VA_FIRST_REG + va->reg++];
    } else {
        uint32_t sp = va->cpu->x[REG_SP];
        lo          = load_u32(sp + va->stack_off);
        hi          = load_u32(sp + va->stack_off + 4);
        va->stack_off += 8;
    }
    return (uint64_t)lo | ((uint64_t)hi << 32);
}

// ---------------------------------------------------------------------------
// Output side
// ---------------------------------------------------------------------------

// Where formatted text goes: either a guest buffer (snprintf semantics,
// truncating but always NUL-terminated) or a host stream.
typedef struct {
    char  *buf;
    size_t size;
    size_t used;
    FILE  *stream;
} va_out_t;

static void out_write(va_out_t *o, char const *s, size_t n) {
    if (o->stream != NULL) {
        fwrite(s, 1, n, o->stream);
        return;
    }
    if (o->size == 0) {
        return;  // snprintf(buf, 0, ...): count only, never touch buf
    }
    size_t avail = o->size > o->used + 1 ? o->size - o->used - 1 : 0;
    size_t take  = n < avail ? n : avail;
    if (take > 0) {
        memcpy(o->buf + o->used, s, take);
        o->used += take;
    }
    o->buf[o->used] = '\0';
}

// Room left for one conversion's output, as the size argument to snprintf().
static size_t out_room(va_out_t *o) {
    return o->size > o->used ? o->size - o->used : 0;
}

static void out_advance(va_out_t *o, int n) {
    if (n <= 0 || o->size == 0) {
        return;
    }
    // The snprintf() that produced `n` could itself only write size-1-used
    // characters before its own terminator, so that is how far `used` moves.
    size_t avail = o->size - 1 - o->used;
    size_t take  = (size_t)n < avail ? (size_t)n : avail;
    o->used += take;
    o->buf[o->used] = '\0';
}

// ---------------------------------------------------------------------------
// Format-string parsing
// ---------------------------------------------------------------------------

// Which C type the *host* snprintf() must be handed for one conversion, and
// how many guest argument slots it consumes. The guest's own length modifier
// is rewritten to the host one that matches that type; since this emulator is
// built -m32, guest long/size_t/ptrdiff_t/pointer and host long/size_t/
// ptrdiff_t/pointer are the same width, and only ll/j (64-bit on both) need a
// two-slot guest argument.
typedef enum {
    K_RAW,    // unsupported conversion: emit it verbatim, consume nothing
    K_I32,    // int32_t, no modifier
    K_U32,    // uint32_t, no modifier
    K_I8,     // int8_t promoted to int (%hh)
    K_I16,    // int16_t promoted to int (%h)
    K_U8,
    K_U16,
    K_LONG,   // long
    K_ULONG,  // unsigned long
    K_I64,    // long long
    K_U64,    // unsigned long long
    K_IMAX,   // intmax_t
    K_UMAX,   // uintmax_t
    K_SIZE,   // size_t
    K_PTRD,   // ptrdiff_t
    K_DOUBLE, // double (rule 4: passed in an integer register pair)
    K_CHAR,   // int (a char promoted to int)
    K_STR,    // char const *
    K_PTR,    // void *
    K_N       // int *, writes the character count
} arg_kind_t;

typedef struct {
    char       spec[64];  // the conversion, rebuilt with host length modifiers
    arg_kind_t kind;
    char       raw[96];   // K_RAW: the verbatim text to emit
} conv_t;

// printf() supports positional arguments (%1$d); this walker only knows how to
// consume arguments in order, so those are passed through unformatted rather
// than misformatted. Scan for them before consuming anything.
static bool conv_is_positional(char const *p) {
    ++p;  // the '%'
    while (*p != '\0' && strchr("-+ #0'", *p) != NULL) {
        p++;
    }
    while (*p != '\0' && (*p == '*' || isdigit((unsigned char)*p))) {
        if (*p == '$') {
            return true;
        }
        p++;
    }
    if (*p == '$') {
        return true;
    }
    if (*p == '.') {
        p++;
        while (*p != '\0' && (*p == '*' || isdigit((unsigned char)*p))) {
            if (*p == '$') {
                return true;
            }
            p++;
        }
    }
    return false;
}

static void conv_set_raw(conv_t *c, char const *start, char const *end) {
    size_t n = (size_t)(end - start);
    if (n >= sizeof(c->raw)) {
        n = sizeof(c->raw) - 1;
    }
    memcpy(c->raw, start, n);
    c->raw[n] = '\0';
    c->kind   = K_RAW;
}

// Parses one conversion starting at `p` (which points at '%'), consuming any
// '*' width/precision arguments from `va`. Returns the pointer just past the
// conversion character.
static char const *conv_parse(char const *p, conv_t *c, guest_va_t *va) {
    char const *start = p;
    p++;

    if (conv_is_positional(start)) {
        char const *q = p;
        while (*q != '\0' && strchr("-+ #0'", *q) != NULL) {
            q++;
        }
        while (*q != '\0' && (*q == '*' || *q == '$' || isdigit((unsigned char)*q))) {
            q++;
        }
        if (*q == '.') {
            q++;
            while (*q != '\0' && (*q == '*' || *q == '$' || isdigit((unsigned char)*q))) {
                q++;
            }
        }
        q += strspn(q, "hljztL");
        if (*q != '\0') {
            q++;
        }
        conv_set_raw(c, start, q);
        return q;
    }

    char flags[16];
    size_t nflags = 0;
    while (*p != '\0' && strchr("-+ #0'", *p) != NULL && nflags + 1 < sizeof(flags)) {
        flags[nflags++] = *p++;
    }

    int width = -1;
    bool star_width = false;
    if (*p == '*') {
        p++;
        width = (int)va_u32(va);
        star_width = true;
    } else if (isdigit((unsigned char)*p)) {
        width = 0;
        while (isdigit((unsigned char)*p)) {
            width = width * 10 + (*p++ - '0');
        }
    }

    int prec = -1;
    if (*p == '.') {
        p++;
        if (*p == '*') {
            p++;
            prec = (int)va_u32(va);
            if (prec < 0) {
                prec = -1;  // a negative precision from '*' means "no precision"
            }
        } else if (isdigit((unsigned char)*p)) {
            prec = 0;
            while (isdigit((unsigned char)*p)) {
                prec = prec * 10 + (*p++ - '0');
            }
        } else {
            prec = 0;  // a bare '.' means precision 0
        }
    }

    // Length modifier, and the guest-side width/sign it implies.
    char const *lenmod = "";
    int         narrow = 0;  // 8/16 for %hh/%h, whose argument promotes to int
    if (strncmp(p, "ll", 2) == 0) {
        lenmod = "ll";
        p += 2;
    } else if (strncmp(p, "hh", 2) == 0) {
        narrow = 8;
        p += 2;
    } else if (*p == 'h') {
        narrow = 16;
        p++;
    } else if (*p == 'l') {
        lenmod = "l";
        p++;
    } else if (*p == 'j') {
        lenmod = "j";
        p++;
    } else if (*p == 'z') {
        lenmod = "z";
        p++;
    } else if (*p == 't') {
        lenmod = "t";
        p++;
    } else if (*p == 'L') {
        // riscv32 long double is 128-bit and passed in memory, which is a
        // format no app in this emulator's app set uses; read the 8 bytes
        // this file can actually represent and treat it as a double.
        p++;
    }

    char conv = *p;
    if (conv != '\0') {
        p++;
    }

    // Map (conversion, modifier) to a host type + the host length modifier
    // that prints it. %z/%t are 32 bits on both sides, so they only differ in
    // signedness: signed for d/i, unsigned for u/o/x/X.
    arg_kind_t kind = K_RAW;
    bool       is64 = strcmp(lenmod, "ll") == 0 || strcmp(lenmod, "j") == 0;
    bool       isz  = strcmp(lenmod, "z") == 0 || strcmp(lenmod, "t") == 0;
    if (conv == 'd' || conv == 'i') {
        kind = is64 ? (strcmp(lenmod, "j") == 0 ? K_IMAX : K_I64)
             : strcmp(lenmod, "l") == 0 ? K_LONG
             : isz                       ? K_PTRD
             : narrow == 8             ? K_I8 : narrow == 16 ? K_I16 : K_I32;
    } else if (conv == 'u' || conv == 'o' || conv == 'x' || conv == 'X') {
        kind = is64 ? (strcmp(lenmod, "j") == 0 ? K_UMAX : K_U64)
             : strcmp(lenmod, "l") == 0 ? K_ULONG
             : isz                       ? K_SIZE
             : narrow == 8             ? K_U8 : narrow == 16 ? K_U16 : K_U32;
    } else if (conv == 'c') {
        kind = K_CHAR;
    } else if (conv == 's') {
        kind = K_STR;
    } else if (conv == 'p') {
        kind = K_PTR;
    } else if (conv == 'f' || conv == 'F' || conv == 'e' || conv == 'E' || conv == 'g' || conv == 'G'
               || conv == 'a' || conv == 'A') {
        kind = K_DOUBLE;
    } else if (conv == 'n') {
        kind = K_N;
    }

    if (kind == K_RAW) {
        conv_set_raw(c, start, p);
        return p;
    }

    // A negative width from '*' means the '-' flag with a positive width. A
    // width that is simply absent must stay absent: appending one would turn
    // "%lld" into "%-1lld".
    if (star_width && width < 0) {
        if (nflags + 1 < sizeof(flags)) {
            flags[nflags++] = '-';
        }
        width = -width;
    }

    size_t k = 0;
    c->spec[k++] = '%';
    for (size_t i = 0; i < nflags && k + 1 < sizeof(c->spec); i++) {
        c->spec[k++] = flags[i];
    }
    if (width >= 0 && k + 1 < sizeof(c->spec)) {
        k += (size_t)snprintf(c->spec + k, sizeof(c->spec) - k, "%d", width);
        if (k >= sizeof(c->spec)) {
            k = sizeof(c->spec) - 1;
        }
    }
    if (prec >= 0 && k + 1 < sizeof(c->spec)) {
        k += (size_t)snprintf(c->spec + k, sizeof(c->spec) - k, ".%d", prec);
        if (k >= sizeof(c->spec)) {
            k = sizeof(c->spec) - 1;
        }
    }
    for (char const *m = lenmod; *m != '\0' && k + 1 < sizeof(c->spec); m++) {
        c->spec[k++] = *m;
    }
    c->spec[k++] = conv;
    c->spec[k]   = '\0';
    c->kind      = kind;

    return p;
}

// ---------------------------------------------------------------------------
// Engine
// ---------------------------------------------------------------------------

static int guest_va_format(char const *fmt, guest_va_t *va, va_out_t *out) {
    int total = 0;

    for (char const *p = fmt; *p != '\0';) {
        if (*p != '%') {
            char const *run = p;
            while (*p != '\0' && *p != '%') {
                p++;
            }
            size_t n = (size_t)(p - run);
            out_write(out, run, n);
            total += (int)n;
            continue;
        }
        if (p[1] == '%') {
            out_write(out, "%", 1);
            total++;
            p += 2;
            continue;
        }

        conv_t c;
        p = conv_parse(p, &c, va);

        if (c.kind == K_RAW) {
            size_t n = strlen(c.raw);
            out_write(out, c.raw, n);
            total += (int)n;
            continue;
        }

        // Each conversion is emitted with its own host call, with exactly one
        // argument of exactly the type the rebuilt spec expects. That is what
        // makes this ABI-proof: no guest argument is ever handed to a host
        // vararg list, so no host calling convention is involved at all.
        int n = 0;
        switch (c.kind) {
            case K_I32: {
                int32_t v = (int32_t)va_u32(va);
                n = out->stream ? fprintf(out->stream, c.spec, v) : snprintf(out->buf + out->used, out_room(out), c.spec, v);
                break;
            }
            case K_U32: {
                uint32_t v = va_u32(va);
                n = out->stream ? fprintf(out->stream, c.spec, v) : snprintf(out->buf + out->used, out_room(out), c.spec, v);
                break;
            }
            case K_I8: {
                int v = (int)(int8_t)va_u32(va);
                n = out->stream ? fprintf(out->stream, c.spec, v) : snprintf(out->buf + out->used, out_room(out), c.spec, v);
                break;
            }
            case K_I16: {
                int v = (int)(int16_t)va_u32(va);
                n = out->stream ? fprintf(out->stream, c.spec, v) : snprintf(out->buf + out->used, out_room(out), c.spec, v);
                break;
            }
            case K_U8: {
                unsigned v = (unsigned)(uint8_t)va_u32(va);
                n = out->stream ? fprintf(out->stream, c.spec, v) : snprintf(out->buf + out->used, out_room(out), c.spec, v);
                break;
            }
            case K_U16: {
                unsigned v = (unsigned)(uint16_t)va_u32(va);
                n = out->stream ? fprintf(out->stream, c.spec, v) : snprintf(out->buf + out->used, out_room(out), c.spec, v);
                break;
            }
            case K_LONG: {
                long v = (long)(int32_t)va_u32(va);
                n = out->stream ? fprintf(out->stream, c.spec, v) : snprintf(out->buf + out->used, out_room(out), c.spec, v);
                break;
            }
            case K_ULONG: {
                unsigned long v = (unsigned long)va_u32(va);
                n = out->stream ? fprintf(out->stream, c.spec, v) : snprintf(out->buf + out->used, out_room(out), c.spec, v);
                break;
            }
            case K_I64: {
                long long v = (long long)va_u64(va);
                n = out->stream ? fprintf(out->stream, c.spec, v) : snprintf(out->buf + out->used, out_room(out), c.spec, v);
                break;
            }
            case K_U64: {
                unsigned long long v = (unsigned long long)va_u64(va);
                n = out->stream ? fprintf(out->stream, c.spec, v) : snprintf(out->buf + out->used, out_room(out), c.spec, v);
                break;
            }
            case K_IMAX: {
                intmax_t v = (intmax_t)va_u64(va);
                n = out->stream ? fprintf(out->stream, c.spec, v) : snprintf(out->buf + out->used, out_room(out), c.spec, v);
                break;
            }
            case K_UMAX: {
                uintmax_t v = (uintmax_t)va_u64(va);
                n = out->stream ? fprintf(out->stream, c.spec, v) : snprintf(out->buf + out->used, out_room(out), c.spec, v);
                break;
            }
            case K_SIZE: {
                size_t v = (size_t)va_u32(va);
                n = out->stream ? fprintf(out->stream, c.spec, v) : snprintf(out->buf + out->used, out_room(out), c.spec, v);
                break;
            }
            case K_PTRD: {
                ptrdiff_t v = (ptrdiff_t)va_u32(va);
                n = out->stream ? fprintf(out->stream, c.spec, v) : snprintf(out->buf + out->used, out_room(out), c.spec, v);
                break;
            }
            case K_DOUBLE: {
                // Rule 4: the double arrives in an integer register pair.
                uint64_t bits = va_u64(va);
                double   v;
                memcpy(&v, &bits, sizeof(v));
                n = out->stream ? fprintf(out->stream, c.spec, v) : snprintf(out->buf + out->used, out_room(out), c.spec, v);
                break;
            }
            case K_CHAR: {
                int v = (int)va_u32(va);
                n = out->stream ? fprintf(out->stream, c.spec, v) : snprintf(out->buf + out->used, out_room(out), c.spec, v);
                break;
            }
            case K_STR: {
                char const *v = (char const *)(uintptr_t)va_u32(va);
                // picolibc crashes on a NULL %s; glibc prints "(null)". Printing
                // it keeps one bad app from taking the whole emulator down.
                if (v == NULL) {
                    v = "(null)";
                }
                n = out->stream ? fprintf(out->stream, c.spec, v) : snprintf(out->buf + out->used, out_room(out), c.spec, v);
                break;
            }
            case K_PTR: {
                void *v = (void *)(uintptr_t)va_u32(va);
                n = out->stream ? fprintf(out->stream, c.spec, v) : snprintf(out->buf + out->used, out_room(out), c.spec, v);
                break;
            }
            case K_N: {
                int *where = (int *)(uintptr_t)va_u32(va);
                if (where != NULL) {
                    *where = out->stream ? total : (int)out->used;
                }
                break;
            }
            case K_RAW:
                break;
        }

        if (n > 0) {
            total += n;
            if (out->stream == NULL) {
                out_advance(out, n);
            }
        }
    }

    if (out->stream == NULL && out->size > 0) {
        out->buf[out->used] = '\0';
    }
    return total;
}

int guest_vsnprintf(guest_va_t *va, char *buf, size_t size, char const *fmt) {
    if (fmt == NULL) {
        if (size > 0) {
            buf[0] = '\0';
        }
        return 0;
    }
    va_out_t out = {buf, size, 0, NULL};
    if (size > 0) {
        buf[0] = '\0';
    }
    return guest_va_format(fmt, va, &out);
}

int guest_vfprintf(guest_va_t *va, FILE *stream, char const *fmt) {
    if (fmt == NULL || stream == NULL) {
        return 0;
    }
    va_out_t out = {NULL, 0, 0, stream};
    return guest_va_format(fmt, va, &out);
}
