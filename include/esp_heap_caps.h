#pragma once

// Minimal desktop stand-in for ESP-IDF's esp_heap_caps.h/multi_heap.h --
// just the one struct trap_heap_caps_get_info() (see esp_shim.c) needs.
// Field types are all plain `size_t`, verified against the real header to
// match exactly (no pointers/platform-dependent sizes to worry about).

#include <stddef.h>

typedef struct {
    size_t total_free_bytes;
    size_t total_allocated_bytes;
    size_t largest_free_block;
    size_t minimum_free_bytes;
    size_t allocated_blocks;
    size_t free_blocks;
    size_t total_blocks;
} multi_heap_info_t;
