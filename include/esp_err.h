#pragma once

// Minimal desktop stand-in for ESP-IDF's esp_err.h.

#include <inttypes.h>
#include <stdint.h>

#include "esp_log.h"

typedef int32_t esp_err_t;

#define ESP_OK                0
#define ESP_FAIL              -1
#define ESP_ERR_NO_MEM        0x101
#define ESP_ERR_INVALID_ARG   0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_SIZE  0x104
#define ESP_ERR_NOT_FOUND     0x105
#define ESP_ERR_NOT_SUPPORTED 0x106
#define ESP_ERR_TIMEOUT       0x107

// app_main.c is an unmodified example file; it assigns to an `esp_err_t res`
// local variable without declaring it. This global fills that gap so the
// file compiles and runs as-is.
extern esp_err_t res;

// app_main.c also references BLACK/WHITE pax_col_t constants without
// including a header that defines them.
#define BLACK 0xFF000000u
#define WHITE 0xFFFFFFFFu

const char* esp_err_to_name(esp_err_t err);

void esp_error_check_failed(esp_err_t err, const char* file, int line, const char* func, const char* expr);

#define ESP_ERROR_CHECK(x)                                                       \
    do {                                                                         \
        esp_err_t __err_rc = (x);                                                \
        if (__err_rc != ESP_OK) {                                                \
            esp_error_check_failed(__err_rc, __FILE__, __LINE__, __func__, #x);  \
        }                                                                        \
    } while (0)
