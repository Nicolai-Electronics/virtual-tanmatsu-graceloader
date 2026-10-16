#include "bsp/input.h"

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/queue.h"

static char const TAG[] = "bsp_input";

#define MAX_HOOKS 8

typedef struct {
    bool                 used;
    bsp_input_hook_cb_t  callback;
    void*                user_data;
} hook_slot_t;

static QueueHandle_t s_queue                  = NULL;
static hook_slot_t   s_hooks[MAX_HOOKS]        = {0};
static uint8_t       s_backlight_percentage    = 100;

// Software auto-repeat for the arrow keys, driven from our own polling
// (pump_sdl_events(), via the FreeRTOS-compat idle hook) rather than SDL's
// own key-repeat events, so the rate doesn't depend on the host OS's
// keyboard repeat settings.
#define KEY_REPEAT_INITIAL_DELAY_MS 450
#define KEY_REPEAT_INTERVAL_MS      120

typedef struct {
    bsp_input_navigation_key_t key;
    bool                        active;
    uint32_t                    next_repeat_ms;
} key_repeat_t;

static key_repeat_t s_arrow_repeat[4] = {
    {.key = BSP_INPUT_NAVIGATION_KEY_UP},
    {.key = BSP_INPUT_NAVIGATION_KEY_DOWN},
    {.key = BSP_INPUT_NAVIGATION_KEY_LEFT},
    {.key = BSP_INPUT_NAVIGATION_KEY_RIGHT},
};

// Indexed directly by the bsp_input_scancode_t / bsp_input_navigation_key_t
// enum values, which are small integers (scancodes go up to the 0xe06d
// escaped range).
static bool s_scancode_state[0x10000]   = {0};
static bool s_navigation_state[64]      = {0};

static uint32_t map_modifiers(SDL_Keymod mod) {
    uint32_t out = 0;
    if (mod & KMOD_LSHIFT) out |= BSP_INPUT_MODIFIER_SHIFT_L;
    if (mod & KMOD_RSHIFT) out |= BSP_INPUT_MODIFIER_SHIFT_R;
    if (mod & KMOD_LCTRL) out |= BSP_INPUT_MODIFIER_CTRL_L;
    if (mod & KMOD_RCTRL) out |= BSP_INPUT_MODIFIER_CTRL_R;
    if (mod & KMOD_LALT) out |= BSP_INPUT_MODIFIER_ALT_L;
    if (mod & KMOD_RALT) out |= BSP_INPUT_MODIFIER_ALT_R;
    if (mod & KMOD_LGUI) out |= BSP_INPUT_MODIFIER_SUPER_L;
    if (mod & KMOD_RGUI) out |= BSP_INPUT_MODIFIER_SUPER_R;
    if (mod & KMOD_CAPS) out |= BSP_INPUT_MODIFIER_CAPSLOCK;
    return out;
}

