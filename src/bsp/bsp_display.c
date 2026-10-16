#include "bsp/display.h"

#include <SDL2/SDL.h>
#include <pthread.h>
#include <unistd.h>

#include "bsp_sdl_internal.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/semphr.h"

static char const TAG[] = "bsp_display";

static SDL_Window*   s_window   = NULL;
static SDL_Renderer* s_renderer = NULL;
static SDL_Texture*  s_texture  = NULL;
static size_t         s_width   = 0;
static size_t         s_height  = 0;
static bool           s_ready   = false;

static uint8_t                s_backlight_percentage = 100;
static bsp_display_te_mode_t  s_te_mode               = BSP_DISPLAY_TE_DISABLED;

esp_err_t bsp_display_internal_create_window(size_t width, size_t height) {
    if (s_ready) {
        return ESP_OK;
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        ESP_LOGE(TAG, "SDL_Init failed: %s", SDL_GetError());
        return ESP_FAIL;
    }

    // The window is sized for how a human views the device (landscape,
    // width/height swapped from the raw panel) -- the texture below stays
    // at the raw panel's own dimensions; bsp_display_blit() rotates one
    // into the other at present time.
    s_window = SDL_CreateWindow(
        "VirtuGrace Tanmatsu emulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, (int)height, (int)width,
        SDL_WINDOW_SHOWN
    );
    if (s_window == NULL) {
        ESP_LOGE(TAG, "SDL_CreateWindow failed: %s", SDL_GetError());
        return ESP_FAIL;
    }

    s_renderer = SDL_CreateRenderer(s_window, -1, SDL_RENDERER_ACCELERATED);
    if (s_renderer == NULL) {
        ESP_LOGE(TAG, "SDL_CreateRenderer failed: %s", SDL_GetError());
        return ESP_FAIL;
    }

    // RGB565, not ARGB8888: real Tanmatsu hardware (MIPI-DSI panel) is
    // 16-bit RGB565, and apps request/assume that format explicitly (e.g.
    // bsp_configuration_t.display.requested_color_format +
    // bsp_display_get_parameters()) -- some even bypass PAX for hot paths
    // and poke packed 565 pixels directly (see se_direct565.h/.c in the
    // synthengine3D-based apps). Reporting a different format back doesn't
    // just look wrong, it silently mismatches their own buffer's pixel
    // stride/size against what they compute by hand, corrupting memory.
    s_texture = SDL_CreateTexture(s_renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, (int)width,
                                  (int)height);
    if (s_texture == NULL) {
        ESP_LOGE(TAG, "SDL_CreateTexture failed: %s", SDL_GetError());
        return ESP_FAIL;
    }

    SDL_StartTextInput();

    s_width  = width;
    s_height = height;
    s_ready  = true;

    return ESP_OK;
}

esp_err_t bsp_display_get_parameters(size_t* h_res, size_t* v_res, bsp_display_color_format_t* color_fmt,
                                     bsp_display_endianness_t* data_endian) {
    if (!s_ready) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (h_res != NULL) {
        *h_res = s_width;
    }
    if (v_res != NULL) {
        *v_res = s_height;
    }
    if (color_fmt != NULL) {
        *color_fmt = BSP_DISPLAY_COLOR_FORMAT_16_565RGB;
    }
    if (data_endian != NULL) {
        *data_endian = BSP_DISPLAY_ENDIAN_LITTLE;
    }

    return ESP_OK;
}

// A real (non-NULL) value is enough: apps only ever hand this back to
// esp_lcd_*() calls (also emulated, see esp_lcd_shim.c), never dereference
// it directly. `esp_lcd_panel_handle_t` is an opaque/incomplete struct
// pointer, so this stands in for an instance without needing its real
// (and here, irrelevant) layout.
static int s_dummy_panel;

esp_err_t bsp_display_get_panel(esp_lcd_panel_handle_t* panel) {
    if (panel == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ready) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    *panel = (esp_lcd_panel_handle_t)&s_dummy_panel;
    return ESP_OK;
}

void bsp_display_internal_get_size(size_t* out_width, size_t* out_height) {
    *out_width  = s_width;
    *out_height = s_height;
}

esp_err_t bsp_display_get_panel_io(esp_lcd_panel_io_handle_t* io) {
    (void)io;
    return ESP_ERR_NOT_SUPPORTED;
}

bsp_display_rotation_t bsp_display_get_default_rotation() {
    // Matches the real panel: see the big comment on BSP_SDL_DEFAULT_WIDTH
    // in bsp_sdl_internal.h. This specific enum value (not just the
    // physical rotation) matters: tanmatsu-synthracer-grace/main/magicnumbers.h
    // and tanmatsu-synthminer-grace/synthengine3D/src/se_run.c both switch on
    // this value to pick a pax_buf orientation, and only map
    // BSP_DISPLAY_ROTATION_270 to PAX_O_ROT_CW -- the orientation their
    // se_direct565.h hand-rolled raw-pixel math is hardcoded against
    // (independent of whatever pax_buf_set_orientation() is actually called
    // with). Returning ROTATION_90 here made those apps configure their pax
    // buffers as PAX_O_ROT_CCW instead, which only broke direction-sensitive
    // PAX-drawn shapes (e.g. a progress bar's growing fill) while leaving
    // direct565 text and symmetric content looking fine by coincidence.
    return BSP_DISPLAY_ROTATION_270;
}

