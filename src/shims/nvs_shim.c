// NVS (non-volatile key-value storage) and tanmatsu-settings passthroughs.
// Backed by a small flat file under $HOME/.config/graceloader-emulator so
// settings persist across runs, without pulling in the real (C++) NVS
// implementation.

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "cpu.h"
#include "esp_err.h"
#include "handle_table.h"
#include "trap.h"

typedef struct {
    char     ns[64];
    char     key[64];
    uint32_t value;
} nvs_entry_t;

#define MAX_NVS_ENTRIES 256
static nvs_entry_t g_entries[MAX_NVS_ENTRIES];
static size_t      g_entry_count = 0;
static char        g_nvs_file[1024];
static bool        g_loaded = false;

static void nvs_load(void) {
    if (g_loaded) {
        return;
    }
    g_loaded = true;

    char const *home = getenv("HOME");
    char        dir[900];
    snprintf(dir, sizeof(dir), "%s/.config/graceloader-emulator", home ? home : ".");
    mkdir(dir, 0755);
    snprintf(g_nvs_file, sizeof(g_nvs_file), "%s/nvs.txt", dir);

    FILE *f = fopen(g_nvs_file, "r");
    if (f == NULL) {
        return;
    }
    char ns[64], key[64];
    unsigned val;
    while (g_entry_count < MAX_NVS_ENTRIES && fscanf(f, "%63s %63s %u\n", ns, key, &val) == 3) {
        strcpy(g_entries[g_entry_count].ns, ns);
        strcpy(g_entries[g_entry_count].key, key);
        g_entries[g_entry_count].value = val;
        g_entry_count++;
    }
    fclose(f);
}

static void nvs_save(void) {
    FILE *f = fopen(g_nvs_file, "w");
    if (f == NULL) {
        return;
    }
    for (size_t i = 0; i < g_entry_count; i++) {
        fprintf(f, "%s %s %u\n", g_entries[i].ns, g_entries[i].key, g_entries[i].value);
    }
    fclose(f);
}

static nvs_entry_t *nvs_find(char const *ns, char const *key, bool create) {
    for (size_t i = 0; i < g_entry_count; i++) {
        if (!strcmp(g_entries[i].ns, ns) && !strcmp(g_entries[i].key, key)) {
            return &g_entries[i];
        }
    }
    if (!create || g_entry_count >= MAX_NVS_ENTRIES) {
        return NULL;
    }
    nvs_entry_t *e = &g_entries[g_entry_count++];
    snprintf(e->ns, sizeof(e->ns), "%s", ns);
    snprintf(e->key, sizeof(e->key), "%s", key);
    e->value = 0;
    return e;
}

void trap_nvs_flash_init(cpu_t *cpu) {
    nvs_load();
    ret_u32(cpu, ESP_OK);
}
void trap_nvs_flash_erase(cpu_t *cpu) {
    g_entry_count = 0;
    nvs_save();
    ret_u32(cpu, ESP_OK);
}
void trap_nvs_open(cpu_t *cpu) {
    nvs_load();
    char const *name   = (char const *)arg_ptr(cpu, 0);
    // arg 1 is nvs_open_mode_t (RO/RW), irrelevant here.
    uint32_t   *out     = (uint32_t *)arg_ptr(cpu, 2);
    if (out != NULL) {
        *out = handle_alloc(strdup(name));
    }
    ret_u32(cpu, ESP_OK);
}
void trap_nvs_close(cpu_t *cpu) {
    uint32_t h = arg_u32(cpu, 0);
    free(handle_get(h));
    handle_free(h);
}
void trap_nvs_commit(cpu_t *cpu) {
    nvs_save();
    ret_u32(cpu, ESP_OK);
}