// Physical-key scancode mapping, one entry per key position (independent of
// keyboard layout), following the same idea as the real target's PC
// scancode set 1 numbering.
static bsp_input_scancode_t map_scancode(SDL_Scancode sc) {
    switch (sc) {
        case SDL_SCANCODE_ESCAPE: return BSP_INPUT_SCANCODE_ESC;
        case SDL_SCANCODE_1: return BSP_INPUT_SCANCODE_1;
        case SDL_SCANCODE_2: return BSP_INPUT_SCANCODE_2;
        case SDL_SCANCODE_3: return BSP_INPUT_SCANCODE_3;
        case SDL_SCANCODE_4: return BSP_INPUT_SCANCODE_4;
        case SDL_SCANCODE_5: return BSP_INPUT_SCANCODE_5;
        case SDL_SCANCODE_6: return BSP_INPUT_SCANCODE_6;
        case SDL_SCANCODE_7: return BSP_INPUT_SCANCODE_7;
        case SDL_SCANCODE_8: return BSP_INPUT_SCANCODE_8;
        case SDL_SCANCODE_9: return BSP_INPUT_SCANCODE_9;
        case SDL_SCANCODE_0: return BSP_INPUT_SCANCODE_0;
        case SDL_SCANCODE_MINUS: return BSP_INPUT_SCANCODE_MINUS;
        case SDL_SCANCODE_EQUALS: return BSP_INPUT_SCANCODE_EQUAL;
        case SDL_SCANCODE_BACKSPACE: return BSP_INPUT_SCANCODE_BACKSPACE;
        case SDL_SCANCODE_TAB: return BSP_INPUT_SCANCODE_TAB;
        case SDL_SCANCODE_Q: return BSP_INPUT_SCANCODE_Q;
        case SDL_SCANCODE_W: return BSP_INPUT_SCANCODE_W;
        case SDL_SCANCODE_E: return BSP_INPUT_SCANCODE_E;
        case SDL_SCANCODE_R: return BSP_INPUT_SCANCODE_R;
        case SDL_SCANCODE_T: return BSP_INPUT_SCANCODE_T;
        case SDL_SCANCODE_Y: return BSP_INPUT_SCANCODE_Y;
        case SDL_SCANCODE_U: return BSP_INPUT_SCANCODE_U;
        case SDL_SCANCODE_I: return BSP_INPUT_SCANCODE_I;
        case SDL_SCANCODE_O: return BSP_INPUT_SCANCODE_O;
        case SDL_SCANCODE_P: return BSP_INPUT_SCANCODE_P;
        case SDL_SCANCODE_LEFTBRACKET: return BSP_INPUT_SCANCODE_LEFTBRACE;
        case SDL_SCANCODE_RIGHTBRACKET: return BSP_INPUT_SCANCODE_RIGHTBRACE;
        case SDL_SCANCODE_RETURN: return BSP_INPUT_SCANCODE_ENTER;
        case SDL_SCANCODE_LCTRL: return BSP_INPUT_SCANCODE_LEFTCTRL;
        case SDL_SCANCODE_A: return BSP_INPUT_SCANCODE_A;
        case SDL_SCANCODE_S: return BSP_INPUT_SCANCODE_S;
        case SDL_SCANCODE_D: return BSP_INPUT_SCANCODE_D;
        case SDL_SCANCODE_F: return BSP_INPUT_SCANCODE_F;
        case SDL_SCANCODE_G: return BSP_INPUT_SCANCODE_G;
        case SDL_SCANCODE_H: return BSP_INPUT_SCANCODE_H;
        case SDL_SCANCODE_J: return BSP_INPUT_SCANCODE_J;
        case SDL_SCANCODE_K: return BSP_INPUT_SCANCODE_K;
        case SDL_SCANCODE_L: return BSP_INPUT_SCANCODE_L;
        case SDL_SCANCODE_SEMICOLON: return BSP_INPUT_SCANCODE_SEMICOLON;
        case SDL_SCANCODE_APOSTROPHE: return BSP_INPUT_SCANCODE_APOSTROPHE;
        case SDL_SCANCODE_GRAVE: return BSP_INPUT_SCANCODE_GRAVE;
        case SDL_SCANCODE_LSHIFT: return BSP_INPUT_SCANCODE_LEFTSHIFT;
        case SDL_SCANCODE_BACKSLASH: return BSP_INPUT_SCANCODE_BACKSLASH;
        case SDL_SCANCODE_Z: return BSP_INPUT_SCANCODE_Z;
        case SDL_SCANCODE_X: return BSP_INPUT_SCANCODE_X;
        case SDL_SCANCODE_C: return BSP_INPUT_SCANCODE_C;
        case SDL_SCANCODE_V: return BSP_INPUT_SCANCODE_V;
        case SDL_SCANCODE_B: return BSP_INPUT_SCANCODE_B;
        case SDL_SCANCODE_N: return BSP_INPUT_SCANCODE_N;
        case SDL_SCANCODE_M: return BSP_INPUT_SCANCODE_M;
        case SDL_SCANCODE_COMMA: return BSP_INPUT_SCANCODE_COMMA;
        case SDL_SCANCODE_PERIOD: return BSP_INPUT_SCANCODE_DOT;
        case SDL_SCANCODE_SLASH: return BSP_INPUT_SCANCODE_SLASH;
        case SDL_SCANCODE_RSHIFT: return BSP_INPUT_SCANCODE_RIGHTSHIFT;
        case SDL_SCANCODE_KP_MULTIPLY: return BSP_INPUT_SCANCODE_KPASTERISK;
        case SDL_SCANCODE_LALT: return BSP_INPUT_SCANCODE_LEFTALT;
        case SDL_SCANCODE_SPACE: return BSP_INPUT_SCANCODE_SPACE;
        case SDL_SCANCODE_CAPSLOCK: return BSP_INPUT_SCANCODE_CAPSLOCK;
        case SDL_SCANCODE_F1: return BSP_INPUT_SCANCODE_F1;
        case SDL_SCANCODE_F2: return BSP_INPUT_SCANCODE_F2;
        case SDL_SCANCODE_F3: return BSP_INPUT_SCANCODE_F3;
        case SDL_SCANCODE_F4: return BSP_INPUT_SCANCODE_F4;
        case SDL_SCANCODE_F5: return BSP_INPUT_SCANCODE_F5;
        case SDL_SCANCODE_F6: return BSP_INPUT_SCANCODE_F6;
        case SDL_SCANCODE_F7: return BSP_INPUT_SCANCODE_F7;
        case SDL_SCANCODE_F8: return BSP_INPUT_SCANCODE_F8;
        case SDL_SCANCODE_F9: return BSP_INPUT_SCANCODE_F9;
        case SDL_SCANCODE_F10: return BSP_INPUT_SCANCODE_F10;
        case SDL_SCANCODE_NUMLOCKCLEAR: return BSP_INPUT_SCANCODE_NUMLOCK;
        case SDL_SCANCODE_SCROLLLOCK: return BSP_INPUT_SCANCODE_SCROLLLOCK;
        case SDL_SCANCODE_KP_7: return BSP_INPUT_SCANCODE_KP7;
        case SDL_SCANCODE_KP_8: return BSP_INPUT_SCANCODE_KP8;
        case SDL_SCANCODE_KP_9: return BSP_INPUT_SCANCODE_KP9;
        case SDL_SCANCODE_KP_MINUS: return BSP_INPUT_SCANCODE_KPMINUS;
        case SDL_SCANCODE_KP_4: return BSP_INPUT_SCANCODE_KP4;
        case SDL_SCANCODE_KP_5: return BSP_INPUT_SCANCODE_KP5;
        case SDL_SCANCODE_KP_6: return BSP_INPUT_SCANCODE_KP6;
        case SDL_SCANCODE_KP_PLUS: return BSP_INPUT_SCANCODE_KPPLUS;
        case SDL_SCANCODE_KP_1: return BSP_INPUT_SCANCODE_KP1;
        case SDL_SCANCODE_KP_2: return BSP_INPUT_SCANCODE_KP2;
        case SDL_SCANCODE_KP_3: return BSP_INPUT_SCANCODE_KP3;
        case SDL_SCANCODE_KP_0: return BSP_INPUT_SCANCODE_KP0;
        case SDL_SCANCODE_KP_PERIOD: return BSP_INPUT_SCANCODE_KPDOT;
        case SDL_SCANCODE_F11: return BSP_INPUT_SCANCODE_F11;
        case SDL_SCANCODE_F12: return BSP_INPUT_SCANCODE_F12;
        case SDL_SCANCODE_KP_ENTER: return BSP_INPUT_SCANCODE_ESCAPED_KPENTER;
        case SDL_SCANCODE_RCTRL: return BSP_INPUT_SCANCODE_ESCAPED_RCTRL;
        case SDL_SCANCODE_MUTE: return BSP_INPUT_SCANCODE_ESCAPED_VOLUME_MUTE;
        case SDL_SCANCODE_VOLUMEDOWN: return BSP_INPUT_SCANCODE_ESCAPED_VOLUME_DOWN;
        case SDL_SCANCODE_VOLUMEUP: return BSP_INPUT_SCANCODE_ESCAPED_VOLUME_UP;
        case SDL_SCANCODE_KP_DIVIDE: return BSP_INPUT_SCANCODE_ESCAPED_GREY_KPSLASH;
        case SDL_SCANCODE_RALT: return BSP_INPUT_SCANCODE_ESCAPED_RALT;
        case SDL_SCANCODE_HOME: return BSP_INPUT_SCANCODE_ESCAPED_GREY_HOME;
        case SDL_SCANCODE_UP: return BSP_INPUT_SCANCODE_ESCAPED_GREY_UP;
        case SDL_SCANCODE_PAGEUP: return BSP_INPUT_SCANCODE_ESCAPED_GREY_PGUP;
        case SDL_SCANCODE_LEFT: return BSP_INPUT_SCANCODE_ESCAPED_GREY_LEFT;
        case SDL_SCANCODE_RIGHT: return BSP_INPUT_SCANCODE_ESCAPED_GREY_RIGHT;
        case SDL_SCANCODE_END: return BSP_INPUT_SCANCODE_ESCAPED_GREY_END;
        case SDL_SCANCODE_DOWN: return BSP_INPUT_SCANCODE_ESCAPED_GREY_DOWN;
        case SDL_SCANCODE_PAGEDOWN: return BSP_INPUT_SCANCODE_ESCAPED_GREY_PGDN;
        case SDL_SCANCODE_INSERT: return BSP_INPUT_SCANCODE_ESCAPED_GREY_INSERT;
        case SDL_SCANCODE_DELETE: return BSP_INPUT_SCANCODE_ESCAPED_GREY_DEL;
        case SDL_SCANCODE_LGUI: return BSP_INPUT_SCANCODE_ESCAPED_LEFTMETA;
        case SDL_SCANCODE_RGUI: return BSP_INPUT_SCANCODE_ESCAPED_RIGHTMETA;
        case SDL_SCANCODE_APPLICATION: return BSP_INPUT_SCANCODE_ESCAPED_MENU;
        default: return BSP_INPUT_SCANCODE_NONE;
    }
}

