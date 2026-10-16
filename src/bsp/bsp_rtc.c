#include "bsp/rtc.h"

#include <time.h>

#include "esp_err.h"

esp_err_t bsp_rtc_get_time(uint32_t* value) {
    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    // No dedicated RTC hardware exists on the desktop simulator; the host
    // system clock is used instead.
    *value = (uint32_t)time(NULL);
    return ESP_OK;
}

esp_err_t bsp_rtc_set_time(uint32_t value) {
    (void)value;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t bsp_rtc_update_time(void) {
    return ESP_OK;
}

esp_err_t bsp_rtc_get_alarm(uint32_t* value) {
    (void)value;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t bsp_rtc_set_alarm(uint32_t value) {
    (void)value;
    return ESP_ERR_NOT_SUPPORTED;
}