esp_err_t bsp_display_get_backlight_brightness(uint8_t* out_percentage) {
    if (out_percentage == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_percentage = s_backlight_percentage;
    return ESP_OK;
}

esp_err_t bsp_display_set_backlight_brightness(uint8_t percentage) {
    s_backlight_percentage = percentage;
    return ESP_OK;
}

esp_err_t bsp_display_set_tearing_effect_mode(bsp_display_te_mode_t mode) {
    s_te_mode = mode;
    return ESP_OK;
}

esp_err_t bsp_display_get_tearing_effect_mode(bsp_display_te_mode_t* mode) {
    if (mode == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *mode = s_te_mode;
    return ESP_OK;
}

// The real panel's tearing-effect GPIO pulses once per physical refresh
// (nothing to do with bsp_display_set_tearing_effect_mode()'s DISABLED/
// SINGLE/CONTINUOUS setting, which only controls whether the *driver*
// reads it to time its own blit -- the signal itself is just the panel's
// own fixed timing). Apps that pace their render loop by waiting on this
// semaphore (see tanmatsu-synthracer-grace/synthengine3D/src/se_run.c's
// se_present()) rely on it to NOT run faster than the display can show --
// returning ESP_ERR_NOT_SUPPORTED here, as this used to, left such apps
// with literally nothing capping their frame rate (vTaskDelay-based
// pacing is a separate mechanism some apps also use, this is not that),
// so they ran, and so anything tied to "one tick/frame" in their own game
// logic advanced, as fast as this emulator's interpreter could step
// guest instructions -- not at anything resembling real time. A free-
// running host thread "pulsing" this semaphore at a plausible panel
// refresh rate (60 Hz; the real ST7701 panel's exact DPI timing isn't
// available to this emulator, but any value in that neighbourhood turns
// "unbounded" back into "roughly real time", which is what matters here)
// fixes that for every app using this mechanism, not just one.
#define TE_PULSE_HZ      60
#define TE_PULSE_PERIOD_US (1000000 / TE_PULSE_HZ)

static SemaphoreHandle_t s_te_sem           = NULL;
static pthread_t         s_te_thread;
static bool              s_te_thread_started = false;

static void* te_thread_main(void* arg) {
    (void)arg;
    for (;;) {
        usleep(TE_PULSE_PERIOD_US);
        xSemaphoreGive(s_te_sem);
    }
    return NULL;
}

esp_err_t bsp_display_get_tearing_effect_semaphore(SemaphoreHandle_t* semaphore) {
    if (semaphore == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_te_sem == NULL) {
        s_te_sem = xSemaphoreCreateBinary();
        if (s_te_sem == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (!s_te_thread_started) {
        pthread_create(&s_te_thread, NULL, te_thread_main, NULL);
        s_te_thread_started = true;
    }
    *semaphore = s_te_sem;
    return ESP_OK;
}

esp_err_t bsp_display_blit(size_t x, size_t y, size_t width, size_t height, const void* buffer) {
    if (!s_ready) {
        return ESP_FAIL;
    }
    if (buffer == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    SDL_Rect rect = {(int)x, (int)y, (int)width, (int)height};
    if (SDL_UpdateTexture(s_texture, &rect, buffer, (int)(width * 2)) != 0) {
        ESP_LOGE(TAG, "SDL_UpdateTexture failed: %s", SDL_GetError());
        return ESP_FAIL;
    }

    // The texture holds the raw (480x800 portrait) panel layout; rotate it
    // 90 degrees to the window's landscape (800x480) viewing orientation --
    // see the comment on BSP_SDL_DEFAULT_WIDTH in bsp_sdl_internal.h.
    // SDL_RenderCopyEx rotates a dstrect around its own center, so the
    // unrotated rect has to be the *texture's* own w/h, centered in the
    // window (which is exactly big enough for the post-rotation result
    // since its dimensions are the texture's swapped).
    int win_w, win_h;
    SDL_GetRendererOutputSize(s_renderer, &win_w, &win_h);
    SDL_Rect dst = {(win_w - (int)s_width) / 2, (win_h - (int)s_height) / 2, (int)s_width, (int)s_height};

    SDL_RenderClear(s_renderer);
    SDL_RenderCopyEx(s_renderer, s_texture, NULL, &dst, -90.0, NULL, SDL_FLIP_NONE);
    SDL_RenderPresent(s_renderer);

    return ESP_OK;
}