static bsp_input_navigation_key_t map_navigation(SDL_Keycode kc) {
    switch (kc) {
        case SDLK_ESCAPE: return BSP_INPUT_NAVIGATION_KEY_ESC;
        case SDLK_LEFT: return BSP_INPUT_NAVIGATION_KEY_LEFT;
        case SDLK_RIGHT: return BSP_INPUT_NAVIGATION_KEY_RIGHT;
        case SDLK_UP: return BSP_INPUT_NAVIGATION_KEY_UP;
        case SDLK_DOWN: return BSP_INPUT_NAVIGATION_KEY_DOWN;
        case SDLK_HOME: return BSP_INPUT_NAVIGATION_KEY_HOME;
        case SDLK_END: return BSP_INPUT_NAVIGATION_KEY_END;
        case SDLK_PAGEUP: return BSP_INPUT_NAVIGATION_KEY_PGUP;
        case SDLK_PAGEDOWN: return BSP_INPUT_NAVIGATION_KEY_PGDN;
        case SDLK_APPLICATION:
        case SDLK_MENU: return BSP_INPUT_NAVIGATION_KEY_MENU;
        case SDLK_RETURN:
        case SDLK_KP_ENTER: return BSP_INPUT_NAVIGATION_KEY_RETURN;
        case SDLK_LGUI:
        case SDLK_RGUI: return BSP_INPUT_NAVIGATION_KEY_SUPER;
        case SDLK_TAB: return BSP_INPUT_NAVIGATION_KEY_TAB;
        case SDLK_BACKSPACE: return BSP_INPUT_NAVIGATION_KEY_BACKSPACE;
        case SDLK_SPACE: return BSP_INPUT_NAVIGATION_KEY_SPACE_M;
        case SDLK_F1: return BSP_INPUT_NAVIGATION_KEY_F1;
        case SDLK_F2: return BSP_INPUT_NAVIGATION_KEY_F2;
        case SDLK_F3: return BSP_INPUT_NAVIGATION_KEY_F3;
        case SDLK_F4: return BSP_INPUT_NAVIGATION_KEY_F4;
        case SDLK_F5: return BSP_INPUT_NAVIGATION_KEY_F5;
        case SDLK_F6: return BSP_INPUT_NAVIGATION_KEY_F6;
        case SDLK_F7: return BSP_INPUT_NAVIGATION_KEY_F7;
        case SDLK_F8: return BSP_INPUT_NAVIGATION_KEY_F8;
        case SDLK_F9: return BSP_INPUT_NAVIGATION_KEY_F9;
        case SDLK_F10: return BSP_INPUT_NAVIGATION_KEY_F10;
        case SDLK_F11: return BSP_INPUT_NAVIGATION_KEY_F11;
        case SDLK_F12: return BSP_INPUT_NAVIGATION_KEY_F12;
        case SDLK_VOLUMEUP: return BSP_INPUT_NAVIGATION_KEY_VOLUME_UP;
        case SDLK_VOLUMEDOWN: return BSP_INPUT_NAVIGATION_KEY_VOLUME_DOWN;
        default: return BSP_INPUT_NAVIGATION_KEY_NONE;
    }
}

