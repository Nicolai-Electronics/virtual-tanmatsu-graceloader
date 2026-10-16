#pragma once

// Minimal desktop stand-in for ESP-IDF's esp_timer.h.

#include <stdint.h>

// Returns the number of microseconds since an arbitrary but fixed point in
// the past (matching esp_timer_get_time's monotonic-clock contract).
int64_t esp_timer_get_time(void);
