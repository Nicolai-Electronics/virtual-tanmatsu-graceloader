#include "guest_mem.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

static uint8_t *s_base = NULL;

// A real allocator over the guest arena: a bump allocator that never
// reclaims anything (this used to be the whole implementation) works fine
// right up until an app's own alloc/free pattern assumes free() actually
// means something -- which real hardware's allocator, with its 32 MiB of
// PSRAM, absolutely enforces. app_portals.so (a puzzle-platformer with a
// level editor, so a session can load arbitrarily many levels) frees each
// level's assets when the level ends and expects that memory back; a bump
// allocator just leaks it, so a couple of levels were enough to exhaust
// even a multi-GiB guest arena that real hardware would never get close to
// needing. See the git history for the growing GUEST_MEM_SIZE bumps that
// were papering over this before it was root-caused.
//
// Design: a doubly-linked, address-ordered list of fixed-overhead blocks.
// Every block -- free or used -- starts with a block_header_t; the header
// size is fixed at BLOCK_HEADER_SIZE, and every block's start address (and
// so every returned user pointer, immediately after the header) is a
// multiple of it. That one invariant is what satisfies every alignment
// request in this codebase (the largest is SE_PPA_CACHE_LINE, 128 bytes)
// without the complexity of case-by-case alignment padding. The tradeoff is
// up to BLOCK_HEADER_SIZE bytes of overhead per allocation, which is
// negligible against the KB-to-MB sized buffers/textures/structs this
// emulator actually allocates (never a hot per-pixel path).
//
// Allocation is first-fit over the full address-ordered list (simple, and
// fine: allocation counts here are in the hundreds, not a hot path), with a
// free block split if the leftover is usefully large. Freeing coalesces
// with both physical neighbours if they're also free. Anything the arena
// has never touched stays physically zero-filled by the kernel; anything
// reused from a freed block is explicitly memset, since it may hold a
// previous allocation's leftover data.
#define BLOCK_HEADER_SIZE 128

typedef struct block_header {
    size_t               span;  // bytes from this header to the start of the next block (header + payload)
    bool                  used;
    struct block_header *prev;  // previous block by address, NULL if this is the first block
    struct block_header *next;  // next block by address, NULL if this is the last carved block
} block_header_t;
_Static_assert(sizeof(block_header_t) <= BLOCK_HEADER_SIZE, "block_header_t must fit in BLOCK_HEADER_SIZE");

static block_header_t *s_first  = NULL;  // first block in the arena, NULL if none carved yet
static block_header_t *s_last   = NULL;  // last (highest-address) block carved so far
static size_t           s_cursor = GUEST_BASE_RESERVED;  // first never-touched byte of the arena
// Once real FreeRTOS tasks are running (see freertos_shim.c's
// xTaskCreatePinnedToCore, which spawns a real pthread per task), more than
// one host thread can call into the allocator at the same time, so every
// operation on the block list (and the bump cursor) is under this lock.
static pthread_mutex_t s_mutex = PTHREAD_MUTEX_INITIALIZER;

static size_t round_up(size_t v, size_t m) {
    return (v + m - 1) & ~(m - 1);
}

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

// First-fit: the first free block (by address) with enough payload
// capacity. Must be called with s_mutex held.
static block_header_t *find_free(size_t size) {
    for (block_header_t *b = s_first; b != NULL; b = b->next) {
        if (!b->used && (b->span - BLOCK_HEADER_SIZE) >= size) {
            return b;
        }
    }
    return NULL;
}

// If `b` (a free block about to become used) has more than `needed_span`
// bytes, split off the remainder as a new free block right after it in the
// address-ordered list. Must be called with s_mutex held.
static void maybe_split(block_header_t *b, size_t needed_span) {
    size_t remaining = b->span - needed_span;
    // Not worth splitting off a sliver that costs a full header for less
    // than a header's worth of usable payload.
    if (remaining < 2 * BLOCK_HEADER_SIZE) {
        return;
    }
    block_header_t *rem = (block_header_t *)((char *)b + needed_span);
    rem->span            = remaining;
    rem->used            = false;
    rem->prev            = b;
    rem->next            = b->next;
    if (rem->next != NULL) {
        rem->next->prev = rem;
    } else {
        s_last = rem;
    }
    b->next = rem;
    b->span = needed_span;
}