static void set_scancode_state(bsp_input_scancode_t scancode, bool state) {
    s_scancode_state[scancode & 0xFFFF] = state;
}

static void set_navigation_state(bsp_input_navigation_key_t key, bool state) {
    if ((size_t)key < (sizeof(s_navigation_state) / sizeof(s_navigation_state[0]))) {
        s_navigation_state[key] = state;
    }
}

// Offers the event to registered hooks first; returns true if consumed.
static bool process_hooks(bsp_input_event_t* event) {
    for (int i = 0; i < MAX_HOOKS; i++) {
        if (s_hooks[i].used && s_hooks[i].callback(event, s_hooks[i].user_data)) {
            return true;
        }
    }
    return false;
}

static void send_navigation_event(bsp_input_navigation_key_t key, bool state, uint32_t modifiers) {
    bsp_input_event_t event = {
        .type                      = INPUT_EVENT_TYPE_NAVIGATION,
        .args_navigation.key       = key,
        .args_navigation.modifiers = modifiers,
        .args_navigation.state     = state,
    };
    if (!process_hooks(&event)) {
        xQueueSend(s_queue, &event, 0);
    }
}

static void send_scancode_event(bsp_input_scancode_t scancode, bool state) {
    bsp_input_event_t event = {
        .type                   = INPUT_EVENT_TYPE_SCANCODE,
        .args_scancode.scancode = scancode | (state ? 0 : BSP_INPUT_SCANCODE_RELEASE_MODIFIER),
    };
    if (!process_hooks(&event)) {
        xQueueSend(s_queue, &event, 0);
    }
}

