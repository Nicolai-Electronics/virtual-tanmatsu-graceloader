#pragma once

// Minimal desktop stand-in for ESP-IDF/FreeRTOS's freertos/FreeRTOS.h.
// The SDL target runs on the host OS, not on FreeRTOS, so this provides just
// enough of the API surface for the BSP headers and app_main.c to compile
// and behave correctly: opaque handle types, the handful of macros used by
// callers, and a queue implementation (see freertos/queue.h) backed by a
// mutex-protected ring buffer.

#include <stdint.h>

typedef uint32_t TickType_t;
typedef int      BaseType_t;
typedef unsigned int UBaseType_t;

#define pdTRUE  1
#define pdFALSE 0
#define pdPASS  1
#define pdFAIL  0

#define portMAX_DELAY ((TickType_t)0xFFFFFFFFu)

#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))

typedef struct queue_definition*     QueueHandle_t;
typedef struct semaphore_definition* SemaphoreHandle_t;
typedef struct task_definition*      TaskHandle_t;
typedef struct timer_definition*     TimerHandle_t;

// Desktop-port-only extension (not part of the real FreeRTOS API): the
// blocking queue/semaphore waits below run on whichever thread calls them,
// so this hook lets the SDL event pump run cooperatively from inside those
// waits instead of requiring a dedicated thread for SDL. Set by bsp_input.c.
void freertos_compat_set_idle_hook(void (*hook)(void));

// Desktop-port-only extensions used by the emulator's RISC-V task shim
// (emulator/src/shims/freertos_shim.c): records which host thread is the
// "main" one (the idle hook must only ever run there, see
// freertos_queue.c), and exposes one idle-hook tick for vTaskDelay().
void freertos_compat_mark_main_thread(void);
void freertos_compat_poll_idle(void);
