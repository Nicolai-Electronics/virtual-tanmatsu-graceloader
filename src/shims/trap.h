#pragma once

// Dispatch for calls the guest app makes into "liball.so" (graceloader's
// exported symbol table). Each imported symbol is resolved by kbelf to a
// unique address in the reserved trap region (see guest_mem.h); when the
// interpreter's PC lands there, trap_dispatch() runs the matching host C
// function instead of decoding guest instructions, then returns to the
// caller (pc = ra) as if the call had returned normally.

#include <stddef.h>

#include "cpu.h"
#include "kbelf.h"

// Number of known trap symbols.
size_t trap_count(void);
// Name of trap symbol `index` (0 <= index < trap_count()).
char const *trap_name(size_t index);
// Guest "address" assigned to trap symbol `index`.
uint32_t trap_addr(size_t index);

// Builds (once) and returns the kbelf_builtin_lib exposing every trap symbol
// at its assigned address, for kbelfx_builtin_libs[].
kbelf_builtin_lib const *trap_builtin_lib(void);

// Argument/return-value accessors for trap handlers (RISC-V calling
// convention: a0-a7 = x10-x17, fa0-fa7 = f10-f17, return in a0/a1 or fa0).
static inline uint32_t arg_u32(cpu_t *cpu, int i) {
    return cpu->x[10 + i];
}
static inline int32_t arg_i32(cpu_t *cpu, int i) {
    return (int32_t)cpu->x[10 + i];
}
static inline void *arg_ptr(cpu_t *cpu, int i) {
    uint32_t a = cpu->x[10 + i];
    return a == 0 ? NULL : (void *)(uintptr_t)a;
}
static inline float arg_f32(cpu_t *cpu, int i) {
    return cpu->f[10 + i];
}
static inline void ret_u32(cpu_t *cpu, uint32_t v) {
    cpu->x[10] = v;
}
static inline void ret_u64(cpu_t *cpu, uint64_t v) {
    cpu->x[10] = (uint32_t)v;
    cpu->x[11] = (uint32_t)(v >> 32);
}
static inline void ret_ptr(cpu_t *cpu, void *p) {
    cpu->x[10] = (uint32_t)(uintptr_t)p;
}
static inline void ret_f32(cpu_t *cpu, float v) {
    cpu->f[10] = v;
}

// Reads a 64-bit integer argument: like doubles (see arg_f64 below), the
// RV32 integer calling convention passes a 64-bit scalar in a pair of
// consecutive registers (lo, hi) -- for *named* arguments only, which GCC
// packs densely with no even/odd alignment requirement. Variadic 64-bit
// arguments do have to start at an even register index; anything reading a
// format string's varargs must use guest_varargs.h instead of these helpers.
// `i` is the index of the low register, e.g. 0 for the first argument
// (a0:a1), 2 for the second (a2:a3) if the first argument was also 64-bit.
static inline uint64_t arg_u64(cpu_t *cpu, int i) {
    return (uint64_t)cpu->x[10 + i] | ((uint64_t)cpu->x[10 + i + 1] << 32);
}
static inline int64_t arg_i64(cpu_t *cpu, int i) {
    return (int64_t)arg_u64(cpu, i);
}

// Reads/writes a guest-ABI "double" argument/return value: the soft-double
// calling convention used by the libgcc helpers (__adddf3 etc.) passes each
// double as a pair of consecutive integer registers (lo, hi).
static inline double arg_f64(cpu_t *cpu, int i) {
    uint64_t bits = (uint64_t)cpu->x[10 + i] | ((uint64_t)cpu->x[10 + i + 1] << 32);
    double   d;
    __builtin_memcpy(&d, &bits, 8);
    return d;
}
static inline void ret_f64(cpu_t *cpu, double v) {
    uint64_t bits;
    __builtin_memcpy(&bits, &v, 8);
    cpu->x[10] = (uint32_t)bits;
    cpu->x[11] = (uint32_t)(bits >> 32);
}