static void send_keyboard_event(char ascii, char const* utf8, uint32_t modifiers) {
    bsp_input_event_t event = {
        .type                    = INPUT_EVENT_TYPE_KEYBOARD,
        .args_keyboard.ascii     = ascii,
        .args_keyboard.modifiers = modifiers,
    };
    size_t max_len = sizeof(event.args_keyboard.utf8) - 1;
    size_t len     = strnlen(utf8, max_len);
    memcpy(event.args_keyboard.utf8, utf8, len);
    event.args_keyboard.utf8[len] = '\0';

    if (!process_hooks(&event)) {
        xQueueSend(s_queue, &event, 0);
    }
}

static void send_action_event(bsp_input_action_type_t action, bool state) {
    bsp_input_event_t event = {
        .type              = INPUT_EVENT_TYPE_ACTION,
        .args_action.type  = action,
        .args_action.state = state,
    };
    if (!process_hooks(&event)) {
        xQueueSend(s_queue, &event, 0);
    }
}

// Index into s_arrow_repeat for a navigation key, or -1 if it isn't one of
// the repeating arrow keys.
static int arrow_repeat_slot(bsp_input_navigation_key_t key) {
    for (size_t i = 0; i < sizeof(s_arrow_repeat) / sizeof(s_arrow_repeat[0]); i++) {
        if (s_arrow_repeat[i].key == key) {
            return (int)i;
        }
    }
    return -1;
}

// Called from the idle hook to fire due repeats for any arrow key that's
// still held down.
static void update_key_repeat(void) {
    uint32_t now       = SDL_GetTicks();
    uint32_t modifiers  = map_modifiers(SDL_GetModState());
    for (size_t i = 0; i < sizeof(s_arrow_repeat) / sizeof(s_arrow_repeat[0]); i++) {
        key_repeat_t* rep = &s_arrow_repeat[i];
        // Wraparound-safe "now >= next_repeat_ms" (SDL_GetTicks() wraps
        // after ~49 days of uptime).
        if (rep->active && (int32_t)(now - rep->next_repeat_ms) >= 0) {
            send_navigation_event(rep->key, true, modifiers);
            rep->next_repeat_ms = now + KEY_REPEAT_INTERVAL_MS;
        }
    }
}

static void handle_key_event(SDL_KeyboardEvent const* key_event) {
    if (key_event->repeat) {
        // SDL's own OS-driven auto-repeat is ignored - arrow keys get our
        // own repeat via update_key_repeat() below instead, and nothing
        // else repeats, matching the reference target implementation (its
        // key-repeat thread is disabled too).
        return;
    }

    bool          state      = (key_event->type == SDL_KEYDOWN);
    SDL_Scancode  sdl_sc     = key_event->keysym.scancode;
    SDL_Keycode   sdl_kc     = key_event->keysym.sym;
    uint32_t      modifiers  = map_modifiers(SDL_GetModState());

    bsp_input_scancode_t scancode = map_scancode(sdl_sc);
    if (scancode != BSP_INPUT_SCANCODE_NONE) {
        set_scancode_state(scancode, state);
        send_scancode_event(scancode, state);
    }

    bsp_input_navigation_key_t nav_key = map_navigation(sdl_kc);
    if (nav_key != BSP_INPUT_NAVIGATION_KEY_NONE) {
        set_navigation_state(nav_key, state);
        send_navigation_event(nav_key, state, modifiers);

        int slot = arrow_repeat_slot(nav_key);
        if (slot >= 0) {
            s_arrow_repeat[slot].active         = state;
            s_arrow_repeat[slot].next_repeat_ms = SDL_GetTicks() + KEY_REPEAT_INITIAL_DELAY_MS;
        }
    }

    // Backspace and tab don't generate SDL_TEXTINPUT events, but the example
    // application expects them as keyboard (ASCII) events too.
    if (state) {
        if (sdl_kc == SDLK_BACKSPACE) {
            send_keyboard_event('\b', "\b", modifiers);
        } else if (sdl_kc == SDLK_TAB) {
            send_keyboard_event('\t', "\t", modifiers);
        }
    }
}

