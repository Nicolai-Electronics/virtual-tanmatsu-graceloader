#pragma once

// Minimal desktop stand-in for ESP-IDF's esp_lcd_types.h.
// The SDL target has no real LCD panel driver, so these are opaque handles
// that are never dereferenced.

typedef struct esp_lcd_panel_t*    esp_lcd_panel_handle_t;
typedef struct esp_lcd_panel_io_t* esp_lcd_panel_io_handle_t;
