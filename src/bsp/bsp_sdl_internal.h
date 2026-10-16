#pragma once

// Private glue between the bsp_*.c compat implementations. Not part of the
// public include/bsp/*.h API: these functions exist only to let bsp_device.c
// drive setup of the SDL window (owned by bsp_display.c) and the input event
// queue (owned by bsp_input.c) without those files reaching into each
// other's statics directly.

#include <stddef.h>

#include "esp_err.h"
#include "bsp/display.h"

// The Tanmatsu's physical MIPI-DSI panel is a 480x800 *portrait* panel,
// mounted rotated 90 degrees so apps see an 800x480 *landscape* logical
// view (bsp_display_get_default_rotation() reports this, and apps
// pax_buf_set_orientation() their framebuffers accordingly -- see
// graceloader.h: "PAX_O_UPRIGHT and PAX_O_ROT_CW (the Tanmatsu's panel)").
// bsp_display_blit() receives pixels in the RAW (480x800) layout PAX
// produces once that orientation is applied, so the window/texture here
// have to be the raw panel dimensions too, with the rotation to a
// human-viewable landscape window handled separately at present time (see
// bsp_display_blit() in bsp_display.c).
#define BSP_SDL_DEFAULT_WIDTH  480
#define BSP_SDL_DEFAULT_HEIGHT 800

// Implemented in bsp_display.c
// `requested_color_format` is the app's bsp_display_configuration_t request,
// passed through unchanged from bsp_device_initialize(); bsp_display.c
// negotiates it the same way the real ST7701 driver does and the window's
// texture is created in the resulting format.
esp_err_t bsp_display_internal_create_window(size_t width, size_t height,
                                             bsp_display_color_format_t requested_color_format);
// Implemented in bsp_display.c; used by esp_lcd_shim.c to size the
// framebuffers it hands out via esp_lcd_dpi_panel_get_frame_buffer().
void      bsp_display_internal_get_size(size_t* out_width, size_t* out_height);
// Implemented in bsp_display.c; the bytes per pixel of the *negotiated*
// display format, which is what both bsp_display_blit() and any framebuffer
// this emulator allocates on a guest app's behalf have to be sized with.
size_t    bsp_display_internal_get_bytes_per_pixel(void);

// Implemented in bsp_input.c
esp_err_t bsp_input_internal_init(void);