static void handle_text_input_event(SDL_TextInputEvent const* text_event) {
    uint32_t      modifiers = map_modifiers(SDL_GetModState());
    unsigned char first     = (unsigned char)text_event->text[0];
    char          ascii     = (first != 0 && first < 0x80) ? (char)first : 0;
    send_keyboard_event(ascii, text_event->text, modifiers);
}

static void pump_sdl_events(void) {
    SDL_Event sdl_event;
    while (SDL_PollEvent(&sdl_event)) {
        switch (sdl_event.type) {
            case SDL_QUIT:
                ESP_LOGI(TAG, "Window close requested, exiting simulator");
                exit(0);
                break;
            case SDL_KEYDOWN:
            case SDL_KEYUP:
                handle_key_event(&sdl_event.key);
                break;
            case SDL_TEXTINPUT:
                handle_text_input_event(&sdl_event.text);
                break;
            default:
                break;
        }
    }
    update_key_repeat();
}

esp_err_t bsp_input_internal_init(void) {
    if (s_queue != NULL) {
        return ESP_OK;
    }

    s_queue = xQueueCreate(64, sizeof(bsp_input_event_t));
    if (s_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    freertos_compat_set_idle_hook(pump_sdl_events);

    return ESP_OK;
}

esp_err_t bsp_input_get_queue(QueueHandle_t* out_queue) {
    if (out_queue == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_queue == NULL) {
        return ESP_FAIL;
    }
    *out_queue = s_queue;
    return ESP_OK;
}

bool bsp_input_needs_on_screen_keyboard(void) {
    return false;
}

esp_err_t bsp_input_get_backlight_brightness(uint8_t* out_percentage) {
    if (out_percentage == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_percentage = s_backlight_percentage;
    return ESP_OK;
}

esp_err_t bsp_input_set_backlight_brightness(uint8_t percentage) {
    s_backlight_percentage = percentage;
    return ESP_OK;
}

esp_err_t bsp_input_read_navigation_key(bsp_input_navigation_key_t key, bool* out_state) {
    if (out_state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_state = ((size_t)key < (sizeof(s_navigation_state) / sizeof(s_navigation_state[0]))) &&
                 s_navigation_state[key];
    return ESP_OK;
}

esp_err_t bsp_input_read_scancode(bsp_input_scancode_t key, bool* out_state) {
    if (out_state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_state = s_scancode_state[key & 0xFFFF];
    return ESP_OK;
}

esp_err_t bsp_input_read_action(bsp_input_action_type_t action, bool* out_state) {
    (void)action;
    if (out_state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    // No SD card, audio jack or power button hardware exists on the desktop
    // simulator.
    *out_state = false;
    return ESP_OK;
}

esp_err_t bsp_input_get_touch_coordinates(uint16_t* out_x, uint16_t* out_y, uint16_t* out_strength,
                                          uint8_t* out_count, uint8_t max_count) {
    (void)out_x;
    (void)out_y;
    (void)out_strength;
    (void)max_count;
    if (out_count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_count = 0;
    return ESP_ERR_NOT_SUPPORTED;
}

int bsp_input_hook_register(bsp_input_hook_cb_t callback, void* user_data) {
    if (callback == NULL) {
        return -1;
    }
    for (int i = 0; i < MAX_HOOKS; i++) {
        if (!s_hooks[i].used) {
            s_hooks[i].used      = true;
            s_hooks[i].callback  = callback;
            s_hooks[i].user_data = user_data;
            return i;
        }
    }
    return -1;
}

void bsp_input_hook_unregister(int hook_id) {
    if (hook_id < 0 || hook_id >= MAX_HOOKS) {
        return;
    }
    s_hooks[hook_id].used      = false;
    s_hooks[hook_id].callback  = NULL;
    s_hooks[hook_id].user_data = NULL;
}

esp_err_t bsp_input_inject_event(bsp_input_event_t* event) {
    if (event == NULL || s_queue == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xQueueSend(s_queue, event, pdMS_TO_TICKS(10)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}
