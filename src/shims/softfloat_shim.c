// libgcc runtime helpers the compiler emits for double-precision arithmetic
// and 64-bit integer division (this is a single-float ABI target: `double`
// has no hardware support, so every double operation is lowered to an
// explicit call using the soft-call convention -- see arg_f64()/ret_f64() in
// trap.h -- and 64-bit integer division has no RV32 instruction either).

#include <math.h>
#include <stdbool.h>
#include <string.h>

#include "cpu.h"
#include "trap.h"

void trap___adddf3(cpu_t *cpu) {
    ret_f64(cpu, arg_f64(cpu, 0) + arg_f64(cpu, 2));
}
void trap___subdf3(cpu_t *cpu) {
    ret_f64(cpu, arg_f64(cpu, 0) - arg_f64(cpu, 2));
}
void trap___muldf3(cpu_t *cpu) {
    ret_f64(cpu, arg_f64(cpu, 0) * arg_f64(cpu, 2));
}
void trap___divdf3(cpu_t *cpu) {
    ret_f64(cpu, arg_f64(cpu, 0) / arg_f64(cpu, 2));
}
void trap___gedf2(cpu_t *cpu) {
    // libgcc convention: result >= 0 means "greater than or equal".
    ret_u32(cpu, arg_f64(cpu, 0) >= arg_f64(cpu, 2) ? 1 : -1);
}
void trap___gtdf2(cpu_t *cpu) {
    ret_u32(cpu, arg_f64(cpu, 0) > arg_f64(cpu, 2) ? 1 : -1);
}
void trap___floatsidf(cpu_t *cpu) {
    ret_f64(cpu, (double)arg_i32(cpu, 0));
}
void trap___floatdisf(cpu_t *cpu) {
    ret_f32(cpu, (float)arg_i64(cpu, 0));
}
void trap___floatunsidf(cpu_t *cpu) {
    ret_f64(cpu, (double)arg_u32(cpu, 0));
}
void trap___fixdfdi(cpu_t *cpu) {
    ret_u64(cpu, (uint64_t)(int64_t)arg_f64(cpu, 0));
}
void trap___fixunsdfsi(cpu_t *cpu) {
    ret_u32(cpu, (uint32_t)arg_f64(cpu, 0));
}
void trap___extendsfdf2(cpu_t *cpu) {
    ret_f64(cpu, (double)arg_f32(cpu, 0));
}
void trap___issignalingf(cpu_t *cpu) {
    float    f = arg_f32(cpu, 0);
    uint32_t bits;
    memcpy(&bits, &f, sizeof(bits));
    // IEEE 754: a NaN with the top mantissa bit (the "quiet" bit) clear is
    // signaling.
    bool is_signaling = isnan(f) && !(bits & 0x00400000u);
    ret_u32(cpu, (uint32_t)is_signaling);
}

static int cmp3(double a, double b) {
    return a < b ? -1 : (a > b ? 1 : 0);
}
void trap___eqdf2(cpu_t *cpu) {
    ret_u32(cpu, (uint32_t)cmp3(arg_f64(cpu, 0), arg_f64(cpu, 2)));
}
void trap___nedf2(cpu_t *cpu) {
    ret_u32(cpu, (uint32_t)cmp3(arg_f64(cpu, 0), arg_f64(cpu, 2)));
}
void trap___ltdf2(cpu_t *cpu) {
    ret_u32(cpu, (uint32_t)cmp3(arg_f64(cpu, 0), arg_f64(cpu, 2)));
}
void trap___ledf2(cpu_t *cpu) {
    ret_u32(cpu, (uint32_t)cmp3(arg_f64(cpu, 0), arg_f64(cpu, 2)));
}

void trap___fixdfsi(cpu_t *cpu) {
    ret_u32(cpu, (uint32_t)(int32_t)arg_f64(cpu, 0));
}
void trap___floatdidf(cpu_t *cpu) {
    ret_f64(cpu, (double)arg_i64(cpu, 0));
}
void trap___truncdfsf2(cpu_t *cpu) {
    ret_f32(cpu, (float)arg_f64(cpu, 0));
}

void trap___umoddi3(cpu_t *cpu) {
    ret_u64(cpu, arg_u64(cpu, 0) % arg_u64(cpu, 2));
}

void trap___divdi3(cpu_t *cpu) {
    ret_u64(cpu, (uint64_t)(arg_i64(cpu, 0) / arg_i64(cpu, 2)));
}
void trap___moddi3(cpu_t *cpu) {
    ret_u64(cpu, (uint64_t)(arg_i64(cpu, 0) % arg_i64(cpu, 2)));
}
void trap___udivdi3(cpu_t *cpu) {
    ret_u64(cpu, arg_u64(cpu, 0) / arg_u64(cpu, 2));
}
