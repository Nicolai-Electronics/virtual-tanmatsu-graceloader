#include "fsroot.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static char g_int_dir[900] = "int";
static char g_sd_dir[900]  = "sd";

void fsroot_init(char const *int_dir, char const *sd_dir) {
    snprintf(g_int_dir, sizeof(g_int_dir), "%s", int_dir);
    snprintf(g_sd_dir, sizeof(g_sd_dir), "%s", sd_dir);
}

static bool has_prefix(char const *path, char const *prefix, size_t prefix_len) {
    return strncmp(path, prefix, prefix_len) == 0 && (path[prefix_len] == '\0' || path[prefix_len] == '/');
}

char const *fsroot_translate(char const *guest_path, char *buf, size_t buflen) {
    if (guest_path == NULL) {
        return guest_path;
    }
    if (has_prefix(guest_path, "/int", 4)) {
        snprintf(buf, buflen, "%s%s", g_int_dir, guest_path + 4);
        return buf;
    }
    if (has_prefix(guest_path, "/sd", 3)) {
        snprintf(buf, buflen, "%s%s", g_sd_dir, guest_path + 3);
        return buf;
    }
    return guest_path;
}
