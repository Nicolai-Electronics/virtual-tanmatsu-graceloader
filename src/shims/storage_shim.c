// bsp_storage_* passthroughs, mirroring what the real graceloader answers.
// There the BSP's own storage init is linked out (storage_shim.c in
// ../tanmatsu-graceloader) because the loader owns the SD card and mounts
// /sd itself, so apps see the card as present-but-not-mounted-by-BSP and any
// attempt to mount it through the BSP is refused. Here /sd is a real host
// directory (see main.c) and the same answers keep apps off the loader's
// mount without changing what they can read and write.

#include <stdio.h>

#include "bsp/storage.h"
#include "cpu.h"
#include "esp_err.h"
#include "trap.h"

void trap_bsp_storage_initialize(cpu_t *cpu) {
    ret_u32(cpu, ESP_OK);
}

void trap_bsp_storage_mount(cpu_t *cpu) {
    (void)cpu;
    ret_u32(cpu, ESP_ERR_NOT_SUPPORTED);
}
void trap_bsp_storage_mount_advanced(cpu_t *cpu) {
    (void)cpu;
    ret_u32(cpu, ESP_ERR_NOT_SUPPORTED);
}

void trap_bsp_storage_get_supported(cpu_t *cpu) {
    bsp_storage_type_t type = (bsp_storage_type_t)arg_i32(cpu, 0);
    ret_u32(cpu, type == BSP_STORAGE_TYPE_INTERNAL || type == BSP_STORAGE_TYPE_SDCARD);
}

void trap_bsp_storage_get_status(cpu_t *cpu) {
    switch ((bsp_storage_type_t)arg_i32(cpu, 0)) {
        case BSP_STORAGE_TYPE_INTERNAL:
            ret_u32(cpu, BSP_STORAGE_STATUS_NOT_SUPPORTED);
            break;
        case BSP_STORAGE_TYPE_SDCARD:
            ret_u32(cpu, BSP_STORAGE_STATUS_NOT_MOUNTED);
            break;
        default:
            ret_u32(cpu, BSP_STORAGE_STATUS_NOT_SUPPORTED);
            break;
    }
}

void trap_bsp_storage_get_mounted(cpu_t *cpu) {
    bool *out = (bool *)arg_ptr(cpu, 0);
    if (out == NULL) {
        ret_u32(cpu, ESP_ERR_INVALID_ARG);
        return;
    }
    if ((bsp_storage_type_t)arg_i32(cpu, 0) != BSP_STORAGE_TYPE_SDCARD) {
        ret_u32(cpu, ESP_ERR_NOT_SUPPORTED);
        return;
    }
    *out = false;
    ret_u32(cpu, ESP_OK);
}

void trap_bsp_storage_get_mountpoint(cpu_t *cpu) {
    char   *out  = (char *)arg_ptr(cpu, 0);
    size_t  size = arg_u32(cpu, 1);
    if (out == NULL) {
        ret_u32(cpu, ESP_ERR_INVALID_ARG);
        return;
    }
    if ((bsp_storage_type_t)arg_i32(cpu, 0) != BSP_STORAGE_TYPE_SDCARD) {
        ret_u32(cpu, ESP_ERR_NOT_SUPPORTED);
        return;
    }
    out[0] = '\0';  // unmounted, so the BSP reports an empty mountpoint
    ret_u32(cpu, ESP_OK);
}

void trap_bsp_storage_unmount(cpu_t *cpu) {
    if ((bsp_storage_type_t)arg_i32(cpu, 0) == BSP_STORAGE_TYPE_SDCARD) {
        ret_u32(cpu, ESP_OK);  // already unmounted, as on the badge
    } else {
        ret_u32(cpu, ESP_ERR_NOT_SUPPORTED);
    }
}

void trap_bsp_storage_format(cpu_t *cpu) {
    if ((bsp_storage_type_t)arg_i32(cpu, 0) == BSP_STORAGE_TYPE_SDCARD) {
        ret_u32(cpu, ESP_ERR_INVALID_STATE);  // not mounted, so nothing to format
    } else {
        ret_u32(cpu, ESP_ERR_NOT_SUPPORTED);
    }
}
