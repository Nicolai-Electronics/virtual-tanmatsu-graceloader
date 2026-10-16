# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.13)

# Built as a 32-bit (i386) program, not 64-bit: app.so is a 32-bit (ILP32)
# RISC-V binary, and several pax-gfx structs it shares with this emulator
# directly (not just through accessor functions) -- pax_buf_t above all --
# embed raw pointer fields. Those fields are 4 bytes in the app's own ILP32
# compilation but would be 8 bytes if *our* copy of pax-gfx were compiled
# LP64, silently shifting every field after the first pointer and making
# the app read garbage out of its own structs (see the git history for the
# multi-day debugging session this caused: a `pax_buf_t` written by our
# 64-bit pax_buf_init() landed fields at different offsets than the app's
# own 32-bit-compiled code expected, and overran into the next global
# variable in .bss). Building this whole program i386 instead makes every
# shared struct's layout match exactly, the same way the guest-memory
# identity-mapping trick in guest_mem.h means guest pointers are already
# real host pointers -- both are "stop bridging two different ABIs and
# just make them the same ABI" fixes.
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -m32")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -m32")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -m32")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -m32")

project(graceloader_emulator C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# Point find_package at the 32-bit (multilib) copies of these libraries
# instead of the default 64-bit ones.
set(SDL2_DIR /usr/lib32/cmake/SDL2)
set(ZLIB_ROOT /usr/lib32)

find_package(SDL2 REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)

set(KBELF_DIR ${CMAKE_CURRENT_LIST_DIR}/../lib/kbelf)

# Headers are shared freely across all of this project's own targets (loader,
# interpreter, shims, bsp); simpler than threading PUBLIC/INTERFACE include
# propagation through what turns out to be a circular link-time dependency
# between emu_loader/emu_shims/bsp (see the comment on graceloader-emulator's
# link line below) -- CMake's per-target include propagation only works
# cleanly on a DAG.
include_directories(
    include
    src/loader
    src/riscv
    src/shims
    ${KBELF_DIR}/include
    components/pax-gfx/core/include
    components/pax-gfx/gui/include
    components/pax-codecs/include
    components/pax-codecs/libspng/spng
)

# kbelf is a RISC-V32 ELF loader/linker; it's agnostic to what CPU *runs* the
# ELF once loaded (that's the interpreter in src/riscv/), but its own build
# needs to think of itself as targeting RISC-V32 rather than auto-detecting
# the host (x86-64) -- see KBELF_CROSS in kbelf/src/port/riscv.c and
# KBELF_MACHINE/KBELF_IS_ELF64 in kbelf/include/kbelf/machine.h. This must be
# visible to *every* translation unit that includes kbelf.h, not just
# kbelf's own sources -- kbelfx_host.c (in emu_loader) also reads/writes
# kbelf_segment etc., and a struct-layout mismatch between translation units
# built with different KBELF_IS_ELF64 values corrupts every field after the
# first one that differs in size. A plain add_definitions() (rather than
# target_compile_definitions on just the kbelf target) matches the global
# include_directories() above and reaches every target in this file,
# including the ones in the emu_loader/emu_shims/bsp link cycle.
add_definitions(
    -DKBELF_MACHINE=0xF3    # KBELF_MACHINE_RISCV
    -DKBELF_IS_ELF64=0
    -DKBELF_CROSS
    -DKBELF_RISCV_HOST_RVC=1      # KBELF_RISCV_FLAG_RVC: our interpreter supports RVC
    -DKBELF_RISCV_HOST_FABI=2     # KBELF_RISCV_FLAG_FABI_SINGLE
    -DKBELF_RISCV_HOST_RVE=0
)

add_library(kbelf STATIC
    ${KBELF_DIR}/src/kbelf.c
    ${KBELF_DIR}/src/kbelf_dyn.c
    ${KBELF_DIR}/src/kbelf_file.c
    ${KBELF_DIR}/src/kbelf_inst.c
    ${KBELF_DIR}/src/kbelf_reloc.c
    ${KBELF_DIR}/src/port/riscv.c
)

# Force static: pax-gfx defaults to building itself as a shared library off
# ESP-IDF, but shared libraries are always mmap'd to a randomized high
# address regardless of whether the main executable is PIE -- any pointer
# into pax-gfx's own static data would not be guest-safe (see the big
# comment on graceloader-emulator's link step, below).
set(PAX_STATIC ON)
add_subdirectory(components/pax-gfx)

add_library(pax_codecs STATIC
    components/pax-codecs/src/pax_codecs.c
    components/pax-codecs/libspng/spng/spng.c
)
target_link_libraries(pax_codecs PUBLIC pax_gfx ZLIB::ZLIB)

# Desktop board-support-package + ESP-IDF/FreeRTOS compatibility shims,
# vendored from the companion SDL launcher project (and extended here to
# allocate guest-visible handles from guest memory -- see guest_mem.h).
add_library(bsp STATIC
    src/bsp/bsp_audio.c
    src/bsp/bsp_device.c
    src/bsp/bsp_display.c
    src/bsp/bsp_i2c.c
    src/bsp/bsp_input.c
    src/bsp/bsp_led.c
    src/bsp/bsp_orientation.c
    src/bsp/bsp_power.c
    src/bsp/bsp_rtc.c
    src/bsp/bsp_sensors.c
    src/compat/esp_err.c
    src/compat/esp_timer.c
    src/compat/freertos_queue.c
    src/compat/freertos_semphr.c
)
target_link_libraries(bsp PUBLIC pax_gfx SDL2::SDL2 Threads::Threads)

# The RISC-V32 interpreter core.
add_library(riscv_cpu STATIC
    src/riscv/cpu.c
)

# Guest memory + kbelf platform glue.
add_library(emu_loader STATIC
    src/loader/guest_mem.c
    src/loader/kbelfx_host.c
)

# Host-side implementations of every symbol app.so can import (see
# src/shims/trap_list.h), dispatched to from the interpreter's trap region.
add_library(emu_shims STATIC
    src/shims/trap_table.c
    src/shims/handle_table.c
    src/shims/fsroot.c
    src/shims/libc_shim.c
    src/shims/softfloat_shim.c
    src/shims/math_shim.c
    src/shims/esp_shim.c
    src/shims/nvs_shim.c
    src/shims/fatfs_shim.c
    src/shims/bsp_shim.c
    src/shims/i2c_i2s_shim.c
    src/shims/ppa_shim.c
    src/shims/pax_shim.c
    src/shims/gl_input_shim.c
    src/shims/freertos_shim.c
    src/shims/zlib_shim.c
    src/shims/spng_shim.c
    src/shims/esp_lcd_shim.c
    src/shims/hw_stub_shim.c
)

add_executable(graceloader-emulator
    src/main.c
)

# emu_loader, emu_shims and bsp have a genuine link-time cycle:
# kbelfx_host.c (emu_loader) calls trap_builtin_lib() (emu_shims);
# bsp_shim.c/gl_input_shim.c (emu_shims) call into bsp_*.c (bsp);
# freertos_queue.c/freertos_semphr.c (bsp) call guest_mem_calloc() (emu_loader).
# A start/end-group lets the linker make repeated passes over these archives
# instead of requiring an acyclic link order.
target_link_libraries(graceloader-emulator PRIVATE
    -Wl,--start-group
    emu_loader
    emu_shims
    bsp
    -Wl,--end-group
    riscv_cpu
    kbelf
    pax_gfx
    pax_codecs
    SDL2::SDL2main
    SDL2::SDL2
    Threads::Threads
    ZLIB::ZLIB
    m
)

# Guest registers are 32 bits wide; every pointer this emulator hands back to
# the guest (framebuffers, pax_buf_t allocations, FreeRTOS queue handles, ...)
# must therefore live below the 2 GiB mark. Linking non-PIE puts this
# executable's own code/data (and glibc's brk-based malloc arena, which is
# what nearly all small-to-medium allocations use) at a low fixed address
# instead of a randomized 64-bit one; see main.c for the companion mallopt()
# call that keeps *large* allocations out of high mmap'd memory too.
target_compile_options(graceloader-emulator PRIVATE -fno-pie)
target_link_options(graceloader-emulator PRIVATE -no-pie)
