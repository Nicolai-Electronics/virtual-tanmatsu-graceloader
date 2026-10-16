#include "bsp/power.h"

#include <stdlib.h>

#include "esp_err.h"
#include "esp_log.h"

static char const TAG[] = "bsp_power";

static bool     s_usb_host_boost_enabled = false;
static bsp_radio_state_t s_radio_state   = BSP_POWER_RADIO_STATE_OFF;

esp_err_t bsp_power_get_battery_information(bsp_power_battery_information_t* out_information) {
    if (out_information == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    // No real battery hardware exists on the desktop simulator: report a
    // plausible "always on mains power" state.
    *out_information = (bsp_power_battery_information_t){
        .type                     = "simulated",
        .power_supply_available   = true,
        .battery_available        = false,
        .charging_disabled        = true,
        .battery_charging         = false,
        .maximum_charging_current = 0,
        .current_charging_current = 0,
        .voltage                  = 0,
        .charging_target_voltage  = 0,
        .remaining_percentage     = 100.0,
    };
    return ESP_OK;
}

esp_err_t bsp_power_get_battery_voltage(uint16_t* out_millivolt) {
    if (out_millivolt == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_millivolt = 0;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t bsp_power_get_system_voltage(uint16_t* out_millivolt) {
    if (out_millivolt == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_millivolt = 5000;
    return ESP_OK;
}

esp_err_t bsp_power_get_input_voltage(uint16_t* out_millivolt) {
    if (out_millivolt == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_millivolt = 5000;
    return ESP_OK;
}

esp_err_t bsp_power_get_charging_configuration(bool* out_disabled, uint16_t* out_current) {
    if (out_disabled == NULL || out_current == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_disabled = true;
    *out_current  = 0;
    return ESP_OK;
}

esp_err_t bsp_power_configure_charging(bool disable, uint16_t current) {
    (void)disable;
    (void)current;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t bsp_power_get_usb_host_boost_enabled(bool* out_enabled) {
    if (out_enabled == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_enabled = s_usb_host_boost_enabled;
    return ESP_OK;
}

esp_err_t bsp_power_set_usb_host_boost_enabled(bool enable) {
    s_usb_host_boost_enabled = enable;
    return ESP_OK;
}

esp_err_t bsp_power_get_radio_state(bsp_radio_state_t* out_state) {
    if (out_state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_state = s_radio_state;
    return ESP_OK;
}

esp_err_t bsp_power_set_radio_state(bsp_radio_state_t state) {
    s_radio_state = state;
    return ESP_OK;
}

esp_err_t bsp_power_off(bool enable_alarm_wakeup) {
    (void)enable_alarm_wakeup;
    ESP_LOGW(TAG, "Power off requested, exiting simulator");
    exit(0);
}
