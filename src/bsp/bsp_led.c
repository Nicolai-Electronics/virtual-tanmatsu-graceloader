#include "bsp/led.h"

#include <string.h>

#include "esp_err.h"

#define BSP_LED_SIMULATED_COUNT 1

static uint32_t s_pixels[BSP_LED_SIMULATED_COUNT] = {0};
static uint8_t  s_brightness                      = 100;
static bool     s_automatic                       = true;

esp_err_t bsp_led_write(const uint8_t* data, uint32_t length) {
    (void)data;
    (void)length;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t bsp_led_set_brightness(uint8_t percentage) {
    s_brightness = percentage;
    return ESP_OK;
}

esp_err_t bsp_led_get_brightness(uint8_t* out_percentage) {
    if (out_percentage == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_percentage = s_brightness;
    return ESP_OK;
}

esp_err_t bsp_led_set_mode(bool automatic) {
    s_automatic = automatic;
    return ESP_OK;
}

esp_err_t bsp_led_get_mode(bool* out_automatic) {
    if (out_automatic == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_automatic = s_automatic;
    return ESP_OK;
}

esp_err_t bsp_led_send(void) {
    return ESP_OK;
}

esp_err_t bsp_led_clear(void) {
    memset(s_pixels, 0, sizeof(s_pixels));
    return ESP_OK;
}

esp_err_t bsp_led_set_pixel(uint32_t index, uint32_t color) {
    if (index >= BSP_LED_SIMULATED_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    s_pixels[index] = color;
    return ESP_OK;
}

esp_err_t bsp_led_set_pixel_rgb(uint32_t index, uint8_t red, uint8_t green, uint8_t blue) {
    return bsp_led_set_pixel(index, ((uint32_t)red << 16) | ((uint32_t)green << 8) | blue);
}

esp_err_t bsp_led_set_pixel_rgbw(uint32_t index, uint8_t red, uint8_t green, uint8_t blue, uint8_t white) {
    (void)white;
    return bsp_led_set_pixel_rgb(index, red, green, blue);
}

esp_err_t bsp_led_set_pixel_hsv(uint32_t index, uint16_t hue, uint8_t saturation, uint8_t value) {
    (void)hue;
    (void)saturation;
    (void)value;
    if (index >= BSP_LED_SIMULATED_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

esp_err_t bsp_led_get_count(uint32_t* out_count) {
    if (out_count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_count = BSP_LED_SIMULATED_COUNT;
    return ESP_OK;
}
