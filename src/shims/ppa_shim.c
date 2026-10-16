// PPA (ESP32-P4 hardware 2D blitter) emulation.
//
// synthengine3D's se_ppa.c wrapper (see tanmatsu-synthracer-grace and
// tanmatsu-synthminer-grace) documents that a failed ppa_register_client()
// degrades to "no backdrop, never a crash" -- by design, with NO software
// fallback for the artwork it draws (sun/mountains/sky/floor fill in
// backdrop.c). Reporting PPA as unavailable, as this file used to, is a
// correctly-handled failure as far as the app is concerned, but it means
// that artwork never renders at all. So unlike most ESP32-P4-only
// peripherals in this emulator, PPA needs a real (CPU-side) implementation,
// not just a graceful-failure stub.
//
// Scope: only what se_ppa.c actually builds. Every job it submits uses
// RGB565 buffers, rotation_angle 0, no mirror/rgb_swap/byte_swap, and (for
// blend) a foreground-only colour key. Anything outside that falls back to
// ESP_ERR_INVALID_ARG rather than silently doing the wrong thing.
//
// Hardware PPA ops are asynchronous: a client submits a job, then later an
// ISR invokes the registered on_trans_done callback. This emulation runs
// every op synchronously (there's no real DMA to wait for), then invokes
// the callback immediately, reentrantly, on the calling guest thread's own
// cpu_t/stack (see cpu_call_guest) -- exactly like esp_lcd_shim.c's
// on_refresh_done. se_ppa.c only ever submits in PPA_TRANS_MODE_NON_BLOCKING
// mode, which is what that callback-driven completion path is for.

#include <stdint.h>

#include "cpu.h"
#include "driver/ppa.h"
#include "esp_err.h"
#include "esp_log.h"
#include "guest_mem.h"
#include "trap.h"

static char const TAG[] = "ppa_shim";

// Opaque client handle: guest code never dereferences it, only passes it
// back into ppa_do_*()/ppa_client_register_event_callbacks(), so a
// guest-memory-backed struct (guest pointer == host pointer, see
// guest_mem.h) works fine as the handle value. `on_trans_done` is stored as
// a raw guest address (NOT the real ppa_event_callback_t function-pointer
// type) -- it is RISC-V code, never something this host binary may call
// directly; it only ever gets passed to cpu_call_guest().
typedef struct {
    ppa_operation_t oper_type;
    uint32_t        on_trans_done;  // guest function pointer, or 0
} ppa_client_emu_t;

void trap_ppa_register_client(cpu_t *cpu) {
    ppa_client_config_t const *config = (ppa_client_config_t const *)arg_ptr(cpu, 0);
    void                      **out   = (void **)arg_ptr(cpu, 1);
    if (config == NULL || out == NULL) {
        ret_u32(cpu, ESP_ERR_INVALID_ARG);
        return;
    }
    ppa_client_emu_t *client = (ppa_client_emu_t *)guest_mem_calloc(sizeof(ppa_client_emu_t), 4);
    client->oper_type        = config->oper_type;
    *out                     = client;
    ret_u32(cpu, ESP_OK);
}

void trap_ppa_unregister_client(cpu_t *cpu) {
    (void)cpu;
    ret_u32(cpu, ESP_OK);
}

void trap_ppa_client_register_event_callbacks(cpu_t *cpu) {
    ppa_client_emu_t            *client = (ppa_client_emu_t *)arg_ptr(cpu, 0);
    ppa_event_callbacks_t const *cbs    = (ppa_event_callbacks_t const *)arg_ptr(cpu, 1);
    if (client == NULL || cbs == NULL) {
        ret_u32(cpu, ESP_ERR_INVALID_ARG);
        return;
    }
    client->on_trans_done = (uint32_t)(uintptr_t)cbs->on_trans_done;
    ret_u32(cpu, ESP_OK);
}

// Fire the client's on_trans_done callback synchronously/reentrantly on the
// calling guest thread. No-op if no callback was registered.
static void ppa_fire_done(cpu_t *cpu, ppa_client_emu_t *client, void *user_data) {
    if (client == NULL || client->on_trans_done == 0) {
        return;
    }
    uint32_t args[3] = {
        (uint32_t)(uintptr_t)client,
        0,  // event_data: an empty struct in the real header
        (uint32_t)(uintptr_t)user_data,
    };
    cpu_call_guest(cpu, client->on_trans_done, 3, args);
}

