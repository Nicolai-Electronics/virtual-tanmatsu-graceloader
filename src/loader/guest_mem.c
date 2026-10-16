#include "guest_mem.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

static uint8_t        *s_base   = NULL;
static size_t           s_cursor = GUEST_BASE_RESERVED;
// Once real FreeRTOS tasks are running (see freertos_shim.c's
// xTaskCreatePinnedToCore, which spawns a real pthread per task), more than
// one host thread can call guest_mem_alloc() at the same time -- a plain
// bump allocator's read-modify-write of s_cursor is not atomic, so without
// this lock two concurrent allocations can race and overlap.
static pthread_mutex_t s_mutex = PTHREAD_MUTEX_INITIALIZER;

void guest_mem_init(void) {
    if (s_base != NULL) {
        return;
    }

    // This whole program is built 32-bit (see the big comment at the top of
    // CMakeLists.txt), so every host pointer -- including this mmap's --
    // already fits in a 32-bit guest register on its own; no MAP_32BIT or
    // similar trick is needed the way it would be for a 64-bit host build.
    //
    // A 32-bit process on a 64-bit kernel gets the compat layer's mmap
    // placement, which (unlike a real 32-bit kernel's traditional
    // just-below-the-stack default) can land as high as ~0xd7000000 --
    // close enough to TRAP_BASE that GUEST_MEM_SIZE might run past it. A
    // low address hint isn't a MAP_FIXED requirement, just a preference,
    // but it's reliably honored in practice when that range is free (which
    // it is, this early in the process).
    void *mem = mmap(
        (void *)0x20000000, GUEST_MEM_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0
    );
    if (mem == MAP_FAILED) {
        perror("guest_mem_init: mmap");
        exit(1);
    }
    if ((uintptr_t)mem + GUEST_MEM_SIZE >= TRAP_BASE) {
        fprintf(stderr, "guest_mem_init: mmap landed too high (%p), overlaps trap region\n", mem);
        exit(1);
    }

    s_base = (uint8_t *)mem;
}

void *guest_mem_base(void) {
    return s_base;
}

uintptr_t guest_mem_alloc(size_t size, size_t align) {
    if (align == 0) {
        align = 1;
    }
    pthread_mutex_lock(&s_mutex);
    size_t aligned = (s_cursor + align - 1) & ~(align - 1);
    size_t next    = aligned + size;
    if (next > GUEST_MEM_SIZE) {
        fprintf(stderr, "guest_mem_alloc: out of guest memory (requested %zu, cursor %zu)\n", size, aligned);
        exit(1);
    }
    s_cursor = next;
    pthread_mutex_unlock(&s_mutex);
    return (uintptr_t)(s_base + aligned);
}

void *guest_mem_calloc(size_t size, size_t align) {
    return (void *)guest_mem_alloc(size, align);
}