static esp_err_t nvs_get(uint32_t handle, char const *key, uint32_t *out) {
    char const  *ns = handle_get(handle);
    nvs_entry_t *e  = ns ? nvs_find(ns, key, false) : NULL;
    if (e == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    *out = e->value;
    return ESP_OK;
}
static esp_err_t nvs_set(uint32_t handle, char const *key, uint32_t value) {
    char const  *ns = handle_get(handle);
    nvs_entry_t *e  = ns ? nvs_find(ns, key, true) : NULL;
    if (e == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    e->value = value;
    return ESP_OK;
}

void trap_nvs_get_u8(cpu_t *cpu) {
    uint32_t v = 0;
    esp_err_t err = nvs_get(arg_u32(cpu, 0), (char const *)arg_ptr(cpu, 1), &v);
    uint8_t *out = (uint8_t *)arg_ptr(cpu, 2);
    if (err == ESP_OK && out) {
        *out = (uint8_t)v;
    }
    ret_u32(cpu, (uint32_t)err);
}
void trap_nvs_get_u16(cpu_t *cpu) {
    uint32_t v = 0;
    esp_err_t err = nvs_get(arg_u32(cpu, 0), (char const *)arg_ptr(cpu, 1), &v);
    uint16_t *out = (uint16_t *)arg_ptr(cpu, 2);
    if (err == ESP_OK && out) {
        *out = (uint16_t)v;
    }
    ret_u32(cpu, (uint32_t)err);
}
void trap_nvs_get_i32(cpu_t *cpu) {
    uint32_t v = 0;
    esp_err_t err = nvs_get(arg_u32(cpu, 0), (char const *)arg_ptr(cpu, 1), &v);
    int32_t  *out = (int32_t *)arg_ptr(cpu, 2);
    if (err == ESP_OK && out) {
        *out = (int32_t)v;  // the store keeps it unsigned; the sign is the caller's
    }
    ret_u32(cpu, (uint32_t)err);
}
void trap_nvs_set_u8(cpu_t *cpu) {
    ret_u32(cpu, (uint32_t)nvs_set(arg_u32(cpu, 0), (char const *)arg_ptr(cpu, 1), arg_u32(cpu, 2) & 0xFF));
}
void trap_nvs_set_u16(cpu_t *cpu) {
    ret_u32(cpu, (uint32_t)nvs_set(arg_u32(cpu, 0), (char const *)arg_ptr(cpu, 1), arg_u32(cpu, 2) & 0xFFFF));
}
void trap_nvs_set_i32(cpu_t *cpu) {
    ret_u32(cpu, (uint32_t)nvs_set(arg_u32(cpu, 0), (char const *)arg_ptr(cpu, 1), arg_u32(cpu, 2)));
}

// String values: a separate table from the numeric g_entries above (value
// type differs), and in-memory only -- unlike the numeric settings below,
// nothing in this emulator's app set yet needs a string NVS value to
// survive a restart, so there's no flat-file persistence for these.
#define MAX_NVS_STR_ENTRIES 64
#define NVS_STR_MAX_LEN     128
typedef struct {
    char ns[64];
    char key[64];
    char value[NVS_STR_MAX_LEN];
} nvs_str_entry_t;
static nvs_str_entry_t g_str_entries[MAX_NVS_STR_ENTRIES];
static size_t          g_str_entry_count = 0;

static nvs_str_entry_t *nvs_str_find(char const *ns, char const *key, bool create) {
    for (size_t i = 0; i < g_str_entry_count; i++) {
        if (!strcmp(g_str_entries[i].ns, ns) && !strcmp(g_str_entries[i].key, key)) {
            return &g_str_entries[i];
        }
    }
    if (!create || g_str_entry_count >= MAX_NVS_STR_ENTRIES) {
        return NULL;
    }
    nvs_str_entry_t *e = &g_str_entries[g_str_entry_count++];
    snprintf(e->ns, sizeof(e->ns), "%s", ns);
    snprintf(e->key, sizeof(e->key), "%s", key);
    e->value[0] = '\0';
    return e;
}

// Shared by trap_nvs_get_str() and trap_nvs_settings_get_owner_nickname()
// (which, on real hardware, is implemented in terms of nvs_get_str() inside
// liball.so -- see the vendored nvs_settings_helpers.c's
// nvs_settings_get_string()). out_value == NULL queries the required
// length only, matching real nvs_get_str()'s two-call convention.
static esp_err_t nvs_str_get(char const *ns, char const *key, char *out_value, uint32_t *inout_len) {
    nvs_str_entry_t *e = nvs_str_find(ns, key, false);
    if (e == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    uint32_t need = (uint32_t)strlen(e->value) + 1;
    if (out_value == NULL) {
        if (inout_len) {
            *inout_len = need;
        }
        return ESP_OK;
    }
    uint32_t cap = inout_len ? *inout_len : 0;
    if (cap < need) {
        if (inout_len) {
            *inout_len = need;
        }
        return ESP_ERR_INVALID_SIZE;  // approximated: real NVS uses ESP_ERR_NVS_INVALID_LENGTH
    }
    memcpy(out_value, e->value, need);
    if (inout_len) {
        *inout_len = need;
    }
    return ESP_OK;
}
static esp_err_t nvs_str_set(char const *ns, char const *key, char const *value) {
    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_str_entry_t *e = nvs_str_find(ns, key, true);
    if (e == NULL) {
        return ESP_ERR_NO_MEM;
    }
    snprintf(e->value, sizeof(e->value), "%s", value);
    return ESP_OK;
}

void trap_nvs_get_str(cpu_t *cpu) {
    char const *ns   = handle_get(arg_u32(cpu, 0));
    char const *key  = (char const *)arg_ptr(cpu, 1);
    char       *out  = (char *)arg_ptr(cpu, 2);
    uint32_t   *lenp = (uint32_t *)arg_ptr(cpu, 3);
    ret_u32(cpu, (uint32_t)(ns ? nvs_str_get(ns, key, out, lenp) : ESP_ERR_INVALID_ARG));
}
void trap_nvs_set_str(cpu_t *cpu) {
    char const *ns    = handle_get(arg_u32(cpu, 0));
    char const *key    = (char const *)arg_ptr(cpu, 1);
    char const *value  = (char const *)arg_ptr(cpu, 2);
    ret_u32(cpu, (uint32_t)(ns ? nvs_str_set(ns, key, value) : ESP_ERR_INVALID_ARG));
}

// nvs_settings_get_owner_nickname(): on real hardware this convenience
// wrapper lives inside liball.so (tanmatsu-settings component), calling
// nvs_settings_get_string("owner.nickname", default_value, out_value,
// max_length) under the "system" namespace -- replicated here directly
// since apps import the wrapper symbol itself, not the raw NVS calls it's
// built from.
#define OWNER_SETTINGS_NS "system"
void trap_nvs_settings_get_owner_nickname(cpu_t *cpu) {
    char       *out_value     = (char *)arg_ptr(cpu, 0);
    uint32_t    max_length    = arg_u32(cpu, 1);
    char const *default_value = (char const *)arg_ptr(cpu, 2);

    if (out_value == NULL || max_length == 0) {
        ret_u32(cpu, ESP_ERR_INVALID_ARG);
        return;
    }
    uint32_t  len = max_length;
    esp_err_t res = nvs_str_get(OWNER_SETTINGS_NS, "owner.nickname", out_value, &len);
    if (res != ESP_OK && default_value != NULL) {
        snprintf(out_value, max_length, "%s", default_value);
    }
    ret_u32(cpu, (uint32_t)res);
}

// tanmatsu-settings convenience wrappers: a fixed set of shared settings
// apps read/write without opening their own NVS handle. Backed by the same
// flat-file store, under one well-known namespace.
#define SETTINGS_NS "tanmatsu_settings"

static esp_err_t settings_get_u8(char const *key, uint8_t default_value, uint8_t *out) {
    nvs_load();
    nvs_entry_t *e = nvs_find(SETTINGS_NS, key, false);
    *out            = e ? (uint8_t)e->value : default_value;
    return ESP_OK;
}
static esp_err_t settings_set_u8(char const *key, uint8_t value) {
    nvs_load();
    nvs_entry_t *e = nvs_find(SETTINGS_NS, key, true);
    e->value        = value;
    nvs_save();
    return ESP_OK;
}

#define SETTINGS_TRAP(fn_suffix, key, default_value)                                                                 \
    void trap_nvs_settings_get_##fn_suffix(cpu_t *cpu) {                                                             \
        uint8_t v;                                                                                                   \
        esp_err_t err = settings_get_u8(key, default_value, &v);                                                     \
        uint8_t *out  = (uint8_t *)arg_ptr(cpu, 0);                                                                  \
        if (out) *out = v;                                                                                           \
        ret_u32(cpu, (uint32_t)err);                                                                                 \
    }                                                                                                                 \
    void trap_nvs_settings_set_##fn_suffix(cpu_t *cpu) {                                                             \
        ret_u32(cpu, (uint32_t)settings_set_u8(key, (uint8_t)arg_u32(cpu, 0)));                                      \
    }

SETTINGS_TRAP(speaker_volume, "speaker_volume", 60)
SETTINGS_TRAP(headphone_volume, "headphone_volume", 60)
SETTINGS_TRAP(display_brightness, "display_brightness", 80)
SETTINGS_TRAP(keyboard_brightness, "keyboard_brightness", 50)
SETTINGS_TRAP(led_brightness, "led_brightness", 50)
