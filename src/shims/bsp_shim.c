// Board-support-package passthroughs: call straight into the vendored SDL
// bsp_*.c implementations (emulator/src/bsp/), which own the window, input
// queue, and the various "no such hardware here" stubs.

#include <stdio.h>
#include <string.h>

#include "bsp/audio.h"
#include "bsp/power.h"
#include "bsp/device.h"
#include "bsp/display.h"
#include "bsp/i2c.h"
#include "bsp/input.h"
#include "bsp/led.h"
#include "bsp/orientation.h"
#include "cpu.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "trap.h"

void trap_bsp_device_initialize(cpu_t *cpu) {
    ret_u32(cpu, (uint32_t)bsp_device_initialize((bsp_configuration_t const *)arg_ptr(cpu, 0)));
}
void trap_bsp_device_restart_to_launcher(cpu_t *cpu) {
    (void)cpu;
    bsp_device_restart_to_launcher();
}

void trap_bsp_display_blit(cpu_t *cpu) {
    ret_u32(
        cpu, (uint32_t)bsp_display_blit(arg_u32(cpu, 0), arg_u32(cpu, 1), arg_u32(cpu, 2), arg_u32(cpu, 3),
                                        arg_ptr(cpu, 4))
    );
}
void trap_bsp_display_get_default_rotation(cpu_t *cpu) {
    ret_u32(cpu, (uint32_t)bsp_display_get_default_rotation());
}
void trap_bsp_display_get_parameters(cpu_t *cpu) {
    ret_u32(
        cpu, (uint32_t)bsp_display_get_parameters(
                 (size_t *)arg_ptr(cpu, 0), (size_t *)arg_ptr(cpu, 1),
                 (bsp_display_color_format_t *)arg_ptr(cpu, 2), (bsp_display_endianness_t *)arg_ptr(cpu, 3)
             )
    );
}
void trap_bsp_display_get_tearing_effect_semaphore(cpu_t *cpu) {
    ret_u32(cpu, (uint32_t)bsp_display_get_tearing_effect_semaphore((SemaphoreHandle_t *)arg_ptr(cpu, 0)));
}
void trap_bsp_display_set_backlight_brightness(cpu_t *cpu) {
    ret_u32(cpu, (uint32_t)bsp_display_set_backlight_brightness((uint8_t)arg_u32(cpu, 0)));
}
void trap_bsp_display_set_tearing_effect_mode(cpu_t *cpu) {
    ret_u32(cpu, (uint32_t)bsp_display_set_tearing_effect_mode((bsp_display_te_mode_t)arg_i32(cpu, 0)));
}
void trap_bsp_display_get_panel(cpu_t *cpu) {
    ret_u32(cpu, (uint32_t)bsp_display_get_panel((esp_lcd_panel_handle_t *)arg_ptr(cpu, 0)));
}

void trap_bsp_input_set_backlight_brightness(cpu_t *cpu) {
    ret_u32(cpu, (uint32_t)bsp_input_set_backlight_brightness((uint8_t)arg_u32(cpu, 0)));
}

void trap_bsp_led_set_brightness(cpu_t *cpu) {
    ret_u32(cpu, (uint32_t)bsp_led_set_brightness((uint8_t)arg_u32(cpu, 0)));
}
void trap_bsp_led_clear(cpu_t *cpu) {
    (void)cpu;
    ret_u32(cpu, (uint32_t)bsp_led_clear());
}
void trap_bsp_led_get_count(cpu_t *cpu) {
    ret_u32(cpu, (uint32_t)bsp_led_get_count((uint32_t *)arg_ptr(cpu, 0)));
}
void trap_bsp_led_send(cpu_t *cpu) {
    (void)cpu;
    ret_u32(cpu, (uint32_t)bsp_led_send());
}
void trap_bsp_led_set_mode(cpu_t *cpu) {
    ret_u32(cpu, (uint32_t)bsp_led_set_mode(arg_u32(cpu, 0) != 0));
}
void trap_bsp_led_set_pixel(cpu_t *cpu) {
    ret_u32(cpu, (uint32_t)bsp_led_set_pixel(arg_u32(cpu, 0), arg_u32(cpu, 1)));
}

