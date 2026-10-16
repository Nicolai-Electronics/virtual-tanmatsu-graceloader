// graceloader's gl_input_* merged-input API: per the real project's own
// README, these are drop-in replacements for the equivalent bsp_input_*
// calls (gl_input merges the native keyboard with USB HID; there's no USB
// HID on this host, so it's a pure passthrough to bsp_input_*, see
// emulator/src/bsp/bsp_input.c).

#include "bsp/input.h"
#include "cpu.h"
#include "trap.h"

void trap_gl_input_get_queue(cpu_t *cpu) {
    ret_u32(cpu, (uint32_t)bsp_input_get_queue((QueueHandle_t *)arg_ptr(cpu, 0)));
}
void trap_gl_input_read_navigation_key(cpu_t *cpu) {
    ret_u32(
        cpu, (uint32_t)bsp_input_read_navigation_key((bsp_input_navigation_key_t)arg_i32(cpu, 0),
                                                      (bool *)arg_ptr(cpu, 1))
    );
}
void trap_gl_input_read_scancode(cpu_t *cpu) {
    ret_u32(
        cpu, (uint32_t)bsp_input_read_scancode((bsp_input_scancode_t)arg_i32(cpu, 0), (bool *)arg_ptr(cpu, 1))
    );
}
void trap_gl_input_read_action(cpu_t *cpu) {
    ret_u32(
        cpu, (uint32_t)bsp_input_read_action((bsp_input_action_type_t)arg_i32(cpu, 0), (bool *)arg_ptr(cpu, 1))
    );
}
