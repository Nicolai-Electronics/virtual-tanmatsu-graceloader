#pragma once

// One general implementation of the guest's variadic calling convention, for
// every trap that has to interpret a guest format string (snprintf, printf,
// fprintf, vsnprintf, esp_log, ...).
//
// Why this exists: app.so is compiled rv32imafc/-mabi=ilp32f, and GCC's
// RISC-V backend lays its varargs out differently from any host C compiler.
// Forwarding the guest's a0-a7 into a host vararg list -- what these traps
// used to do -- silently misreads every argument after the first 64-bit one.
// The rules below were measured against riscv32-esp-elf-gcc 15.2.0 codegen
// (rv32imafc/ilp32f, the same flags app.so is built with), not guessed:
//
//   1. In a *named* argument list, 64-bit scalars go in consecutive
//      registers with no alignment requirement (a0, a1:a2, a3:a4, ...).
//   2. In a *variadic* argument list, every 8-byte argument must start at an
//      even argument-register index. So snprintf(buf, size, fmt, (long long)v)
//      puts v in a4:a5 and leaves a3 empty -- not a3:a4.
//   3. After a0-a7 are exhausted, further arguments go to the outgoing stack
//      area at sp+0, sp+4, ... with the same even-offset rule for 8-byte
//      values. A 64-bit value is never split between a register and the
//      stack: if it does not fit in two remaining registers, the whole value
//      moves to the stack.
//   4. ilp32f has no 64-bit FP register, so variadic floats are promoted to
//      double and passed in *integer* register pairs exactly like long long
//      (the caller emits a __extendsfdf2 call to materialise them). fa0-fa7
//      never carry varargs.
//   5. On riscv32, __builtin_va_list is a plain 4-byte pointer, not x86-64's
//      four-field struct. It points at the first variadic slot in the
//      callee's register-save area and runs straight on into the stack
//      argument area, which the compiler lays out contiguously right after
//      it. va_arg() of an 8-byte type rounds the cursor up to a multiple of 8
//      and then consumes 8 bytes.
//
// Rules 2/3 and rule 5 are the same rule seen from the two sides of a call,
// so one cursor covers both.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "cpu.h"

typedef struct {
    cpu_t *cpu;

    // Call mode: walking the argument registers of a direct call to one of
    // the guest-visible variadic imports, then the outgoing stack area.
    int      reg;        // next argument register index; 8 == registers exhausted
    uint32_t stack_off;  // next outgoing-stack offset from the guest's sp

    // va_list mode: walking a guest va_list slot pointer (rule 5).
    uint32_t addr;
    bool     by_addr;
} guest_va_t;

// Direct call. `first_reg` is how many fixed (named) arguments the guest
// function took before its varargs, i.e. the register index the variadic
// list starts in: 3 for snprintf(buf, size, fmt, ...), 1 for printf(fmt, ...),
// 2 for fprintf(f, fmt, ...).
void guest_va_init_call(guest_va_t *va, cpu_t *cpu, int first_reg);

// va_list. `va_list_addr` is the guest's va_list value itself.
void guest_va_init_va_list(guest_va_t *va, cpu_t *cpu, uint32_t va_list_addr);

// vsnprintf()/vfprintf() for a guest format string plus guest varargs.
// guest_vsnprintf() writes at most size-1 characters into buf, always
// NUL-terminates it (when size > 0) and returns the length the complete
// output would have had, exactly like snprintf(). guest_vfprintf() writes to
// `stream` and returns the number of characters written.
int guest_vsnprintf(guest_va_t *va, char *buf, size_t size, char const *fmt);
int guest_vfprintf(guest_va_t *va, FILE *stream, char const *fmt);
