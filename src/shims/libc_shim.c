// libc/newlib passthroughs. Guest pointers are real host pointers (see
// guest_mem.h), so buffers can be used directly; only handles that come from
// the host heap (FILE*) need the handle table (see handle_table.h) since
// they aren't guaranteed to fit in a 32-bit guest register.

#include <errno.h>
// Host <dirent.h> is glibc's; the guest's is picolibc's and laid out
// differently -- see guest_dirent_t below.
#include <dirent.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "cpu.h"
#include "fsroot.h"
#include "freertos/FreeRTOS.h"
#include "guest_mem.h"
#include "guest_varargs.h"
#include "handle_table.h"
#include "trap.h"

// picolibc's errno accessor: returns the address of this (host pthread's
// own, thread-local) errno -- which is exactly the right semantics, since
// each FreeRTOS "task" is itself a real pthread (see freertos_shim.c), so
// each guest task naturally gets its own independent errno storage too.
void trap___errno(cpu_t *cpu) {
    ret_ptr(cpu, &errno);
}

void trap_memchr(cpu_t *cpu) {
    ret_ptr(cpu, (void *)memchr(arg_ptr(cpu, 0), arg_i32(cpu, 1), arg_u32(cpu, 2)));
}
void trap_memcmp(cpu_t *cpu) {
    ret_u32(cpu, (uint32_t)(int32_t)memcmp(arg_ptr(cpu, 0), arg_ptr(cpu, 1), arg_u32(cpu, 2)));
}
void trap_memcpy(cpu_t *cpu) {
    ret_ptr(cpu, memcpy(arg_ptr(cpu, 0), arg_ptr(cpu, 1), arg_u32(cpu, 2)));
}
void trap_memmove(cpu_t *cpu) {
    ret_ptr(cpu, memmove(arg_ptr(cpu, 0), arg_ptr(cpu, 1), arg_u32(cpu, 2)));
}
void trap_memset(cpu_t *cpu) {
    ret_ptr(cpu, memset(arg_ptr(cpu, 0), arg_i32(cpu, 1), arg_u32(cpu, 2)));
}
void trap_strcmp(cpu_t *cpu) {
    ret_u32(cpu, (uint32_t)(int32_t)strcmp((char *)arg_ptr(cpu, 0), (char *)arg_ptr(cpu, 1)));
}
void trap_strncmp(cpu_t *cpu) {
    ret_u32(cpu, (uint32_t)(int32_t)strncmp((char *)arg_ptr(cpu, 0), (char *)arg_ptr(cpu, 1), arg_u32(cpu, 2)));
}
void trap_strncpy(cpu_t *cpu) {
    // strncpy()'s pad-with-NULs and no-guaranteed-terminator behaviour is
    // exactly the host's; apps use it to fill fixed-size record fields.
    ret_ptr(cpu, strncpy((char *)arg_ptr(cpu, 0), (char const *)arg_ptr(cpu, 1), arg_u32(cpu, 2)));
}
void trap_strcpy(cpu_t *cpu) {
    ret_ptr(cpu, strcpy((char *)arg_ptr(cpu, 0), (char *)arg_ptr(cpu, 1)));
}
void trap_strlen(cpu_t *cpu) {
    ret_u32(cpu, (uint32_t)strlen((char *)arg_ptr(cpu, 0)));
}
void trap_strnlen(cpu_t *cpu) {
    ret_u32(cpu, (uint32_t)strnlen((char *)arg_ptr(cpu, 0), arg_u32(cpu, 1)));
}
void trap_strtoull(cpu_t *cpu) {
    char  *endptr = NULL;
    char   *nptr  = (char *)arg_ptr(cpu, 0);
    char  **out_end = (char **)arg_ptr(cpu, 1);
    int     base   = arg_i32(cpu, 2);
    unsigned long long v = strtoull(nptr, &endptr, base);
    if (out_end != NULL) {
        *out_end = endptr;
    }
    ret_u64(cpu, v);
}

