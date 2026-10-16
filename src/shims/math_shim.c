// Single-precision libm passthroughs.

#include <math.h>

#include "cpu.h"
#include "trap.h"

void trap_sinf(cpu_t *cpu) {
    ret_f32(cpu, sinf(arg_f32(cpu, 0)));
}
void trap_cosf(cpu_t *cpu) {
    ret_f32(cpu, cosf(arg_f32(cpu, 0)));
}
void trap_powf(cpu_t *cpu) {
    ret_f32(cpu, powf(arg_f32(cpu, 0), arg_f32(cpu, 1)));
}
void trap_floorf(cpu_t *cpu) {
    ret_f32(cpu, floorf(arg_f32(cpu, 0)));
}
void trap_ceilf(cpu_t *cpu) {
    ret_f32(cpu, ceilf(arg_f32(cpu, 0)));
}
void trap_fmodf(cpu_t *cpu) {
    ret_f32(cpu, fmodf(arg_f32(cpu, 0), arg_f32(cpu, 1)));
}
void trap_lroundf(cpu_t *cpu) {
    ret_u32(cpu, (uint32_t)lroundf(arg_f32(cpu, 0)));
}
void trap_lrintf(cpu_t *cpu) {
    ret_u32(cpu, (uint32_t)lrintf(arg_f32(cpu, 0)));
}
void trap_atan2f(cpu_t *cpu) {
    ret_f32(cpu, atan2f(arg_f32(cpu, 0), arg_f32(cpu, 1)));
}
void trap_asinf(cpu_t *cpu) {
    ret_f32(cpu, asinf(arg_f32(cpu, 0)));
}
void trap_atanf(cpu_t *cpu) {
    ret_f32(cpu, atanf(arg_f32(cpu, 0)));
}
void trap_expf(cpu_t *cpu) {
    ret_f32(cpu, expf(arg_f32(cpu, 0)));
}
void trap_hypotf(cpu_t *cpu) {
    ret_f32(cpu, hypotf(arg_f32(cpu, 0), arg_f32(cpu, 1)));
}
void trap_roundf(cpu_t *cpu) {
    ret_f32(cpu, roundf(arg_f32(cpu, 0)));
}

// Double-precision variants: double args/returns use the soft-call
// convention (register pairs), same as the libgcc helpers in
// softfloat_shim.c -- see arg_f64()/ret_f64() in trap.h.
void trap_sin(cpu_t *cpu) {
    ret_f64(cpu, sin(arg_f64(cpu, 0)));
}
void trap_cos(cpu_t *cpu) {
    ret_f64(cpu, cos(arg_f64(cpu, 0)));
}
void trap_sqrt(cpu_t *cpu) {
    ret_f64(cpu, sqrt(arg_f64(cpu, 0)));
}
void trap_floor(cpu_t *cpu) {
    ret_f64(cpu, floor(arg_f64(cpu, 0)));
}
void trap_ceil(cpu_t *cpu) {
    ret_f64(cpu, ceil(arg_f64(cpu, 0)));
}
void trap_fmod(cpu_t *cpu) {
    ret_f64(cpu, fmod(arg_f64(cpu, 0), arg_f64(cpu, 2)));
}
void trap_log(cpu_t *cpu) {
    ret_f64(cpu, log(arg_f64(cpu, 0)));
}
void trap_exp2(cpu_t *cpu) {
    ret_f64(cpu, exp2(arg_f64(cpu, 0)));
}
void trap_atan2(cpu_t *cpu) {
    ret_f64(cpu, atan2(arg_f64(cpu, 0), arg_f64(cpu, 2)));
}
