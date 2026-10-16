// ESP-IDF core/system passthroughs (logging, error reporting, timers, heap).

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "guest_varargs.h"
#include "guest_mem.h"
#include "trap.h"

void esp_error_check_failed(esp_err_t err, const char *file, int line, const char *func, const char *expr);

void trap_esp_cache_msync(cpu_t *cpu) {
    // x86-64 has coherent caches; nothing to do.
    (void)cpu;
    ret_u32(cpu, 0);  // ESP_OK
}

void trap__esp_error_check_failed(cpu_t *cpu) {
    esp_err_t   err  = (esp_err_t)arg_i32(cpu, 0);
    char const *file = (char const *)arg_ptr(cpu, 1);
    int         line = arg_i32(cpu, 2);
    char const *func = (char const *)arg_ptr(cpu, 3);
    char const *expr = (char const *)arg_ptr(cpu, 4);
    esp_error_check_failed(err, file, line, func, expr);
}

void trap_esp_err_to_name(cpu_t *cpu) {
    ret_ptr(cpu, (void *)esp_err_to_name((esp_err_t)arg_i32(cpu, 0)));
}

void trap_esp_log(cpu_t *cpu) {
    // ESP-IDF v6: esp_log(esp_log_config_t config, char const *tag, char const
    // *format, ...). The ESP_LOGx() macros all expand straight into this
    // function, so every app log line lands here; the macro's own format
    // prefix already carries the level/timestamp/tag, so only the format
    // string and its varargs need formatting.
    char const *fmt = (char const *)arg_ptr(cpu, 2);
    if (fmt == NULL) {
        return;
    }
    guest_va_t va;
    guest_va_init_call(&va, cpu, 3);
    guest_vfprintf(&va, stdout, fmt);
    fputc('\n', stdout);
    fflush(stdout);
}

void trap_esp_log_timestamp(cpu_t *cpu) {
    ret_u32(cpu, (uint32_t)(esp_timer_get_time() / 1000));
}

void trap_esp_timer_get_time(cpu_t *cpu) {
    ret_u64(cpu, (uint64_t)esp_timer_get_time());
}

// Allocated from guest memory (see guest_mem.h), not the host heap: these
// pointers are handed back to the guest as plain 32-bit addresses, and
// nothing guarantees a host malloc()/calloc() return value fits below the
// 2 GiB mark -- in particular, under a multi-threaded process (which this
// is, once the app spawns FreeRTOS tasks or SDL's own helper threads start
// up) glibc readily serves large or thread-local allocations from mmap'd
// arenas at high addresses regardless of mallopt(M_MMAP_THRESHOLD, ...).
void trap_heap_caps_malloc(cpu_t *cpu) {
    ret_ptr(cpu, guest_mem_calloc(arg_u32(cpu, 0), 8));
}
void trap_heap_caps_calloc(cpu_t *cpu) {
    ret_ptr(cpu, guest_mem_calloc((size_t)arg_u32(cpu, 0) * arg_u32(cpu, 1), 8));
}
void trap_heap_caps_aligned_alloc(cpu_t *cpu) {
    size_t align = arg_u32(cpu, 0);
    size_t size  = arg_u32(cpu, 1);
    ret_ptr(cpu, guest_mem_calloc(size, align));
}
void trap_heap_caps_free(cpu_t *cpu) {
    // Guest memory is a bump allocator for the process lifetime; nothing to
    // free (mirrors kbelfx_seg_free()).
    (void)cpu;
}
void trap_heap_caps_get_free_size(cpu_t *cpu) {
    // No per-capability accounting on the host; report a generous constant
    // so apps that merely log/display this don't misbehave.
    ret_u32(cpu, 64u * 1024 * 1024);
}
void trap_heap_caps_get_largest_free_block(cpu_t *cpu) {
    ret_u32(cpu, 16u * 1024 * 1024);
}
void trap_heap_caps_get_minimum_free_size(cpu_t *cpu) {
    // Same "no per-capability accounting" approximation as
    // trap_heap_caps_get_free_size() above.
    ret_u32(cpu, 64u * 1024 * 1024);
}
void trap_heap_caps_aligned_calloc(cpu_t *cpu) {
    size_t align = arg_u32(cpu, 0);
    size_t n     = arg_u32(cpu, 1);
    size_t size  = arg_u32(cpu, 2);
    ret_ptr(cpu, guest_mem_calloc(n * size, align));
}
void trap_heap_caps_realloc(cpu_t *cpu) {
    // Guest memory is a bump allocator: there's no in-place growth, so this
    // always "moves" -- allocate fresh and copy forward. Correct as long as
    // nothing still holds the old pointer after a realloc, same as real
    // realloc()'s contract.
    void  *old_ptr  = arg_ptr(cpu, 0);
    size_t new_size = arg_u32(cpu, 1);
    if (new_size == 0) {
        ret_ptr(cpu, NULL);
        return;
    }
    void *new_ptr = guest_mem_calloc(new_size, 8);
    if (old_ptr != NULL) {
        // We don't track the old allocation's size; over-copying past it
        // would read adjacent (valid, if unrelated) guest memory, so copy
        // only up to the new size, which is always safe for a grow-or-same
        // realloc and is the common case here.
        memcpy(new_ptr, old_ptr, new_size);
    }
    ret_ptr(cpu, new_ptr);
}
void trap_heap_caps_get_info(cpu_t *cpu) {
    multi_heap_info_t *info = (multi_heap_info_t *)arg_ptr(cpu, 0);
    if (info != NULL) {
        memset(info, 0, sizeof(*info));
        info->total_free_bytes = 64u * 1024 * 1024;
        info->largest_free_block = 16u * 1024 * 1024;
    }
}

void trap_esp_clk_cpu_freq(cpu_t *cpu) {
    ret_u32(cpu, 400 * 1000 * 1000);  // ESP32-P4's default CPU clock
}
void trap_esp_log_level_set(cpu_t *cpu) {
    (void)cpu;
}
void trap_esp_intr_alloc(cpu_t *cpu) {
    void **out_handle = (void **)arg_ptr(cpu, 4);
    if (out_handle != NULL) {
        *out_handle = NULL;
    }
    ret_u32(cpu, ESP_ERR_NOT_SUPPORTED);
}
void trap_esp_intr_free(cpu_t *cpu) {
    (void)cpu;
    ret_u32(cpu, ESP_OK);
}