// qsort's comparator is itself guest code: re-enter the interpreter
// synchronously (see cpu_call_guest()) for every comparison. The callback
// has no way to receive extra context (its signature is fixed by qsort()),
// so which cpu_t/comparator a running qsort_thunk() means has to come from
// globals -- serialize concurrent guest qsort() calls (from different
// FreeRTOS task threads) with a mutex so two of them can never interleave
// through those globals.
static pthread_mutex_t g_qsort_mutex = PTHREAD_MUTEX_INITIALIZER;
static cpu_t           *g_qsort_cpu;
static uint32_t          g_qsort_cmp;
static int qsort_thunk(void const *a, void const *b) {
    uint32_t args[2] = {(uint32_t)(uintptr_t)a, (uint32_t)(uintptr_t)b};
    return (int32_t)cpu_call_guest(g_qsort_cpu, g_qsort_cmp, 2, args);
}
void trap_qsort(cpu_t *cpu) {
    void    *base  = arg_ptr(cpu, 0);
    size_t   nmemb = arg_u32(cpu, 1);
    size_t   size  = arg_u32(cpu, 2);
    uint32_t cmp   = arg_u32(cpu, 3);

    pthread_mutex_lock(&g_qsort_mutex);
    g_qsort_cpu = cpu;
    g_qsort_cmp = cmp;
    qsort(base, nmemb, size, qsort_thunk);
    pthread_mutex_unlock(&g_qsort_mutex);
}

void trap_snprintf(cpu_t *cpu) {
    char       *buf  = (char *)arg_ptr(cpu, 0);
    size_t      size = arg_u32(cpu, 1);
    char const *fmt  = (char const *)arg_ptr(cpu, 2);
    // The varargs start in a3 (buf/size/fmt are snprintf()'s three named
    // arguments) and follow the guest's own layout rules -- see
    // guest_varargs.h. Handing a0-a7 straight to the host snprintf() instead,
    // as this used to, shifts every argument after the first 64-bit one by a
    // register: snprintf(buf, 48, "score=%lld", (long long)20) read a3 (a
    // stale leftover of the caller's own argument list) as the low half and
    // a4 (the real value) as the high half, and printed 8589934612.
    guest_va_t va;
    guest_va_init_call(&va, cpu, 3);
    ret_u32(cpu, (uint32_t)guest_vsnprintf(&va, buf, size, fmt));
}

void trap_printf(cpu_t *cpu) {
    char const *fmt = (char const *)arg_ptr(cpu, 0);
    // See trap_snprintf()'s comment; printf()'s one named argument is the
    // format string, so its varargs start in a1.
    guest_va_t va;
    guest_va_init_call(&va, cpu, 1);
    ret_u32(cpu, (uint32_t)guest_vfprintf(&va, stdout, fmt));
}
void trap_putchar(cpu_t *cpu) {
    ret_u32(cpu, (uint32_t)putchar(arg_i32(cpu, 0)));
}
void trap_puts(cpu_t *cpu) {
    ret_u32(cpu, (uint32_t)puts((char const *)arg_ptr(cpu, 0)));
}
void trap_fputs(cpu_t *cpu) {
    char const *s = (char const *)arg_ptr(cpu, 0);
    FILE       *f = handle_get(arg_u32(cpu, 1));
    ret_u32(cpu, (uint32_t)(f ? fputs(s, f) : -1));
}

void trap_fopen(cpu_t *cpu) {
    char buf[1024];
    char const *path = fsroot_translate((char const *)arg_ptr(cpu, 0), buf, sizeof(buf));
    FILE       *f    = fopen(path, (char const *)arg_ptr(cpu, 1));
    ret_u32(cpu, f ? handle_alloc(f) : 0);
}
void trap_fastopen(cpu_t *cpu) {
    trap_fopen(cpu);
}
void trap_fclose(cpu_t *cpu) {
    uint32_t h = arg_u32(cpu, 0);
    FILE    *f = handle_get(h);
    ret_u32(cpu, f ? (uint32_t)fclose(f) : 0);
    handle_free(h);
}
void trap_fastclose(cpu_t *cpu) {
    trap_fclose(cpu);
}
void trap_fread(cpu_t *cpu) {
    void *ptr  = arg_ptr(cpu, 0);
    size_t sz   = arg_u32(cpu, 1);
    size_t n    = arg_u32(cpu, 2);
    FILE  *f    = handle_get(arg_u32(cpu, 3));
    ret_u32(cpu, (uint32_t)(f ? fread(ptr, sz, n, f) : 0));
}
void trap_fwrite(cpu_t *cpu) {
    void const *ptr = arg_ptr(cpu, 0);
    size_t      sz  = arg_u32(cpu, 1);
    size_t      n   = arg_u32(cpu, 2);
    FILE       *f   = handle_get(arg_u32(cpu, 3));
    ret_u32(cpu, (uint32_t)(f ? fwrite(ptr, sz, n, f) : 0));
}
void trap_fseek(cpu_t *cpu) {
    FILE *f = handle_get(arg_u32(cpu, 0));
    ret_u32(cpu, (uint32_t)(f ? fseek(f, arg_i32(cpu, 1), arg_i32(cpu, 2)) : -1));
}

