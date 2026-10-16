// Entry point for the x86-64 graceloader emulator: loads a RISC-V32 app.so
// the same way the real on-device graceloader does (via kbelf), but runs it
// through a from-scratch RV32IMAFC interpreter instead of executing it on
// real silicon, with an SDL2 window standing in for the Tanmatsu's display
// and keyboard.

#include <libgen.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define KBELF_REVEAL_PRIVATE
#include "bsp_shim.h"
#include "cpu.h"
#include "freertos/FreeRTOS.h"
#include "fsroot.h"
#include "guest_mem.h"
#include "kbelf.h"

extern void kbelfx_host_init(void);

static void run_init_funcs(cpu_t *cpu, kbelf_dyn dyn, size_t len, kbelf_addr (*get)(kbelf_dyn, size_t)) {
    for (size_t i = 0; i < len; i++) {
        cpu_call_guest(cpu, get(dyn, i), 0, NULL);
    }
}

int main(int argc, char **argv) {
    // Every pointer this emulator hands back to the guest must fit in a
    // 32-bit register (see guest_mem.h); combined with linking non-PIE (see
    // CMakeLists.txt), forcing glibc's malloc to never fall back to mmap for
    // large allocations keeps essentially everything in the low, fixed brk
    // arena instead of high 64-bit mmap'd memory.
    mallopt(M_MMAP_THRESHOLD, 256 * 1024 * 1024);
    mallopt(M_MMAP_MAX, 0);

    char const *app_path = "app.so";
    char const *int_dir  = "int";
    char const *sd_dir    = "sd";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--int") && i + 1 < argc) {
            int_dir = argv[++i];
        } else if (!strcmp(argv[i], "--sd") && i + 1 < argc) {
            sd_dir = argv[++i];
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            fprintf(
                stderr, "usage: %s [--int DIR] [--sd DIR] [app.so]\n"
                        "  --int DIR   host directory to use as the app's \"/int\" (default: ./int)\n"
                        "  --sd DIR    host directory to use as the app's \"/sd\" (default: ./sd)\n",
                argv[0]
            );
            return 0;
        } else {
            app_path = argv[i];
        }
    }

    mkdir(int_dir, 0755);
    mkdir(sd_dir, 0755);
    fsroot_init(int_dir, sd_dir);

    guest_mem_init();
    kbelfx_host_init();
    freertos_compat_mark_main_thread();

    char basepath_buf[1024];
    snprintf(basepath_buf, sizeof(basepath_buf), "%s", app_path);
    bsp_shim_set_install_basepath(dirname(basepath_buf));

    kbelf_dyn dyn = kbelf_dyn_create(0);
    if (dyn == NULL) {
        fprintf(stderr, "failed to create kbelf context\n");
        return 1;
    }
    if (!kbelf_dyn_set_exec(dyn, app_path, NULL)) {
        fprintf(stderr, "failed to open %s\n", app_path);
        return 1;
    }
    if (!kbelf_dyn_load(dyn)) {
        fprintf(stderr, "failed to load/link %s\n", app_path);
        return 1;
    }

    cpu_t cpu;
    cpu_reset(&cpu);

    size_t    stack_size = 1 * 1024 * 1024;
    uintptr_t stack_base = guest_mem_alloc(stack_size, 16);
    cpu.x[REG_SP]         = (uint32_t)(stack_base + stack_size);

    // Build argv for entry(argc, argv, envp): a single element pointing at
    // app_path, NULL-terminated, all living in guest memory.
    size_t    path_len  = strlen(app_path) + 1;
    uintptr_t path_addr = guest_mem_alloc(path_len, 1);
    memcpy((void *)path_addr, app_path, path_len);
    uintptr_t argv_addr                 = guest_mem_alloc(2 * sizeof(uint32_t), sizeof(uint32_t));
    ((uint32_t *)argv_addr)[0]          = (uint32_t)path_addr;
    ((uint32_t *)argv_addr)[1]          = 0;

    run_init_funcs(&cpu, dyn, kbelf_dyn_preinit_len(dyn), kbelf_dyn_preinit_get);
    run_init_funcs(&cpu, dyn, kbelf_dyn_init_len(dyn), kbelf_dyn_init_get);

    cpu.x[REG_A0] = 1;                 // argc
    cpu.x[REG_A1] = (uint32_t)argv_addr;
    cpu.x[REG_A2] = 0;                 // envp
    cpu.x[REG_RA] = CPU_CALL_SENTINEL;
    cpu.pc         = (uint32_t)kbelf_dyn_entrypoint(dyn);

    cpu_run_until_return(&cpu);

    run_init_funcs(&cpu, dyn, kbelf_dyn_fini_len(dyn), kbelf_dyn_fini_get);

    kbelf_dyn_unload(dyn);
    kbelf_dyn_destroy(dyn);
    return 0;
}