static inline uint16_t pack565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// Expand a 565 pixel to RGB888 by bit replication, matching the comment in
// backdrop.c about PPA's 565->888 expansion ("shift" on some silicon,
// "replicate" on others) -- callers that colour-key against this widen
// their window to cover both, so either choice is safe.
static inline void unpack565(uint16_t v, uint8_t *r, uint8_t *g, uint8_t *b) {
    uint8_t r5 = (v >> 11) & 0x1F, g6 = (v >> 5) & 0x3F, b5 = v & 0x1F;
    *r = (uint8_t)((r5 << 3) | (r5 >> 2));
    *g = (uint8_t)((g6 << 2) | (g6 >> 4));
    *b = (uint8_t)((b5 << 3) | (b5 >> 2));
}

void trap_ppa_do_fill(cpu_t *cpu) {
    ppa_client_emu_t          *client = (ppa_client_emu_t *)arg_ptr(cpu, 0);
    ppa_fill_oper_config_t const *cfg = (ppa_fill_oper_config_t const *)arg_ptr(cpu, 1);
    if (cfg == NULL) {
        ret_u32(cpu, ESP_ERR_INVALID_ARG);
        return;
    }
    if (cfg->out.fill_cm != PPA_FILL_COLOR_MODE_RGB565 || cfg->out.buffer == NULL) {
        ESP_LOGW(TAG, "fill: unsupported color mode %d", (int)cfg->out.fill_cm);
        ret_u32(cpu, ESP_ERR_INVALID_ARG);
        return;
    }

    uint16_t      *buf    = (uint16_t *)cfg->out.buffer;
    uint32_t const pic_w  = cfg->out.pic_w;
    uint32_t const ox     = cfg->out.block_offset_x;
    uint32_t const oy     = cfg->out.block_offset_y;
    uint16_t const packed = pack565(cfg->fill_argb_color.r, cfg->fill_argb_color.g, cfg->fill_argb_color.b);

    for (uint32_t row = 0; row < cfg->fill_block_h; row++) {
        uint16_t *dst = buf + (oy + row) * pic_w + ox;
        for (uint32_t col = 0; col < cfg->fill_block_w; col++) {
            dst[col] = packed;
        }
    }

    ppa_fire_done(cpu, client, cfg->user_data);
    ret_u32(cpu, ESP_OK);
}

void trap_ppa_do_scale_rotate_mirror(cpu_t *cpu) {
    ppa_client_emu_t         *client = (ppa_client_emu_t *)arg_ptr(cpu, 0);
    ppa_srm_oper_config_t const *cfg = (ppa_srm_oper_config_t const *)arg_ptr(cpu, 1);
    if (cfg == NULL) {
        ret_u32(cpu, ESP_ERR_INVALID_ARG);
        return;
    }
    if (cfg->in.srm_cm != PPA_SRM_COLOR_MODE_RGB565 || cfg->out.srm_cm != PPA_SRM_COLOR_MODE_RGB565 ||
        cfg->rotation_angle != PPA_SRM_ROTATION_ANGLE_0 || cfg->mirror_x || cfg->mirror_y || cfg->rgb_swap ||
        cfg->byte_swap || cfg->in.buffer == NULL || cfg->out.buffer == NULL) {
        ESP_LOGW(TAG, "srm: unsupported config (cm/rotation/mirror/swap)");
        ret_u32(cpu, ESP_ERR_INVALID_ARG);
        return;
    }

    uint16_t const *src = (uint16_t const *)cfg->in.buffer;
    uint16_t       *dst = (uint16_t *)cfg->out.buffer;

    int scale_x = (int)(cfg->scale_x + 0.5f);
    int scale_y = (int)(cfg->scale_y + 0.5f);
    if (scale_x < 1) {
        scale_x = 1;
    }
    if (scale_y < 1) {
        scale_y = 1;
    }

    for (uint32_t row = 0; row < cfg->in.block_h; row++) {
        uint16_t const *srow = src + (cfg->in.block_offset_y + row) * cfg->in.pic_w + cfg->in.block_offset_x;
        for (int ry = 0; ry < scale_y; ry++) {
            uint16_t *drow = dst + (cfg->out.block_offset_y + row * (uint32_t)scale_y + (uint32_t)ry) * cfg->out.pic_w +
                             cfg->out.block_offset_x;
            for (uint32_t col = 0; col < cfg->in.block_w; col++) {
                uint16_t v = srow[col];
                for (int rx = 0; rx < scale_x; rx++) {
                    drow[col * (uint32_t)scale_x + (uint32_t)rx] = v;
                }
            }
        }
    }

    ppa_fire_done(cpu, client, cfg->user_data);
    ret_u32(cpu, ESP_OK);
}

