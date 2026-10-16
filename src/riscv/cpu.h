#pragma once

// A from-scratch interpreter for the RV32IMAFC + Zicsr + Zifencei subset
// used by Tanmatsu apps (see lib/kbelf's riscv port and app.so's ELF
// attributes: rv32i2p1_m2p0_a2p1_f2p2_c2p0_zicsr2p0_zifencei2p0...).
//
// Guest memory addresses are real host pointers (see guest_mem.h), so loads
///stores just dereference the address directly -- no separate address
// translation layer.

#include <stdbool.h>
#include <stdint.h>

typedef struct cpu {
    uint32_t x[32];  // integer registers, x[0] is always read as 0
    float    f[32];  // single-precision float registers
    uint32_t pc;
    uint32_t fcsr;  // bits [4:0] = fflags, bits [7:5] = frm

    uint64_t instret;

    // Reservation for lr.w/sc.w (approximated: a single global address,
    // since apps aren't expected to race on this in practice).
    bool     lr_valid;
    uint32_t lr_addr;

    // Set by the trap dispatcher (see trap.h) when the guest called
    // bsp_device_restart_to_launcher()/exited/etc. and the run loop should
    // stop stepping this cpu.
    bool halted;
} cpu_t;

// ABI register name helpers (RISC-V calling convention).
#define REG_RA 1
#define REG_SP 2
#define REG_A0 10
#define REG_A1 11
#define REG_A2 12
#define REG_A3 13
#define REG_A4 14
#define REG_A5 15
#define REG_A6 16
#define REG_A7 17

void cpu_reset(cpu_t *cpu);

// Decode and execute a single instruction at cpu->pc, advancing cpu->pc.
// Returns false on an unimplemented/illegal instruction (cpu->pc left
// pointing at the offending instruction for diagnostics).
bool cpu_step(cpu_t *cpu);

// Called by cpu_step() before fetching, so trap-region addresses (see
// guest_mem.h's TRAP_BASE) can be intercepted instead of decoded as code.
// Implemented in trap_table.c. Returns true if it handled the PC (and
// updated it), false if the interpreter should fetch+decode normally.
bool cpu_trap_hook(cpu_t *cpu, uint32_t pc);

// A guest address that is never a valid segment or trap address; used as a
// synthetic return address so a nested/spawned call can tell when the guest
// function it invoked has returned. Must be even: a guest "ret" is a jalr,
// which always clears bit 0 of its computed target, so an odd sentinel
// would come back as (sentinel - 1) and never compare equal.
#define CPU_CALL_SENTINEL 0xFFFFFFFEu

// Runs `cpu` (already set up: pc = entry, sp/a0/etc as desired, ra =
// CPU_CALL_SENTINEL) until it returns to the sentinel or halts. Used both for
// synchronous callbacks (qsort comparators) on the calling task's own cpu_t,
// and as the top-level loop for a freshly spawned FreeRTOS task's cpu_t.
void cpu_run_until_return(cpu_t *cpu);

// Calls a guest function with up to 4 integer/pointer arguments on `cpu`,
// reusing its current stack, and returns its a0. For reentrant callbacks
// (e.g. qsort comparators) invoked from within a trap handler.
uint32_t cpu_call_guest(cpu_t *cpu, uint32_t func_addr, int nargs, uint32_t const *args);