// app.so's `struct stat` is picolibc's (ESP-IDF riscv32-esp-elf target), not
// glibc's -- sizes/offsets differ (notably time_t is 8 bytes there). This
// was reverse-engineered by compiling a tiny probe with the real
// riscv32-esp-elf-gcc and reading offsetof() values back out of the
// resulting object file's symbol table (see the struct layout below);
// copying the host `struct stat` directly, as an earlier version of this
// function did, overflowed the guest's (much smaller, 88-byte) buffer and
// corrupted its stack -- see the git history for the crash this caused.
typedef struct {
    int16_t  st_dev;
    int16_t  st_ino;
    uint32_t st_mode;
    int16_t  st_nlink;
    int16_t  st_uid;
    int16_t  st_gid;
    int16_t  st_rdev;
    int64_t  st_size;
    int64_t  st_atim_sec;
    int32_t  st_atim_nsec, _pad1;
    int64_t  st_mtim_sec;
    int32_t  st_mtim_nsec, _pad2;
    int64_t  st_ctim_sec;
    int32_t  st_ctim_nsec, _pad3;
    int32_t  st_blksize;
    int32_t  st_blocks;
    int32_t  st_spare4[2];
} guest_stat_t;
_Static_assert(sizeof(guest_stat_t) == 88, "guest_stat_t must match picolibc's struct stat exactly");

static void fill_guest_stat(guest_stat_t *gst, struct stat const *st) {
    memset(gst, 0, sizeof(*gst));
    gst->st_mode      = st->st_mode;
    gst->st_nlink     = (int16_t)st->st_nlink;
    gst->st_size      = st->st_size;
    // All three timestamps, not just mtime: st_ctim is as close to a
    // "creation time" as either side gets (Linux's struct stat has no birth
    // time, and the device's FatFs reports a file's creation date in its
    // ctime slot), and an app that lists a directory shows one of them.
    gst->st_atim_sec  = st->st_atime;
    gst->st_atim_nsec = (int32_t)st->st_atim.tv_nsec;
    gst->st_mtim_sec  = st->st_mtime;
    gst->st_mtim_nsec = (int32_t)st->st_mtim.tv_nsec;
    gst->st_ctim_sec  = st->st_ctime;
    gst->st_ctim_nsec = (int32_t)st->st_ctim.tv_nsec;
    gst->st_blksize   = (int32_t)st->st_blksize;
    gst->st_blocks    = (int32_t)st->st_blocks;
}

void trap_stat(cpu_t *cpu) {
    char        buf[1024];
    char const *path = fsroot_translate((char const *)arg_ptr(cpu, 0), buf, sizeof(buf));
    struct stat st;
    int         rc = stat(path, &st);
    if (rc == 0) {
        guest_stat_t gst;
        fill_guest_stat(&gst, &st);
        memcpy(arg_ptr(cpu, 1), &gst, sizeof(gst));
    }
    ret_u32(cpu, (uint32_t)rc);
}
// fstat() is the same struct, filled the same way, from an fd instead of a
// path. There is no fd indirection table here the way there is for FILE*
// (see handle_table.h): an fd is a small integer, so the number the guest
// holds is the host's own fd.
void trap_fstat(cpu_t *cpu) {
    struct stat st;
    int         rc = fstat(arg_i32(cpu, 0), &st);
    if (rc == 0) {
        guest_stat_t gst;
        fill_guest_stat(&gst, &st);
        memcpy(arg_ptr(cpu, 1), &gst, sizeof(gst));
    }
    ret_u32(cpu, (uint32_t)rc);
}
void trap_mkdir(cpu_t *cpu) {
    char        buf[1024];
    char const *path = fsroot_translate((char const *)arg_ptr(cpu, 0), buf, sizeof(buf));
    ret_u32(cpu, (uint32_t)mkdir(path, arg_u32(cpu, 1)));
}

void trap_time(cpu_t *cpu) {
    time_t *tloc = (time_t *)arg_ptr(cpu, 0);
    // time_t is 64-bit in the guest's picolibc (_TIME_T_ is __int_least64_t),
    // so time() returns it in the a0:a1 pair like any other 64-bit scalar.
    // Returning only the low half leaves the caller's high half holding
    // whatever stale value a1 already had.
    ret_u64(cpu, (uint64_t)(int64_t)time(tloc));
}
void trap_difftime(cpu_t *cpu) {
    // difftime(time_t end, time_t start): two 64-bit named arguments, hence
    // a0:a1 and a2:a3; the double return uses the soft-call pair.
    ret_f64(cpu, difftime((time_t)arg_i64(cpu, 0), (time_t)arg_i64(cpu, 2)));
}

