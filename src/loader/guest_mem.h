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

// 1.5 GiB: comfortably below TRAP_BASE (0xF0000000, ~3.75 GiB) even with
// guest_mem_init()'s 0x20000000 mmap hint, but with real headroom above
// 512 MiB -- app_portals.so's one-time asset loading (PNG decodes etc, all
// through this bump allocator, which never reclaims anything -- see
// guest_mem_alloc()) ran past the old 512 MiB budget.
#define GUEST_MEM_SIZE (1536u * 1024 * 1024)

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

// Like guest_mem_alloc(), but returns a host pointer (the memory is already
// zero-filled: fresh, never-reused mmap pages). Use this instead of
// malloc/calloc for any struct whose pointer may be handed back to the guest
// as an opaque handle (QueueHandle_t, SemaphoreHandle_t, ...) -- guest
// registers are 32 bits wide, so a pointer from the ordinary host heap
// (anywhere in the 64-bit address space) would be silently truncated.
void *guest_mem_calloc(size_t size, size_t align);