void trap_bsp_orientation_enable_accelerometer(cpu_t *cpu) {
    ret_u32(cpu, (uint32_t)bsp_orientation_enable_accelerometer());
}
void trap_bsp_orientation_enable_gyroscope(cpu_t *cpu) {
    (void)cpu;
    ret_u32(cpu, (uint32_t)bsp_orientation_enable_gyroscope());
}
void trap_bsp_orientation_get(cpu_t *cpu) {
    ret_u32(
        cpu, (uint32_t)bsp_orientation_get(
                 (bool *)arg_ptr(cpu, 0), (bool *)arg_ptr(cpu, 1), (float *)arg_ptr(cpu, 2), (float *)arg_ptr(cpu, 3),
                 (float *)arg_ptr(cpu, 4), (float *)arg_ptr(cpu, 5), (float *)arg_ptr(cpu, 6),
                 (float *)arg_ptr(cpu, 7)
             )
    );
}

void trap_bsp_audio_get_i2s_handle(cpu_t *cpu) {
    ret_u32(cpu, (uint32_t)bsp_audio_get_i2s_handle((i2s_chan_handle_t *)arg_ptr(cpu, 0)));
}
void trap_bsp_audio_set_amplifier(cpu_t *cpu) {
    ret_u32(cpu, (uint32_t)bsp_audio_set_amplifier(arg_u32(cpu, 0) != 0));
}
void trap_bsp_audio_set_rate(cpu_t *cpu) {
    ret_u32(cpu, (uint32_t)bsp_audio_set_rate(arg_u32(cpu, 0)));
}
void trap_bsp_audio_set_volume(cpu_t *cpu) {
    ret_u32(cpu, (uint32_t)bsp_audio_set_volume(arg_f32(cpu, 0)));
}

void trap_bsp_i2c_primary_bus_claim(cpu_t *cpu) {
    ret_u32(cpu, (uint32_t)bsp_i2c_primary_bus_claim());
}
void trap_bsp_i2c_primary_bus_release(cpu_t *cpu) {
    ret_u32(cpu, (uint32_t)bsp_i2c_primary_bus_release());
}
void trap_bsp_power_set_radio_state(cpu_t *cpu) {
    ret_u32(cpu, (uint32_t)bsp_power_set_radio_state((bsp_radio_state_t)arg_i32(cpu, 0)));
}

void trap_bsp_i2c_primary_bus_get_handle(cpu_t *cpu) {
    ret_u32(cpu, (uint32_t)bsp_i2c_primary_bus_get_handle((i2c_master_bus_handle_t *)arg_ptr(cpu, 0)));
}

// Not part of the badge-bsp version this emulator vendors (a newer board
// feature, presumably an add-on-connector I2C bus); there's no real hardware
// to back it on the host, so this is a plain stub.
void trap_bsp_catt_set_i2c_enabled(cpu_t *cpu) {
    (void)arg_u32(cpu, 0);
    ret_u32(cpu, ESP_OK);
}
void trap_bsp_catt_i2c_bus_get_handle(cpu_t *cpu) {
    i2c_master_bus_handle_t *out = (i2c_master_bus_handle_t *)arg_ptr(cpu, 0);
    if (out != NULL) {
        *out = NULL;
    }
    ret_u32(cpu, ESP_ERR_NOT_SUPPORTED);
}

// Set once at startup by main.c (see graceloader.h on the real target).
static char g_install_basepath[1024] = "";
void        bsp_shim_set_install_basepath(char const *path) {
    snprintf(g_install_basepath, sizeof(g_install_basepath), "%s", path);
}
void trap_graceloader_get_install_basepath(cpu_t *cpu) {
    ret_ptr(cpu, g_install_basepath);
}
