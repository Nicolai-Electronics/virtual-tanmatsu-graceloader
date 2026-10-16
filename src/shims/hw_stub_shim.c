// Stubs for ESP32-P4-only hardware this desktop backend has no equivalent
// for: the hardware H.264 encoder (screen recording / streaming features)
// and USB Serial/JTAG (console-over-USB). Every one fails/no-ops in a way
// real apps already have to tolerate (these peripherals are absent or
// disabled on several of the other boards this same codebase targets), so
// callers fall back gracefully rather than crash.

#include "cpu.h"
#include "esp_err.h"
#include "trap.h"

// ESP_H264_ERR_FAIL (see esp_h264_types.h) -- these traps don't link
// against the real h264 headers (irrelevant off real hardware), so the
// numeric value is inlined here instead.
#define ESP_H264_ERR_FAIL (-1)

void trap_esp_h264_enc_hw_new(cpu_t *cpu) {
    void **out_enc = (void **)arg_ptr(cpu, 1);
    if (out_enc != NULL) {
        *out_enc = NULL;
    }
    ret_u32(cpu, (uint32_t)ESP_H264_ERR_FAIL);
}
void trap_esp_h264_enc_open(cpu_t *cpu) {
    (void)cpu;
    ret_u32(cpu, (uint32_t)ESP_H264_ERR_FAIL);
}
void trap_esp_h264_enc_process(cpu_t *cpu) {
    (void)cpu;
    ret_u32(cpu, (uint32_t)ESP_H264_ERR_FAIL);
}
void trap_esp_h264_enc_close(cpu_t *cpu) {
    (void)cpu;
    ret_u32(cpu, (uint32_t)ESP_H264_ERR_FAIL);
}
void trap_esp_h264_enc_del(cpu_t *cpu) {
    (void)cpu;
    ret_u32(cpu, (uint32_t)ESP_H264_ERR_FAIL);
}

void trap_usb_new_phy(cpu_t *cpu) {
    void **out_handle = (void **)arg_ptr(cpu, 1);
    if (out_handle != NULL) {
        *out_handle = NULL;
    }
    ret_u32(cpu, ESP_ERR_NOT_SUPPORTED);
}
void trap_usb_del_phy(cpu_t *cpu) {
    (void)cpu;
    ret_u32(cpu, ESP_OK);
}
void trap_usb_serial_jtag_driver_install(cpu_t *cpu) {
    (void)cpu;
    ret_u32(cpu, ESP_ERR_NOT_SUPPORTED);
}
void trap_usb_serial_jtag_read_bytes(cpu_t *cpu) {
    (void)cpu;
    ret_u32(cpu, 0);  // 0 bytes read
}
void trap_usb_serial_jtag_wait_tx_done(cpu_t *cpu) {
    (void)cpu;
    ret_u32(cpu, ESP_OK);
}
