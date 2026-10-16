// libc/newlib passthroughs. Guest pointers are real host pointers (see
// guest_mem.h), so buffers can be used directly; only handles that come from
// the host heap (FILE*) need the handle table (see handle_table.h) since
// they aren't guaranteed to fit in a 32-bit guest register.

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
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

void trap_stat(cpu_t *cpu) {
    char        buf[1024];
    char const *path = fsroot_translate((char const *)arg_ptr(cpu, 0), buf, sizeof(buf));
    struct stat st;
    int         rc = stat(path, &st);
    if (rc == 0) {
        guest_stat_t gst = {0};
        gst.st_mode      = st.st_mode;
        gst.st_nlink     = (int16_t)st.st_nlink;
        gst.st_size       = st.st_size;
        gst.st_mtim_sec   = st.st_mtime;
        gst.st_blksize    = (int32_t)st.st_blksize;
        gst.st_blocks     = (int32_t)st.st_blocks;
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
    ret_u32(cpu, (uint32_t)time(tloc));
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
void trap_free(cpu_t *cpu) {
    // Guest memory is a bump allocator for the process lifetime; nothing to
    // free.
    (void)cpu;
}
void trap_malloc(cpu_t *cpu) {
    ret_ptr(cpu, guest_mem_calloc(arg_u32(cpu, 0), 8));
}
void trap_realloc(cpu_t *cpu) {
    // Same bump-allocator rationale as trap_heap_caps_realloc() in
    // esp_shim.c: always "moves" (fresh alloc + copy forward), which is
    // correct as long as nothing still holds the old pointer afterwards --
    // and only copies up to the new size, since the old allocation's size
    // isn't tracked and over-copying past it would be the unsafe direction.
    void  *old_ptr  = arg_ptr(cpu, 0);
    size_t new_size = arg_u32(cpu, 1);
    if (new_size == 0) {
        ret_ptr(cpu, NULL);
        return;
    }
    void *new_ptr = guest_mem_calloc(new_size, 8);
    if (old_ptr != NULL) {
        memcpy(new_ptr, old_ptr, new_size);
    }
    ret_ptr(cpu, new_ptr);
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

void trap_fflush(cpu_t *cpu) {
    FILE *f = handle_get(arg_u32(cpu, 0));
    ret_u32(cpu, (uint32_t)fflush(f));
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
