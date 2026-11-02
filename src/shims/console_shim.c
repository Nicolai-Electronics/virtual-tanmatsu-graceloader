// The debug console: USB Serial/JTAG, backed by this process's stdin.
//
// WHY THIS IS NOT A STUB. Every app with a test kit (main/testkit/debugcon.c)
// listens for commands -- "perf scene=title secs=20", "shots scene=map
// ms=0,2500", "EXIT" -- on the USB-serial/JTAG peripheral, and writes its
// CRC-framed records to stdout. On a badge that is how a host runs a whole
// build -> install -> run -> test cycle with nobody at the device, and the
// records already come out of this emulator on stdout (report.c uses fwrite),
// so the only half that was missing was the way in: install used to answer
// ESP_ERR_NOT_SUPPORTED, every app logged "no debug commands" and carried on.
//
// stdin is polled, never blocked. read_bytes() is called from the app's own
// console task -- a real host thread, see the comment at the top of
// freertos_shim.c -- with a timeout, and returns 0 when nothing arrived in
// that time, exactly as the real driver does. Once stdin is at EOF (a piped
// script that has finished, or /dev/null) it stays there, and the call waits
// out its timeout instead of returning 0 as fast as the caller can ask, which
// is the difference between an idle console and one that burns a core.

#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

#include "cpu.h"
#include "esp_err.h"
#include "trap.h"

// portMAX_DELAY arrives as a huge tick count; waiting that long for a command
// nobody is going to type is not what the caller means.
#define MAX_WAIT_MS 5000

static bool s_eof;

void trap_usb_serial_jtag_driver_install(cpu_t *cpu) {
    (void)cpu;
    s_eof = false;
    ret_u32(cpu, ESP_OK);
}

void trap_usb_serial_jtag_read_bytes(cpu_t *cpu) {
    uint8_t  *out        = (uint8_t *)arg_ptr(cpu, 0);
    size_t const want    = (size_t)arg_u32(cpu, 1);
    int32_t const ticks  = arg_i32(cpu, 2);  // FreeRTOS ticks: 1 ms each here

    if (out == NULL || want == 0) {
        ret_u32(cpu, 0);
        return;
    }

    int const wait_ms = (ticks < 0 || ticks > MAX_WAIT_MS) ? MAX_WAIT_MS : (int)ticks;

    if (s_eof) {
        struct timespec ts = {
            .tv_sec  = wait_ms / 1000,
            .tv_nsec = (long)(wait_ms % 1000) * 1000000L,
        };
        nanosleep(&ts, NULL);
        ret_u32(cpu, 0);
        return;
    }

    for (;;) {
        struct pollfd pfd = {.fd = STDIN_FILENO, .events = POLLIN};
        int const     res = poll(&pfd, 1, wait_ms);
        if (res == 0) {
            ret_u32(cpu, 0);  // timed out, as the real driver does
            return;
        }
        if (res > 0) {
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        s_eof = true;
        ret_u32(cpu, 0);
        return;
    }

    ssize_t const n = read(STDIN_FILENO, out, want);
    if (n <= 0) {
        if (n == 0) {
            s_eof = true;  // a pipe that has run out
        }
        ret_u32(cpu, 0);
        return;
    }
    ret_u32(cpu, (uint32_t)n);
}

void trap_usb_serial_jtag_wait_tx_done(cpu_t *cpu) {
    (void)cpu;
    ret_u32(cpu, ESP_OK);
}