// The guest's PRNG is the host's: nothing here needs cryptographic quality,
// but a game that drew the same "random" layout every run would look broken,
// so seed once from the clock rather than leaving rand() on its fixed default
// seed (which is what the guest's own libc would do, since no app imports
// srand()). Shared with trap_esp_random() (see esp_shim.c); declared in
// trap.h.
void seed_host_rand(void) {
    static bool seeded = false;
    if (!seeded) {
        seeded = true;
        srand((unsigned)time(NULL) ^ (unsigned)getpid());
    }
}
void trap_rand(cpu_t *cpu) {
    seed_host_rand();
    ret_u32(cpu, (uint32_t)rand());
}

void trap_usleep(cpu_t *cpu) {
    // Like trap_vTaskDelay() in freertos_shim.c: never really block the host
    // thread (that would stall this task's cpu_t and, if it's the main
    // thread, the SDL pump too) -- poll the idle hook once per ~1ms instead.
    uint32_t usec = arg_u32(cpu, 0);
    uint32_t ms   = usec / 1000;
    if (ms == 0) {
        ms = 1;
    }
    for (uint32_t i = 0; i < ms; i++) {
        freertos_compat_poll_idle();
    }
    ret_u32(cpu, 0);
}
// app.so's `struct tm` (picolibc, riscv32-esp-elf) is the plain 9-`int`
// POSIX layout with none of glibc's extension fields (tm_gmtoff/tm_zone) --
// 36 bytes against glibc's 56. Verified the same way as guest_stat_t in
// trap_stat(): compiled a probe with the real riscv32-esp-elf-gcc and read
// sizeof()/offsetof() back out of the object file. Writing a host
// `struct tm` directly into the guest's buffer, as an earlier version of
// this function did, overflowed by 20 bytes into whatever followed on the
// guest stack (typically the caller's saved ra/s0/s1) -- see the git
// history for the corruption this caused.
typedef struct {
    int32_t tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst;
} guest_tm_t;
_Static_assert(sizeof(guest_tm_t) == 36, "guest_tm_t must match picolibc's struct tm exactly");

void trap_localtime_r(cpu_t *cpu) {
    time_t const *timep = (time_t const *)arg_ptr(cpu, 0);
    guest_tm_t   *out   = (guest_tm_t *)arg_ptr(cpu, 1);
    struct tm     host_tm;
    localtime_r(timep, &host_tm);
    out->tm_sec   = host_tm.tm_sec;
    out->tm_min   = host_tm.tm_min;
    out->tm_hour  = host_tm.tm_hour;
    out->tm_mday  = host_tm.tm_mday;
    out->tm_mon   = host_tm.tm_mon;
    out->tm_year  = host_tm.tm_year;
    out->tm_wday  = host_tm.tm_wday;
    out->tm_yday  = host_tm.tm_yday;
    out->tm_isdst = host_tm.tm_isdst;
    ret_ptr(cpu, out);
}

void trap_calloc(cpu_t *cpu) {
    // From guest memory, not the host heap -- see the comment on
    // trap_heap_caps_malloc() in esp_shim.c.
    ret_ptr(cpu, guest_mem_calloc((size_t)arg_u32(cpu, 0) * arg_u32(cpu, 1), 8));
}

void trap_strftime(cpu_t *cpu) {
    // The guest's struct tm is the 36-byte picolibc one (see guest_tm_t
    // above); glibc's has two extra fields, so the fields are copied across
    // rather than the struct reinterpreted.
    char             *out  = (char *)arg_ptr(cpu, 0);
    size_t const      max  = (size_t)arg_u32(cpu, 1);
    char const       *fmt  = (char const *)arg_ptr(cpu, 2);
    guest_tm_t const *gtm  = (guest_tm_t const *)arg_ptr(cpu, 3);
    struct tm         host = {0};
    host.tm_sec   = gtm->tm_sec;
    host.tm_min   = gtm->tm_min;
    host.tm_hour  = gtm->tm_hour;
    host.tm_mday  = gtm->tm_mday;
    host.tm_mon   = gtm->tm_mon;
    host.tm_year  = gtm->tm_year;
    host.tm_wday  = gtm->tm_wday;
    host.tm_yday  = gtm->tm_yday;
    host.tm_isdst = gtm->tm_isdst;
    ret_u32(cpu, (uint32_t)strftime(out, max, fmt, &host));
}
void trap_free(cpu_t *cpu) {
    guest_mem_free(arg_ptr(cpu, 0));
}
void trap_malloc(cpu_t *cpu) {
    ret_ptr(cpu, guest_mem_calloc(arg_u32(cpu, 0), 8));
}
void trap_realloc(cpu_t *cpu) {
    ret_ptr(cpu, guest_mem_realloc(arg_ptr(cpu, 0), arg_u32(cpu, 1)));
}

