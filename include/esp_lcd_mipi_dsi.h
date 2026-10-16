#pragma once

// Minimal desktop stand-in for ESP-IDF's esp_lcd_mipi_dsi.h: just the
// handful of declarations the Tanmatsu display's double-buffering
// protocol needs (see graceloader.h and emulator/src/shims/esp_lcd_shim.c).

#include <stdbool.h>

#include "esp_err.h"
#include "esp_lcd_types.h"

typedef struct {
} esp_lcd_dpi_panel_event_data_t;

typedef bool (*esp_lcd_dpi_panel_color_trans_done_cb_t)(
    esp_lcd_panel_handle_t panel, esp_lcd_dpi_panel_event_data_t *edata, void *user_ctx
);
typedef esp_lcd_dpi_panel_color_trans_done_cb_t esp_lcd_dpi_panel_refresh_done_cb_t;

typedef struct {
    esp_lcd_dpi_panel_color_trans_done_cb_t on_color_trans_done;
    esp_lcd_dpi_panel_refresh_done_cb_t     on_refresh_done;
} esp_lcd_dpi_panel_event_callbacks_t;

esp_err_t esp_lcd_dpi_panel_register_event_callbacks(
    esp_lcd_panel_handle_t dpi_panel, esp_lcd_dpi_panel_event_callbacks_t const *cbs, void *user_ctx
);