static inline bool in_range888(uint8_t r, uint8_t g, uint8_t b, color_pixel_rgb888_data_t lo,
                               color_pixel_rgb888_data_t hi) {
    return r >= lo.r && r <= hi.r && g >= lo.g && g <= hi.g && b >= lo.b && b <= hi.b;
}

void trap_ppa_do_blend(cpu_t *cpu) {
    ppa_client_emu_t           *client = (ppa_client_emu_t *)arg_ptr(cpu, 0);
    ppa_blend_oper_config_t const *cfg = (ppa_blend_oper_config_t const *)arg_ptr(cpu, 1);
    if (cfg == NULL) {
        ret_u32(cpu, ESP_ERR_INVALID_ARG);
        return;
    }
    if (cfg->in_bg.blend_cm != PPA_BLEND_COLOR_MODE_RGB565 || cfg->in_fg.blend_cm != PPA_BLEND_COLOR_MODE_RGB565 ||
        cfg->out.blend_cm != PPA_BLEND_COLOR_MODE_RGB565 || cfg->bg_rgb_swap || cfg->bg_byte_swap ||
        cfg->fg_rgb_swap || cfg->fg_byte_swap || cfg->in_bg.buffer == NULL || cfg->in_fg.buffer == NULL ||
        cfg->out.buffer == NULL) {
        ESP_LOGW(TAG, "blend: unsupported config (cm/swap)");
        ret_u32(cpu, ESP_ERR_INVALID_ARG);
        return;
    }
    if (cfg->in_bg.block_w != cfg->in_fg.block_w || cfg->in_bg.block_h != cfg->in_fg.block_h) {
        ESP_LOGW(TAG, "blend: bg/fg block size mismatch");
        ret_u32(cpu, ESP_ERR_INVALID_ARG);
        return;
    }

    uint16_t const *bg_buf = (uint16_t const *)cfg->in_bg.buffer;
    uint16_t const *fg_buf = (uint16_t const *)cfg->in_fg.buffer;
    uint16_t       *outbuf = (uint16_t *)cfg->out.buffer;
    uint16_t const  dflt   = pack565(cfg->ck_rgb_default_val.r, cfg->ck_rgb_default_val.g, cfg->ck_rgb_default_val.b);

    for (uint32_t row = 0; row < cfg->in_bg.block_h; row++) {
        uint16_t const *bg_row = bg_buf + (cfg->in_bg.block_offset_y + row) * cfg->in_bg.pic_w + cfg->in_bg.block_offset_x;
        uint16_t const *fg_row = fg_buf + (cfg->in_fg.block_offset_y + row) * cfg->in_fg.pic_w + cfg->in_fg.block_offset_x;
        uint16_t       *out_row = outbuf + (cfg->out.block_offset_y + row) * cfg->out.pic_w + cfg->out.block_offset_x;

        for (uint32_t col = 0; col < cfg->in_bg.block_w; col++) {
            uint16_t bgv = bg_row[col], fgv = fg_row[col];

            bool bg_in = false, fg_in = false;
            if (cfg->bg_ck_en) {
                uint8_t r, g, b;
                unpack565(bgv, &r, &g, &b);
                bg_in = in_range888(r, g, b, cfg->bg_ck_rgb_low_thres, cfg->bg_ck_rgb_high_thres);
            }
            if (cfg->fg_ck_en) {
                uint8_t r, g, b;
                unpack565(fgv, &r, &g, &b);
                fg_in = in_range888(r, g, b, cfg->fg_ck_rgb_low_thres, cfg->fg_ck_rgb_high_thres);
            }

            uint16_t result;
            if (bg_in && fg_in) {
                result = dflt;
            } else if (bg_in) {
                result = cfg->ck_reverse_bg2fg ? fgv : bgv;
            } else if (fg_in) {
                result = bgv;  // fg is the key colour here -> transparent, bg shows through
            } else {
                result = fgv;  // normal case: opaque foreground over background
            }
            out_row[col] = result;
        }
    }

    ppa_fire_done(cpu, client, cfg->user_data);
    ret_u32(cpu, ESP_OK);
}