void trap_atoi(cpu_t *cpu) {
    ret_u32(cpu, (uint32_t)atoi((char const *)arg_ptr(cpu, 0)));
}
void trap_strtol(cpu_t *cpu) {
    char *endptr = NULL;
    long  v      = strtol((char const *)arg_ptr(cpu, 0), &endptr, arg_i32(cpu, 2));
    char **out_end = (char **)arg_ptr(cpu, 1);
    if (out_end != NULL) {
        *out_end = endptr;
    }
    ret_u32(cpu, (uint32_t)v);
}
void trap_strtoul(cpu_t *cpu) {
    char         *endptr = NULL;
    unsigned long v      = strtoul((char const *)arg_ptr(cpu, 0), &endptr, arg_i32(cpu, 2));
    char        **out_end = (char **)arg_ptr(cpu, 1);
    if (out_end != NULL) {
        *out_end = endptr;
    }
    ret_u32(cpu, (uint32_t)v);
}
void trap_strchr(cpu_t *cpu) {
    ret_ptr(cpu, strchr((char *)arg_ptr(cpu, 0), arg_i32(cpu, 1)));
}
void trap_strrchr(cpu_t *cpu) {
    ret_ptr(cpu, strrchr((char *)arg_ptr(cpu, 0), arg_i32(cpu, 1)));
}
void trap_strstr(cpu_t *cpu) {
    ret_ptr(cpu, strstr((char *)arg_ptr(cpu, 0), (char *)arg_ptr(cpu, 1)));
}
void trap_strcspn(cpu_t *cpu) {
    ret_u32(cpu, (uint32_t)strcspn((char const *)arg_ptr(cpu, 0), (char const *)arg_ptr(cpu, 1)));
}
void trap_strlcat(cpu_t *cpu) {
    // BSD strlcat, same story as trap_strlcpy() below: not in every libc this
    // emulator is built against, so implement it directly rather than depend
    // on the host version. It returns the length it tried to create -- the
    // original length of dst plus strlen(src) -- not the length it wrote.
    char       *dst  = (char *)arg_ptr(cpu, 0);
    char const *src  = (char const *)arg_ptr(cpu, 1);
    size_t      size = arg_u32(cpu, 2);
    size_t      dlen = strnlen(dst, size);
    if (dlen >= size) {
        ret_u32(cpu, (uint32_t)(size + strlen(src)));  // dst wasn't terminated within size bytes
        return;
    }
    strncat(dst, src, size - dlen - 1);
    ret_u32(cpu, (uint32_t)(dlen + strlen(src)));
}
void trap_strncat(cpu_t *cpu) {
    ret_ptr(cpu, strncat((char *)arg_ptr(cpu, 0), (char const *)arg_ptr(cpu, 1), arg_u32(cpu, 2)));
}
void trap_strlcpy(cpu_t *cpu) {
    // BSD strlcpy isn't in glibc; it's a bounded strcpy that always
    // NUL-terminates (when size > 0) and returns strlen(src).
    char       *dst  = (char *)arg_ptr(cpu, 0);
    char const *src  = (char const *)arg_ptr(cpu, 1);
    size_t      size = arg_u32(cpu, 2);
    size_t      len  = strlen(src);
    if (size > 0) {
        size_t n = (len < size - 1) ? len : size - 1;
        memcpy(dst, src, n);
        dst[n] = '\0';
    }
    ret_u32(cpu, (uint32_t)len);
}

