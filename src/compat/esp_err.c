#include "esp_err.h"

#include <stdio.h>
#include <stdlib.h>

// Workaround for app_main.c using an undeclared `res` local, see esp_err.h.
esp_err_t res = ESP_OK;

const char* esp_err_to_name(esp_err_t err) {
    switch (err) {
        case ESP_OK:
            return "ESP_OK";
        case ESP_FAIL:
            return "ESP_FAIL";
        case ESP_ERR_NO_MEM:
            return "ESP_ERR_NO_MEM";
        case ESP_ERR_INVALID_ARG:
            return "ESP_ERR_INVALID_ARG";
        case ESP_ERR_INVALID_STATE:
            return "ESP_ERR_INVALID_STATE";
        case ESP_ERR_INVALID_SIZE:
            return "ESP_ERR_INVALID_SIZE";
        case ESP_ERR_NOT_FOUND:
            return "ESP_ERR_NOT_FOUND";
        case ESP_ERR_NOT_SUPPORTED:
            return "ESP_ERR_NOT_SUPPORTED";
        case ESP_ERR_TIMEOUT:
            return "ESP_ERR_TIMEOUT";
        default:
            return "ESP_ERR_UNKNOWN";
    }
}

void esp_error_check_failed(esp_err_t err, const char* file, int line, const char* func, const char* expr) {
    fprintf(stderr, "ESP_ERROR_CHECK failed: esp_err_t 0x%x (%s) at %s:%d (%s) -- expression: %s\n", (unsigned)err,
            esp_err_to_name(err), file, line, func, expr);
    abort();
}
