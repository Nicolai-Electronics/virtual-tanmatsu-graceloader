#include "bsp/i2c.h"

#include <pthread.h>

#include "esp_err.h"

// No real I2C bus exists on the desktop simulator, but the claim/release
// pair is implemented for real so callers written against the real BSP
// behave the same way here.
static pthread_mutex_t s_bus_mutex = PTHREAD_MUTEX_INITIALIZER;

esp_err_t bsp_i2c_primary_bus_get_handle(i2c_master_bus_handle_t* handle) {
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *handle = NULL;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t bsp_i2c_primary_bus_get_semaphore(SemaphoreHandle_t* semaphore) {
    if (semaphore == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *semaphore = NULL;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t bsp_i2c_primary_bus_claim(void) {
    pthread_mutex_lock(&s_bus_mutex);
    return ESP_OK;
}

esp_err_t bsp_i2c_primary_bus_release(void) {
    pthread_mutex_unlock(&s_bus_mutex);
    return ESP_OK;
}