// ctype case mapping. The guest's newlib/picolibc has both as real functions;
// the host's <ctype.h> usually makes them macros, so implement the C locale
// behaviour directly: anything that is not an ASCII letter (EOF included)
// comes back unchanged.
void trap_tolower(cpu_t *cpu) {
    int c = arg_i32(cpu, 0);
    ret_u32(cpu, (uint32_t)((c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c));
}
void trap_toupper(cpu_t *cpu) {
    int c = arg_i32(cpu, 0);
    ret_u32(cpu, (uint32_t)((c >= 'a' && c <= 'z') ? c - 'a' + 'A' : c));
}

void trap_fflush(cpu_t *cpu) {
    FILE *f = handle_get(arg_u32(cpu, 0));
    ret_u32(cpu, (uint32_t)fflush(f));
}
void trap_fputc(cpu_t *cpu) {
    // fputc(int c, FILE *stream): the character in a0, the handle in a1.
    FILE *f = handle_get(arg_u32(cpu, 1));
    ret_u32(cpu, (uint32_t)(f ? fputc(arg_i32(cpu, 0), f) : -1));
}
void trap_ftell(cpu_t *cpu) {
    FILE *f = handle_get(arg_u32(cpu, 0));
    ret_u32(cpu, (uint32_t)(f ? ftell(f) : -1));
}
void trap_rmdir(cpu_t *cpu) {
    char        buf[1024];
    char const *path = fsroot_translate((char const *)arg_ptr(cpu, 0), buf, sizeof(buf));
    ret_u32(cpu, (uint32_t)rmdir(path));
}

void trap_fprintf(cpu_t *cpu) {
    FILE       *f   = handle_get(arg_u32(cpu, 0));
    char const *fmt = (char const *)arg_ptr(cpu, 1);
    // See trap_snprintf()'s comment; fprintf()'s two named arguments are the
    // stream and the format string, so its varargs start in a2.
    if (f == NULL) {
        ret_u32(cpu, (uint32_t)-1);
        return;
    }
    guest_va_t va;
    guest_va_init_call(&va, cpu, 2);
    ret_u32(cpu, (uint32_t)guest_vfprintf(&va, f, fmt));
}
void trap_sprintf(cpu_t *cpu) {
    // sprintf() is snprintf() with no bound. The varargs start in a2 (buf and
    // fmt are its two named arguments) -- see trap_snprintf()'s comment.
    // SIZE_MAX rather than a real size because there is no size to honour:
    // overflowing the caller's buffer is sprintf()'s own footgun, not ours.
    char       *buf = (char *)arg_ptr(cpu, 0);
    char const *fmt = (char const *)arg_ptr(cpu, 1);
    guest_va_t  va;
    guest_va_init_call(&va, cpu, 2);
    ret_u32(cpu, (uint32_t)guest_vsnprintf(&va, buf, SIZE_MAX, fmt));
}
void trap_vsnprintf(cpu_t *cpu) {
    // Unlike trap_snprintf()/trap_fprintf() (which read the *caller's* argument
    // registers directly, since those are genuine varargs calls), this is
    // vsnprintf(): the guest already called va_start() and hands its va_list
    // over as the 4th argument, in a3 (a va_list is 4 bytes on riscv32, so it
    // travels in a register like any other 4-byte scalar).
    //
    // That value is a plain slot pointer into the callee's register-save area,
    // which GCC places immediately before the stack-argument area so that one
    // pointer can walk both -- see rule 5 in guest_varargs.h. Reading it as
    // guest memory (identity-mapped, see guest_mem.h) is therefore exact. An
    // earlier version of this function ignored the pointer entirely and called
    // snprintf(buf, size, fmt) with zero arguments, which is undefined
    // behaviour (it reads whatever garbage is on the host's own stack) for any
    // format string with a real conversion in it -- see the crash this caused
    // in main/common/trace.c's trace()/dbg_log() in tanmatsu-synthminer-grace.
    char       *buf  = (char *)arg_ptr(cpu, 0);
    size_t      size = arg_u32(cpu, 1);
    char const *fmt  = (char const *)arg_ptr(cpu, 2);
    uint32_t    ap   = arg_u32(cpu, 3);
    guest_va_t  va;
    guest_va_init_va_list(&va, cpu, ap);
    ret_u32(cpu, (uint32_t)guest_vsnprintf(&va, buf, size, fmt));
}
void trap_sscanf(cpu_t *cpu) {
    char const *str = (char const *)arg_ptr(cpu, 0);
    char const *fmt = (char const *)arg_ptr(cpu, 1);
    // Forward every remaining integer arg register (a2-a7, the max RV32
    // has): str/fmt alone use a0/a1, so up to 6 scanf targets. An earlier
    // version only forwarded 4 -- enough for most calls, but not e.g.
    // "%15s %f %f %f %n" (a real app_portals.so format string: %s + three
    // %f + %n is 5 targets), where the unforwarded 5th (%n's int*) left
    // this function's own host-side sscanf() call with one fewer argument
    // than its format string needs, so it read an uninitialized value off
    // the host stack/registers for that target and crashed writing through
    // it as a garbage pointer -- see the crash this caused on startup.
    ret_u32(
        cpu, (uint32_t)sscanf(
                 str, fmt, (void *)(uintptr_t)arg_u32(cpu, 2), (void *)(uintptr_t)arg_u32(cpu, 3),
                 (void *)(uintptr_t)arg_u32(cpu, 4), (void *)(uintptr_t)arg_u32(cpu, 5),
                 (void *)(uintptr_t)arg_u32(cpu, 6), (void *)(uintptr_t)arg_u32(cpu, 7)
             )
    );
}

void trap_f_rename(cpu_t *cpu) {
    char bufa[1024], bufb[1024];
    char const *a = fsroot_translate((char const *)arg_ptr(cpu, 0), bufa, sizeof(bufa));
    char const *b = fsroot_translate((char const *)arg_ptr(cpu, 1), bufb, sizeof(bufb));
    ret_u32(cpu, rename(a, b) == 0 ? 0 : 4 /* FR_NO_FILE (approximated) */);
}
void trap_f_unlink(cpu_t *cpu) {
    char        buf[1024];
    char const *path = fsroot_translate((char const *)arg_ptr(cpu, 0), buf, sizeof(buf));
    ret_u32(cpu, remove(path) == 0 ? 0 : 4 /* FR_NO_FILE (approximated) */);
}

// Directory walking and file renaming/deleting, i.e. the POSIX half of the
// VFS API: opendir/readdir/readdir_r/closedir, rename, unlink, remove,
// access. The real loader answers these with esp_vfs_opendir() & co. (IDF's
// components/vfs/vfs_calls.c), which for /int and /sd end up in its FatFs
// and littlefs VFS shims; here they go straight to the host's own calls, on
// the directories fsroot_translate() maps those two mount points onto.

// app.so's `struct dirent` is picolibc's, not glibc's: ino_t is 16 bits,
// there is no d_off/d_reclen, and d_name is 256 bytes -- 260 bytes (d_ino at
// 0, d_type at 2, d_name at 3) against the 268 this -m32 emulator's own
// glibc has, with different offsets throughout. Verified the same way as
// guest_stat_t above: compiled a probe with the real riscv32-esp-elf-gcc and
// read sizeof()/offsetof() back out of the object file's symbol table.
typedef struct {
    uint16_t d_ino;
    uint8_t  d_type;
    char     d_name[256];
} guest_dirent_t;
_Static_assert(sizeof(guest_dirent_t) == 260, "guest_dirent_t must match picolibc's struct dirent exactly");

// The guest's DT_* values are picolibc's (DT_UNKNOWN 0, DT_REG 1, DT_DIR 2),
// not glibc's (DT_UNKNOWN 0, DT_REG 8, DT_DIR 4), so they get their own names
// here instead of the <dirent.h> macros.
#define GUEST_DT_UNKNOWN 0
#define GUEST_DT_REG     1
#define GUEST_DT_DIR     2

// The guest's DIR is 4 bytes of VFS bookkeeping whose contents belong to
// whichever VFS implementation served opendir(); apps never read through it,
// they only hand it back to readdir()/closedir(). So this emulator hands back
// a pointer to a guest-memory block of its own, exactly like the real
// implementations do (they allocate a DIR large enough for their private
// state and return that). It has to be guest memory rather than the host
// heap because readdir() hands back a pointer *into* it, and guest registers
// are 32 bits wide (see guest_mem.h).
typedef struct {
    DIR           *host_dir;
    char           path[1024];  // the directory being read, for the d_type fallback below
    guest_dirent_t ent;         // readdir()'s result, stable until the next readdir()/closedir()
} guest_dir_t;

// The next entry of `gd`, skipping "." and ".." the way the device's FatFs
// and littlefs volumes never offer them in the first place. Returns NULL at
// end of directory, which is the end-of-directory marker the guest's
// readdir() callers expect; readdir() leaves errno alone there, so a caller
// that checks trap___errno() sees no error.
static struct dirent *next_host_entry(guest_dir_t *gd) {
    struct dirent *ent;
    while ((ent = readdir(gd->host_dir)) != NULL) {
        if (strcmp(ent->d_name, ".") != 0 && strcmp(ent->d_name, "..") != 0) {
            return ent;
        }
    }
    return NULL;
}

static void fill_guest_dirent(guest_dirent_t *dst, struct dirent const *src, char const *dir_path) {
    memset(dst, 0, sizeof(*dst));
    dst->d_ino = (uint16_t)src->d_ino;  // the guest's ino_t is 16 bits, so this is a truncation
    dst->d_type = GUEST_DT_UNKNOWN;
    if (src->d_type == DT_DIR) {
        dst->d_type = GUEST_DT_DIR;
    } else if (src->d_type == DT_REG) {
        dst->d_type = GUEST_DT_REG;
    } else {
        // Some host filesystems report DT_UNKNOWN for everything, and a
        // symlink is DT_LNK, which the guest has no value for at all. Ask
        // the host what it actually is rather than give up.
        char        full[1200];
        snprintf(full, sizeof(full), "%s/%s", dir_path, src->d_name);
        struct stat st;
        if (stat(full, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                dst->d_type = GUEST_DT_DIR;
            } else if (S_ISREG(st.st_mode)) {
                dst->d_type = GUEST_DT_REG;
            }
        }
    }
    snprintf(dst->d_name, sizeof(dst->d_name), "%s", src->d_name);
}

void trap_opendir(cpu_t *cpu) {
    char        buf[1024];
    char const *path = fsroot_translate((char const *)arg_ptr(cpu, 0), buf, sizeof(buf));
    DIR        *d    = opendir(path);
    if (d == NULL) {
        ret_u32(cpu, 0);  // NULL; the host's errno reaches the guest through trap___errno()
        return;
    }
    guest_dir_t *gd = (guest_dir_t *)guest_mem_calloc(sizeof(guest_dir_t), _Alignof(guest_dir_t));
    gd->host_dir = d;
    snprintf(gd->path, sizeof(gd->path), "%s", path);
    ret_ptr(cpu, gd);
}
void trap_readdir(cpu_t *cpu) {
    guest_dir_t *gd = (guest_dir_t *)arg_ptr(cpu, 0);
    if (gd == NULL || gd->host_dir == NULL) {
        errno = EBADF;
        ret_u32(cpu, 0);
        return;
    }
    struct dirent *ent = next_host_entry(gd);
    if (ent == NULL) {
        ret_u32(cpu, 0);  // NULL end-of-directory marker
        return;
    }
    fill_guest_dirent(&gd->ent, ent, gd->path);
    ret_ptr(cpu, &gd->ent);  // valid until the next readdir()/closedir(), as on the device
}
void trap_readdir_r(cpu_t *cpu) {
    guest_dir_t     *gd  = (guest_dir_t *)arg_ptr(cpu, 0);
    guest_dirent_t  *out = (guest_dirent_t *)arg_ptr(cpu, 1);
    guest_dirent_t **res = (guest_dirent_t **)arg_ptr(cpu, 2);
    if (gd == NULL || gd->host_dir == NULL || out == NULL) {
        errno = EBADF;
        ret_u32(cpu, (uint32_t)(int32_t)-1);
        return;
    }
    struct dirent *ent = next_host_entry(gd);
    if (ent == NULL) {
        if (res != NULL) {
            *res = NULL;  // end of directory, reported through *out_dirent rather than the return value
        }
        ret_u32(cpu, 0);
        return;
    }
    fill_guest_dirent(out, ent, gd->path);
    if (res != NULL) {
        *res = out;
    }
    ret_u32(cpu, 0);
}
void trap_closedir(cpu_t *cpu) {
    guest_dir_t *gd = (guest_dir_t *)arg_ptr(cpu, 0);
    if (gd == NULL || gd->host_dir == NULL) {
        errno = EBADF;
        ret_u32(cpu, (uint32_t)(int32_t)-1);
        return;
    }
    int rc = closedir(gd->host_dir);
    gd->host_dir = NULL;
    guest_mem_free(gd);
    ret_u32(cpu, (uint32_t)rc);
}

// rename/unlink/remove/access return the host's own value, -1 rather than a
// FatFs FRESULT, so the host errno reaches the guest through trap___errno()
// exactly as esp_vfs_rename() & co. pass theirs through on the device.
void trap_rename(cpu_t *cpu) {
    char bufa[1024], bufb[1024];
    char const *a = fsroot_translate((char const *)arg_ptr(cpu, 0), bufa, sizeof(bufa));
    char const *b = fsroot_translate((char const *)arg_ptr(cpu, 1), bufb, sizeof(bufb));
    ret_u32(cpu, (uint32_t)(int32_t)rename(a, b));
}
void trap_unlink(cpu_t *cpu) {
    char        buf[1024];
    char const *path = fsroot_translate((char const *)arg_ptr(cpu, 0), buf, sizeof(buf));
    ret_u32(cpu, (uint32_t)(int32_t)unlink(path));
}
void trap_remove(cpu_t *cpu) {
    // There is no esp_vfs_remove(): the `remove` the loader exports is
    // picolibc's own, which tries unlink() and falls back to rmdir() -- both
    // of which land in the VFS entry points above. The host's remove() does
    // exactly the same, so it is the right thing to call here.
    char        buf[1024];
    char const *path = fsroot_translate((char const *)arg_ptr(cpu, 0), buf, sizeof(buf));
    ret_u32(cpu, (uint32_t)(int32_t)remove(path));
}
void trap_access(cpu_t *cpu) {
    char        buf[1024];
    char const *path = fsroot_translate((char const *)arg_ptr(cpu, 0), buf, sizeof(buf));
    ret_u32(cpu, (uint32_t)(int32_t)access(path, arg_i32(cpu, 1)));
}
