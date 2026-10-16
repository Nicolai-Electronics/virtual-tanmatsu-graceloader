#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "guest_mem.h"
#include "handle_table.h"
#include "pax_fonts.h"
#include "trap.h"
#include "trap_list.h"

// Data symbols: a handful of imports apps dereference as plain memory
// rather than call (stdout, and hardware register-block externs like
// USB_SERIAL_JTAG that only matter to real on-device drivers). Each gets a
// real guest-memory slot -- unlike the function table below, these
// addresses are never meant to be fetched as code.
typedef struct {
    char const *name;
    size_t      size;  // backing allocation size
} data_sym_t;

// A data symbol backed directly by a real host address (not a fresh
// guest-memory slot) -- see g_direct_data_syms below.
typedef struct {
    char const *name;
    uintptr_t   addr;
} direct_data_sym_t;

static data_sym_t const g_data_syms[] = {
    {"stdout", 4},              // FILE* slot; patched to a handle_table index below
    {"USB_SERIAL_JTAG", 256},   // register-block externs: zeroed dummy storage is enough,
    {"LP_SYS", 256},            // real on-device driver init paths that would touch
    {"EFUSE", 256},             // these are themselves stubbed out as traps (see
    {"USB_WRAP", 256},          // i2c_i2s_shim.c/esp_shim.c), so nothing here is read.
};
#define DATA_SYM_COUNT_ (sizeof(g_data_syms) / sizeof(g_data_syms[0]))

// pax-gfx's built-in fonts: real, read-only `pax_font_t const` globals that
// apps can import by name and pass straight to the text-drawing calls (or,
// like app_portals.so, read their raw glyph data directly). Unlike the
// zeroed slots above, these point at this host binary's own copy of
// pax-gfx's font data -- correct because the whole emulator is built i386
// (see the big comment at the top of CMakeLists.txt) so pax_font_t's layout
// here is byte-identical to the app's own ILP32 compile, and because this
// is read-only data nothing ever needs a guest-writable slot for it.
static direct_data_sym_t const g_direct_data_syms[] = {
    {"pax_font_sky_raw", (uintptr_t)&pax_font_sky_raw},
    {"pax_font_sky_mono_raw", (uintptr_t)&pax_font_sky_mono_raw},
    {"pax_font_marker_raw", (uintptr_t)&pax_font_marker_raw},
    {"pax_font_saira_condensed_raw", (uintptr_t)&pax_font_saira_condensed_raw},
    {"pax_font_saira_regular_raw", (uintptr_t)&pax_font_saira_regular_raw},
    {"pax_fonts_index", (uintptr_t)&pax_fonts_index},
};
#define DIRECT_DATA_SYM_COUNT_ (sizeof(g_direct_data_syms) / sizeof(g_direct_data_syms[0]))

#define X(name) void trap_##name(cpu_t *cpu);
TRAP_LIST(X)
#undef X

typedef struct {
    char const *name;
    void (*fn)(cpu_t *);
} trap_entry_t;

static trap_entry_t const g_traps[] = {
#define X(name) {#name, trap_##name},
    TRAP_LIST(X)
#undef X
};

#define TRAP_COUNT_ (sizeof(g_traps) / sizeof(g_traps[0]))

size_t trap_count(void) {
    return TRAP_COUNT_;
}

char const *trap_name(size_t index) {
    return g_traps[index].name;
}

uint32_t trap_addr(size_t index) {
    return TRAP_BASE + (uint32_t)index * TRAP_STRIDE;
}

static kbelf_builtin_sym g_syms[TRAP_COUNT_ + DATA_SYM_COUNT_ + DIRECT_DATA_SYM_COUNT_];
static kbelf_builtin_lib g_lib;
static bool              g_lib_built = false;

kbelf_builtin_lib const *trap_builtin_lib(void) {
    if (!g_lib_built) {
        for (size_t i = 0; i < TRAP_COUNT_; i++) {
            g_syms[i].name  = g_traps[i].name;
            g_syms[i].vaddr = trap_addr(i);
        }
        for (size_t i = 0; i < DATA_SYM_COUNT_; i++) {
            void *slot                     = guest_mem_calloc(g_data_syms[i].size, 8);
            g_syms[TRAP_COUNT_ + i].name  = g_data_syms[i].name;
            g_syms[TRAP_COUNT_ + i].vaddr = (size_t)(uintptr_t)slot;
            if (!strcmp(g_data_syms[i].name, "stdout")) {
                *(uint32_t *)slot = handle_alloc(stdout);
            }
        }
        for (size_t i = 0; i < DIRECT_DATA_SYM_COUNT_; i++) {
            g_syms[TRAP_COUNT_ + DATA_SYM_COUNT_ + i].name  = g_direct_data_syms[i].name;
            g_syms[TRAP_COUNT_ + DATA_SYM_COUNT_ + i].vaddr = (size_t)g_direct_data_syms[i].addr;
        }
        g_lib.path        = "liball.so";
        g_lib.symbols_len = TRAP_COUNT_ + DATA_SYM_COUNT_ + DIRECT_DATA_SYM_COUNT_;
        g_lib.symbols      = g_syms;
        g_lib_built        = true;
    }
    return &g_lib;
}

bool cpu_trap_hook(cpu_t *cpu, uint32_t pc) {
    if (pc < TRAP_BASE || pc >= TRAP_END) {
        return false;
    }
    size_t index = (pc - TRAP_BASE) / TRAP_STRIDE;
    if (index >= TRAP_COUNT_) {
        fprintf(stderr, "[trap] call to unmapped trap address 0x%08x\n", pc);
        cpu->halted = true;
        return true;
    }
    g_traps[index].fn(cpu);
    cpu->pc = cpu->x[1];  // "return" to ra, as if the call had returned normally
    return true;
}
