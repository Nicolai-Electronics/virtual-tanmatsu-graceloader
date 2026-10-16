#include "bsp/sensors.h"

#include "esp_err.h"

bool bsp_sensor_get_supported(bsp_sensor_type_t type) {
    (void)type;
    // No environmental sensors exist on the desktop simulator.
    return false;
}

esp_err_t bsp_sensor_enable(bsp_sensor_type_t type) {
    (void)type;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t bsp_sensor_disable(bsp_sensor_type_t type) {
    (void)type;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t bsp_sensor_status(bsp_sensor_type_t type, bool* out_ready) {
    (void)type;
    if (out_ready == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_ready = false;
    return ESP_OK;
}

esp_err_t bsp_sensor_read(bsp_sensor_type_t type, float* out_value) {
    (void)type;
    if (out_value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_value = 0.0f;
    return ESP_ERR_NOT_FOUND;
}
