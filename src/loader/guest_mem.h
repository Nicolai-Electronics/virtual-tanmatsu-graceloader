#pragma once

// Guest address space for the emulated RISC-V32 app.
//
// The whole guest memory region is a single mmap() below the 2 GiB mark
// (MAP_32BIT), so that a 32-bit guest address and the real host pointer to
// that byte are numerically identical (zero-extended). This means buffers
// the app passes to host-implemented functions (pax-gfx, bsp_*, libc shims,
// ...) can be used directly as host pointers with a plain cast -- no
// separate guest/host address translation is needed anywhere.

#include <stddef.h>
#include <stdint.h>

// 2 GiB: below TRAP_BASE (0xF0000000, ~3.75 GiB) with a comfortable margin
// for this (32-bit) process's other mappings -- shared libs (SDL2,
// PipeWire, Mesa/gallium for its GPU-accelerated renderer, ...), thread
// stacks, the host heap -- to fit without colliding with the guest region
// or failing the mmap outright. Bumped from 1.5 GiB: app_portals.so (a
// puzzle-platformer *with a level editor*, i.e. apps can load arbitrarily
// many levels' worth of assets in one session) ran this bump allocator
// (which never reclaims anything -- see guest_mem_alloc()) out of room
// again, this time starting a new game rather than just on startup. A
// bigger budget buys more headroom, not a fix for the underlying "nothing
// is ever freed" design -- a long enough session (many levels started/
// edited) can still in principle exhaust any fixed size.
#define GUEST_MEM_SIZE (2048u * 1024 * 1024)

// Reserved range of "addresses" used for imported-symbol trampolines. These
// values are never backed by real memory and never dereferenced as code --
// the interpreter special-cases any PC in this range (see trap.h) -- so they
// are placed safely above the mmap'd guest region (which stays under 2 GiB).
#define TRAP_BASE   0xF0000000u
#define TRAP_STRIDE 4u
#define TRAP_MAX    2048u
#define TRAP_END    (TRAP_BASE + TRAP_STRIDE * TRAP_MAX)

// Reserve the low page so that guest address 0 (NULL) is never a valid
// allocation.
#define GUEST_BASE_RESERVED 4096u

void    guest_mem_init(void);
// Host pointer to the base of the guest region (guest address 0).
void   *guest_mem_base(void);
// Bump-allocate `size` bytes aligned to `align` from the guest region.
// Returns the guest address (== host pointer value) of the allocation.
uintptr_t guest_mem_alloc(size_t size, size_t align);

// Like guest_mem_alloc(), but returns a host pointer, and the memory is
// always zero-filled -- whether it's fresh (virgin mmap pages, already zero)
// or reused from a freed allocation (explicitly memset; it may hold a
// previous allocation's leftover data). Use this instead of malloc/calloc
// for any struct whose pointer may be handed back to the guest as an opaque
// handle (QueueHandle_t, SemaphoreHandle_t, ...) -- guest registers are 32
// bits wide, so a pointer from the ordinary host heap (anywhere in the
// 64-bit address space) would be silently truncated.
void *guest_mem_calloc(size_t size, size_t align);

// Marks an allocation (from guest_mem_alloc()/guest_mem_calloc()) free for
// reuse by a later allocation, coalescing with free physical neighbours.
// NULL is a no-op, matching free()'s contract. Backs the guest-visible
// free()/heap_caps_free() traps -- see trap_free() in libc_shim.c and
// trap_heap_caps_free() in esp_shim.c.
void guest_mem_free(void *ptr);

// Matches realloc()'s contract: NULL ptr behaves like guest_mem_calloc(),
// new_size 0 behaves like guest_mem_free() (returning NULL), otherwise
// grows/shrinks in place if the existing block already has room, else
// allocates fresh, copies the old data, and frees the old block. Backs
// trap_realloc() in libc_shim.c and trap_heap_caps_realloc() in esp_shim.c.
void *guest_mem_realloc(void *ptr, size_t new_size);
