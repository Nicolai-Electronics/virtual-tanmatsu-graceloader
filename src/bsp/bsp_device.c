#include "bsp/device.h"

#include <stdlib.h>
#include <string.h>

#include "bsp_sdl_internal.h"
#include "esp_err.h"
#include "esp_log.h"

static char const TAG[] = "bsp_device";

static bool s_initialized = false;

esp_err_t bsp_device_initialize(const bsp_configuration_t* configuration) {
    if (s_initialized) {
        return ESP_OK;
    }

    // The display configuration is not ignored here: the requested color
    // format decides the panel format the whole app then builds its
    // framebuffer around (see negotiate_color_format() in bsp_display.c).
    bsp_display_color_format_t requested_color_format =
        configuration ? configuration->display.requested_color_format : BSP_DISPLAY_COLOR_FORMAT_16_565RGB;

    esp_err_t err = bsp_display_internal_create_window(BSP_SDL_DEFAULT_WIDTH, BSP_SDL_DEFAULT_HEIGHT,
                                                       requested_color_format);
    if (err != ESP_OK) {
        return err;
    }

    err = bsp_input_internal_init();
    if (err != ESP_OK) {
        return err;
    }

    s_initialized = true;
    return ESP_OK;
}

esp_err_t bsp_device_get_name(char* output, uint8_t buffer_length) {
    if (output == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    static char const name[] = "SDL simulator";
    if (buffer_length < sizeof(name)) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(output, name, sizeof(name));
    return ESP_OK;
}

esp_err_t bsp_device_get_manufacturer(char* output, uint8_t buffer_length) {
    if (output == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    static char const manufacturer[] = "Nicolai Electronics";
    if (buffer_length < sizeof(manufacturer)) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(output, manufacturer, sizeof(manufacturer));
    return ESP_OK;
}

bool bsp_device_get_initialized_without_coprocessor(void) {
    return true;
}

void bsp_device_restart_to_launcher(void) {
    ESP_LOGW(TAG, "Restart to launcher requested, exiting simulator");
    exit(0);
}
