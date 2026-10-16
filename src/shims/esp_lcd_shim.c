// esp_lcd_* passthroughs for apps that drive the display through the real
// double-buffered DPI panel API (esp_lcd_dpi_panel_get_frame_buffer() +
// esp_lcd_panel_draw_bitmap()) instead of bsp_display_blit() directly --
// see graceloader.h's big comment on page flipping for the real-hardware
// protocol this stands in for.
//
// This desktop backend only has one SDL texture, so "page flipping" here
// just means: each of the app's `fb_num` framebuffers is a real,
// independent guest-memory buffer (so the app's double/triple-buffering
// logic has somewhere real to write), and draw_bitmap() immediately blits
// whichever one was selected straight to the window, then synchronously
// invokes the app's registered on_refresh_done callback (if any) so the
// app's own vsync-wait semaphore is given right away instead of stalling
// on the 100ms "driver isn't refreshing" timeout real apps use as a
// fallback for exactly this no-callback case.

#include <string.h>

#include "bsp/display.h"
#include "cpu.h"
#include "esp_err.h"
#include "esp_lcd_mipi_dsi.h"
#include "guest_mem.h"
#include "trap.h"

extern void bsp_display_internal_get_size(size_t *out_width, size_t *out_height);

static uint32_t g_on_refresh_done_fn  = 0;  // guest function pointer, or 0 if unregistered
static uint32_t g_on_refresh_done_ctx = 0;  // guest pointer, passed through as user_ctx

void trap_esp_lcd_dpi_panel_get_frame_buffer(cpu_t *cpu) {
    // (panel, fb_num, &fb0, &fb1, ...) -- a variadic sink for up to 3
    // framebuffer out-pointers, matching every real call site in practice.
    uint32_t fb_num = arg_u32(cpu, 1);

    size_t width, height;
    bsp_display_internal_get_size(&width, &height);
    size_t const fb_bytes = width * height * 2;  // RGB565, see bsp_display.c

    for (uint32_t i = 0; i < fb_num && i < 3; i++) {
        void **out = (void **)arg_ptr(cpu, 2 + (int)i);
        if (out != NULL) {
            *out = guest_mem_calloc(fb_bytes, 8);
        }
    }
    ret_u32(cpu, ESP_OK);
}

void trap_esp_lcd_panel_draw_bitmap(cpu_t *cpu) {
    // arg 0 (panel) ignored: this backend has exactly one display.
    int         x_start = arg_i32(cpu, 1);
    int         y_start = arg_i32(cpu, 2);
    int         x_end    = arg_i32(cpu, 3);
    int         y_end    = arg_i32(cpu, 4);
    void const *data     = arg_ptr(cpu, 5);

    esp_err_t err = bsp_display_blit(
        (size_t)x_start, (size_t)y_start, (size_t)(x_end - x_start), (size_t)(y_end - y_start), data
    );

    if (g_on_refresh_done_fn != 0) {
        uint32_t args[3] = {arg_u32(cpu, 0), 0 /* edata: an empty struct in the real header */, g_on_refresh_done_ctx};
        cpu_call_guest(cpu, g_on_refresh_done_fn, 3, args);
    }

    ret_u32(cpu, (uint32_t)err);
}

void trap_graceloader_display_register_callbacks(cpu_t *cpu) {
    // struct esp_lcd_dpi_panel_event_callbacks_t { on_color_trans_done; on_refresh_done; }
    // (see esp_lcd_mipi_dsi.h) -- only on_refresh_done is used by any app
    // this emulator has seen.
    uint32_t const *cbs = (uint32_t const *)arg_ptr(cpu, 0);
    g_on_refresh_done_fn  = cbs ? cbs[1] : 0;
    g_on_refresh_done_ctx = arg_u32(cpu, 1);
    ret_u32(cpu, ESP_OK);
}
