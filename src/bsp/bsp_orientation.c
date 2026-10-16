#include "bsp/orientation.h"

#include "esp_err.h"

esp_err_t bsp_orientation_enable_gyroscope(void) {
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t bsp_orientation_disable_gyroscope(void) {
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t bsp_orientation_enable_accelerometer(void) {
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t bsp_orientation_disable_accelerometer(void) {
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t bsp_orientation_get(bool* out_gyro_ready, bool* out_accel_ready, float* out_gyro_x, float* out_gyro_y,
                              float* out_gyro_z, float* out_accel_x, float* out_accel_y, float* out_accel_z) {
    if (out_gyro_ready == NULL || out_accel_ready == NULL || out_gyro_x == NULL || out_gyro_y == NULL ||
        out_gyro_z == NULL || out_accel_x == NULL || out_accel_y == NULL || out_accel_z == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // No gyroscope/accelerometer hardware exists on the desktop simulator.
    *out_gyro_ready  = false;
    *out_accel_ready = false;
    *out_gyro_x = *out_gyro_y = *out_gyro_z = 0.0f;
    *out_accel_x = *out_accel_y = *out_accel_z = 0.0f;

    return ESP_OK;
}
