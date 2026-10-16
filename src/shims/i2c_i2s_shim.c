// Raw I2C/I2S driver passthroughs. There's no real I2C bus on the host, so
// the i2c_master_* calls always report ESP_ERR_NOT_SUPPORTED with a NULL
// handle and bsp_i2c_primary_bus_get_handle() (see bsp_shim.c) agrees.
//
// I2S is different: it's the audio output apps actually need, so these
// route to a real SDL2 audio device owned by bsp_audio.c (see
// bsp_audio_get_i2s_handle() in bsp_shim.c for how an app gets a handle in
// the first place).

#include <stddef.h>

#include "cpu.h"
#include "esp_err.h"
#include "trap.h"

// Implemented in bsp_audio.c; not part of the public include/bsp/audio.h
// API (same "internal, cross-file glue" pattern as bsp_sdl_internal.h's
// bsp_display_internal_*(), just declared locally here since this file
// isn't under src/bsp/ and so isn't on that header's include path).
esp_err_t bsp_audio_internal_channel_enable(void);
esp_err_t bsp_audio_internal_channel_disable(void);
esp_err_t bsp_audio_internal_channel_write(void const *data, size_t size, size_t *out_written);

void trap_i2c_master_probe(cpu_t *cpu) {
    (void)cpu;
    ret_u32(cpu, ESP_ERR_NOT_SUPPORTED);
}
void trap_i2c_master_bus_add_device(cpu_t *cpu) {
    void **out = (void **)arg_ptr(cpu, 2);
    if (out != NULL) {
        *out = NULL;
    }
    ret_u32(cpu, ESP_ERR_NOT_SUPPORTED);
}
void trap_i2c_master_bus_rm_device(cpu_t *cpu) {
    (void)cpu;
    ret_u32(cpu, ESP_OK);
}
void trap_i2c_master_transmit(cpu_t *cpu) {
    (void)cpu;
    ret_u32(cpu, ESP_ERR_NOT_SUPPORTED);
}

void trap_i2s_channel_enable(cpu_t *cpu) {
    // arg 0 (handle) ignored: this backend has exactly one I2S channel.
    ret_u32(cpu, (uint32_t)bsp_audio_internal_channel_enable());
}
void trap_i2s_channel_disable(cpu_t *cpu) {
    ret_u32(cpu, (uint32_t)bsp_audio_internal_channel_disable());
}
void trap_i2s_channel_write(cpu_t *cpu) {
    // arg 4 (timeout_ms) ignored: every caller in this emulator's app set
    // passes portMAX_DELAY, and bsp_audio_internal_channel_write() always
    // blocks (via real backpressure against the SDL audio queue) rather
    // than returning early, which is what that argument is asking for.
    void const *src           = arg_ptr(cpu, 1);
    size_t      size          = arg_u32(cpu, 2);
    size_t     *bytes_written = (size_t *)arg_ptr(cpu, 3);
    ret_u32(cpu, (uint32_t)bsp_audio_internal_channel_write(src, size, bytes_written));
}
