// FatFs (f_opendir/f_readdir/f_closedir) passthroughs, backed by host
// opendir/readdir/closedir. Graceloader mounts /int and /sd as real
// directories on this emulator (see main.c), so paths need no translation.
//
// FF_DIR's real internal layout is opaque/irrelevant here (we don't drive a
// real FAT volume) -- we just need a stable way to associate the app's own
// FF_DIR* (it allocates the struct itself, sized against its own ff.h) with
// a host DIR*, so a flat table keyed by that pointer value is enough; we
// never read or write through it.
//
// FILINFO, by contrast, apps *do* read field-by-field, so this mirrors the
// real ESP-IDF fatfs component's layout for this project's configuration
// (CONFIG_FATFS_MAX_LFN=255, CONFIG_FATFS_API_ENCODING_ANSI_OEM=y, exFAT
// disabled): FSIZE_t/DWORD = uint32_t, TCHAR = char, FF_SFN_BUF = 12,
// FF_LFN_BUF = 255.

#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "cpu.h"
#include "fsroot.h"
#include "trap.h"

typedef struct {
    uint32_t fsize;
    uint16_t fdate;
    uint16_t ftime;
    uint8_t  fattrib;
    char     altname[13];
    char     fname[256];
} filinfo_t;
// Verified by compiling a probe against the real riscv32-esp-elf FILINFO
// (this project's exact ESP-IDF v6.0.2 + sdkconfig_tanmatsu) and reading
// sizeof() back out of the object file's symbol table.
_Static_assert(sizeof(filinfo_t) == 280, "filinfo_t must match FatFs's FILINFO exactly");

#define FA_DIREC 0x10

#define MAX_OPEN_DIRS 32
static struct {
    void *key;
    DIR  *dir;
    char  path[1024];
} g_dirs[MAX_OPEN_DIRS];

static int find_dir_slot(void *key, bool create) {
    for (int i = 0; i < MAX_OPEN_DIRS; i++) {
        if (g_dirs[i].key == key) {
            return i;
        }
    }
    if (!create) {
        return -1;
    }
    for (int i = 0; i < MAX_OPEN_DIRS; i++) {
        if (g_dirs[i].key == NULL) {
            return i;
        }
    }
    return -1;
}

void trap_f_opendir(cpu_t *cpu) {
    void *dp = arg_ptr(cpu, 0);
    char  pathbuf[1024];
    char const *path = fsroot_translate((char const *)arg_ptr(cpu, 1), pathbuf, sizeof(pathbuf));

    int slot = find_dir_slot(dp, true);
    if (slot < 0) {
        ret_u32(cpu, 4);  // FR_TOO_MANY_OPEN_FILES (approximated)
        return;
    }
    DIR *d = opendir(path);
    if (d == NULL) {
        ret_u32(cpu, 4);  // FR_NO_PATH (approximated)
        return;
    }
    g_dirs[slot].key = dp;
    g_dirs[slot].dir = d;
    snprintf(g_dirs[slot].path, sizeof(g_dirs[slot].path), "%s", path);
    ret_u32(cpu, 0);  // FR_OK
}

void trap_f_readdir(cpu_t *cpu) {
    void      *dp  = arg_ptr(cpu, 0);
    filinfo_t *fno = (filinfo_t *)arg_ptr(cpu, 1);

    int slot = find_dir_slot(dp, false);
    if (slot < 0 || fno == NULL) {
        ret_u32(cpu, 3);  // FR_INVALID_OBJECT (approximated)
        return;
    }

    struct dirent *ent;
    for (;;) {
        ent = readdir(g_dirs[slot].dir);
        if (ent == NULL) {
            memset(fno, 0, sizeof(*fno));  // fname[0] == 0 signals end-of-directory
            ret_u32(cpu, 0);               // FR_OK
            return;
        }
        if (strcmp(ent->d_name, ".") != 0 && strcmp(ent->d_name, "..") != 0) {
            break;
        }
    }

    memset(fno, 0, sizeof(*fno));
    snprintf(fno->fname, sizeof(fno->fname), "%s", ent->d_name);

    char full[1200];
    snprintf(full, sizeof(full), "%s/%s", g_dirs[slot].path, ent->d_name);
    struct stat st;
    if (stat(full, &st) == 0) {
        fno->fsize = S_ISDIR(st.st_mode) ? 0 : (uint32_t)st.st_size;
        if (S_ISDIR(st.st_mode)) {
            fno->fattrib |= FA_DIREC;
        }
    }
    ret_u32(cpu, 0);  // FR_OK
}

void trap_f_closedir(cpu_t *cpu) {
    void *dp   = arg_ptr(cpu, 0);
    int   slot = find_dir_slot(dp, false);
    if (slot >= 0) {
        closedir(g_dirs[slot].dir);
        g_dirs[slot].key = NULL;
    }
    ret_u32(cpu, 0);  // FR_OK
}