static void *alloc_payload(size_t size) {
    pthread_mutex_lock(&s_mutex);

    block_header_t *b = find_free(size);
    if (b != NULL) {
        maybe_split(b, round_up(BLOCK_HEADER_SIZE + size, BLOCK_HEADER_SIZE));
        b->used = true;
        pthread_mutex_unlock(&s_mutex);
        void *ptr = (char *)b + BLOCK_HEADER_SIZE;
        // Reused memory may hold a previous allocation's leftover data,
        // unlike virgin arena pages (which the kernel already zeroed) --
        // see guest_mem_calloc()'s documented zero-fill guarantee.
        memset(ptr, 0, size);
        return ptr;
    }

    // Nothing free is big enough: carve a brand-new block from the
    // untouched arena tail, same as the old bump allocator.
    size_t span       = round_up(BLOCK_HEADER_SIZE + size, BLOCK_HEADER_SIZE);
    size_t block_addr = round_up(s_cursor, BLOCK_HEADER_SIZE);
    size_t next_cursor = block_addr + span;
    if (next_cursor > GUEST_MEM_SIZE) {
        pthread_mutex_unlock(&s_mutex);
        fprintf(stderr, "guest_mem_alloc: out of guest memory (requested %zu, cursor %zu)\n", size, block_addr);
        exit(1);
    }
    s_cursor = next_cursor;

    block_header_t *nb = (block_header_t *)(s_base + block_addr);
    nb->span            = span;
    nb->used            = true;
    nb->prev            = s_last;
    nb->next            = NULL;
    if (s_last != NULL) {
        s_last->next = nb;
    } else {
        s_first = nb;
    }
    s_last = nb;

    pthread_mutex_unlock(&s_mutex);
    // Fresh arena memory is already zero (virgin mmap pages); no memset
    // needed, same guarantee the old bump allocator relied on.
    return (char *)nb + BLOCK_HEADER_SIZE;
}

// Special path for alignment bigger than BLOCK_HEADER_SIZE -- in practice
// only kbelfx_seg_alloc() (see kbelfx_host.c), which requests ELF PT_LOAD
// segments' own alignment (commonly page-sized, 4096) and -- per
// kbelfx_seg_free()'s comment -- never frees them: apps aren't unloaded
// mid-process, any more than real hardware unloads one. That means this
// path doesn't need to participate in the reusable block pool at all; it
// just needs to not corrupt the *surrounding* blocks' physical-adjacency
// bookkeeping, which coalescing depends on. So it's still represented as a
// block in the address-ordered list -- permanently `used`, which makes it
// invisible to find_free()/coalescing -- just carved with however much
// extra leading padding the requested alignment needs, instead of the
// usual fixed BLOCK_HEADER_SIZE offset.
//
// That means a pointer from here does NOT sit exactly BLOCK_HEADER_SIZE
// after its block header the way every other allocation does -- fine only
// because nothing ever calls guest_mem_free()/guest_mem_realloc() on one.
static void *alloc_large_aligned(size_t size, size_t align) {
    pthread_mutex_lock(&s_mutex);

    size_t block_addr  = round_up(s_cursor, BLOCK_HEADER_SIZE);
    size_t user_addr    = round_up(block_addr + BLOCK_HEADER_SIZE, align);
    size_t span         = round_up((user_addr + size) - block_addr, BLOCK_HEADER_SIZE);
    size_t next_cursor  = block_addr + span;
    if (next_cursor > GUEST_MEM_SIZE) {
        pthread_mutex_unlock(&s_mutex);
        fprintf(stderr, "guest_mem_alloc: out of guest memory (requested %zu, cursor %zu)\n", size, block_addr);
        exit(1);
    }
    s_cursor = next_cursor;

    block_header_t *nb = (block_header_t *)(s_base + block_addr);
    nb->span            = span;
    nb->used            = true;  // permanently: nothing ever frees one of these
    nb->prev            = s_last;
    nb->next            = NULL;
    if (s_last != NULL) {
        s_last->next = nb;
    } else {
        s_first = nb;
    }
    s_last = nb;

    pthread_mutex_unlock(&s_mutex);
    return s_base + user_addr;
}

uintptr_t guest_mem_alloc(size_t size, size_t align) {
    if (align > BLOCK_HEADER_SIZE) {
        return (uintptr_t)alloc_large_aligned(size, align);
    }
    return (uintptr_t)alloc_payload(size);
}

void *guest_mem_calloc(size_t size, size_t align) {
    return (void *)guest_mem_alloc(size, align);
}

void guest_mem_free(void *ptr) {
    if (ptr == NULL) {
        return;
    }
    block_header_t *b = (block_header_t *)((char *)ptr - BLOCK_HEADER_SIZE);

    pthread_mutex_lock(&s_mutex);
    b->used = false;

    // Coalesce with the next physical block, if it's also free.
    if (b->next != NULL && !b->next->used) {
        block_header_t *n = b->next;
        b->span += n->span;
        b->next  = n->next;
        if (b->next != NULL) {
            b->next->prev = b;
        } else {
            s_last = b;
        }
    }
    // Coalesce with the previous physical block, if it's also free.
    if (b->prev != NULL && !b->prev->used) {
        block_header_t *p = b->prev;
        p->span += b->span;
        p->next  = b->next;
        if (p->next != NULL) {
            p->next->prev = p;
        } else {
            s_last = p;
        }
    }
    pthread_mutex_unlock(&s_mutex);
}

void *guest_mem_realloc(void *ptr, size_t new_size) {
    if (ptr == NULL) {
        return alloc_payload(new_size);
    }
    if (new_size == 0) {
        guest_mem_free(ptr);
        return NULL;
    }

    block_header_t *b            = (block_header_t *)((char *)ptr - BLOCK_HEADER_SIZE);
    size_t           old_capacity = b->span - BLOCK_HEADER_SIZE;
    if (old_capacity >= new_size) {
        // Already big enough: keep it in place rather than move+copy+free
        // for a shrink or same-size request.
        return ptr;
    }

    void *new_ptr = alloc_payload(new_size);
    memcpy(new_ptr, ptr, old_capacity);
    guest_mem_free(ptr);
    return new_ptr;
}
