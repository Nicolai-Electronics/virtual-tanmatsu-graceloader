// FreeRTOS task/queue/notification shims.
//
// app.so was compiled against real ESP-IDF FreeRTOS headers, where
// xQueueSend/xSemaphoreTake/xSemaphoreCreateBinary/etc. are macros that
// expand to calls to the "Generic"/internal entry points below -- those
// (not the friendly macro names) are what actually show up as undefined
// symbols. This file adapts them onto the simple queue/semaphore compat in
// emulator/src/compat/freertos_queue.c (binary/counting semaphores and
// mutexes are themselves just queues in real FreeRTOS, so everything here
// funnels through QueueHandle_t).
//
// Tasks (xTaskCreatePinnedToCore) are real POSIX threads, each running its
// own cpu_t with its own guest stack but sharing the single guest memory
// region -- see guest_mem.h. This mirrors how FreeRTOS tasks on real
// hardware share one address space but have independent register files and
// stacks.

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cpu.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "guest_mem.h"
#include "trap.h"

// ==== Queues, semaphores, mutexes (all QueueHandle_t under the hood) ========

void trap_xQueueGenericCreate(cpu_t *cpu) {
    UBaseType_t length    = arg_u32(cpu, 0);
    UBaseType_t item_size = arg_u32(cpu, 1);
    // Semaphores/mutexes are created with item_size == 0 (no payload, just a
    // token); our compat ring buffer needs a nonzero slot size.
    if (item_size == 0) {
        item_size = 1;
    }
    ret_ptr(cpu, xQueueCreate(length, item_size));
}

void trap_xQueueCreateMutex(cpu_t *cpu) {
    (void)cpu;
    QueueHandle_t q = xQueueCreate(1, 1);
    if (q != NULL) {
        // A freshly created mutex starts "given" (unlocked): the first take
        // must succeed immediately.
        uint8_t token = 1;
        xQueueSend(q, &token, 0);
    }
    ret_ptr(cpu, q);
}

void trap_xQueueGenericSend(cpu_t *cpu) {
    QueueHandle_t queue = arg_ptr(cpu, 0);
    void const   *item  = arg_ptr(cpu, 1);
    TickType_t    ticks = arg_u32(cpu, 2);
    // arg 3 (xCopyPosition: back/front/overwrite) isn't supported by the
    // ring-buffer compat; apps needing front-insert/overwrite semantics
    // aren't expected here (notifications use xTaskGenericNotify instead).
    uint8_t dummy = 1;
    ret_u32(cpu, (uint32_t)xQueueSend(queue, item != NULL ? item : &dummy, ticks));
}

void trap_xQueueReceive(cpu_t *cpu) {
    ret_u32(cpu, (uint32_t)xQueueReceive(arg_ptr(cpu, 0), arg_ptr(cpu, 1), arg_u32(cpu, 2)));
}

void trap_xQueueSemaphoreTake(cpu_t *cpu) {
    QueueHandle_t queue = arg_ptr(cpu, 0);
    TickType_t    ticks = arg_u32(cpu, 1);
    uint8_t       dummy;
    ret_u32(cpu, (uint32_t)xQueueReceive(queue, &dummy, ticks));
}

void trap_xQueueGiveFromISR(cpu_t *cpu) {
    QueueHandle_t queue               = arg_ptr(cpu, 0);
    BaseType_t   *higher_prio_woken   = (BaseType_t *)arg_ptr(cpu, 1);
    uint8_t       token               = 1;
    if (higher_prio_woken != NULL) {
        *higher_prio_woken = pdFALSE;
    }
    ret_u32(cpu, (uint32_t)xQueueSend(queue, &token, 0));
}

void trap_vQueueDelete(cpu_t *cpu) {
    vQueueDelete(arg_ptr(cpu, 0));
}

// ==== Tick count / delay =====================================================

void trap_xTaskGetTickCount(cpu_t *cpu) {
    ret_u32(cpu, (uint32_t)(esp_timer_get_time() / 1000));
}

void trap_vTaskDelay(cpu_t *cpu) {
    // One tick == ~1ms in this compat layer (pdMS_TO_TICKS is the identity).
    TickType_t ticks = arg_u32(cpu, 0);
    for (TickType_t i = 0; i < ticks; i++) {
        freertos_compat_poll_idle();
    }
}

void trap_xTaskDelayUntil(cpu_t *cpu) {
    // xTaskDelayUntil(&xPreviousWakeTime, xTimeIncrement): absolute version of
    // vTaskDelay(), so a periodic loop keeps its phase instead of drifting by
    // however long the body took. *prev is the guest's own variable; FreeRTOS
    // expects it to be initialised with xTaskGetTickCount() before the first
    // call, and 0 here means "not initialised yet", in which case the target
    // is measured from now.
    TickType_t *prev = (TickType_t *)arg_ptr(cpu, 0);
    TickType_t  inc  = arg_u32(cpu, 1);
    TickType_t  now  = (TickType_t)(esp_timer_get_time() / 1000);
    TickType_t  next = (prev != NULL && *prev != 0) ? *prev + inc : now + inc;
    if (prev != NULL) {
        *prev = next;
    }
    // Signed difference so the comparison survives the tick counter wrapping.
    while ((int32_t)((TickType_t)(esp_timer_get_time() / 1000) - next) < 0) {
        freertos_compat_poll_idle();
    }
    ret_u32(cpu, pdTRUE);
}

// ==== Tasks ===================================================================

typedef struct task_ctrl {
    cpu_t          *cpu;
    pthread_t       thread;
    bool            is_running;
    volatile bool   stop_requested;

    pthread_mutex_t notify_mutex;
    uint32_t        notify_value;
    bool            notified;
} task_ctrl_t;

// The task currently executing on this host thread, so vTaskDelete(NULL) and
// self-notification know which task_ctrl_t they are.
static __thread task_ctrl_t *t_self;

typedef struct {
    task_ctrl_t *tcb;
    uint32_t     entry;
    uint32_t     param;
} task_start_args_t;

static void *task_trampoline(void *arg) {
    task_start_args_t *start = (task_start_args_t *)arg;
    task_ctrl_t        *tcb   = start->tcb;
    t_self                    = tcb;

    cpu_t *cpu = tcb->cpu;
    cpu->x[REG_A0] = start->param;
    cpu->x[REG_RA] = CPU_CALL_SENTINEL;
    cpu->pc         = start->entry;
    free(start);

    while (!cpu->halted && cpu->pc != CPU_CALL_SENTINEL && !tcb->stop_requested) {
        if (!cpu_step(cpu)) {
            cpu->halted = true;
        }
    }
    tcb->is_running = false;
    return NULL;
}

void trap_xTaskCreatePinnedToCore(cpu_t *cpu) {
    uint32_t     entry       = arg_u32(cpu, 0);
    // arg 1 (pcName) ignored.
    uint32_t     stack_words = arg_u32(cpu, 2);  // FreeRTOS stack depth is in words
    uint32_t     param       = arg_u32(cpu, 3);
    // arg 4 (priority), arg 6 (core id) ignored: every task just gets a real
    // host thread and the OS scheduler.
    uint32_t    *out_handle  = (uint32_t *)arg_ptr(cpu, 5);

    size_t stack_size = (size_t)stack_words * 4;
    if (stack_size < 16 * 1024) {
        stack_size = 16 * 1024;
    }
    uintptr_t stack_base = guest_mem_alloc(stack_size, 16);

    task_ctrl_t *tcb = guest_mem_calloc(sizeof(task_ctrl_t), _Alignof(task_ctrl_t));
    tcb->cpu          = calloc(1, sizeof(cpu_t));
    cpu_reset(tcb->cpu);
    tcb->cpu->x[REG_SP] = (uint32_t)(stack_base + stack_size);
    tcb->is_running      = true;
    pthread_mutex_init(&tcb->notify_mutex, NULL);

    task_start_args_t *start = malloc(sizeof(task_start_args_t));
    start->tcb               = tcb;
    start->entry              = entry;
    start->param              = param;

    if (pthread_create(&tcb->thread, NULL, task_trampoline, start) != 0) {
        fprintf(stderr, "[freertos] xTaskCreatePinnedToCore: pthread_create failed\n");
        ret_u32(cpu, pdFAIL);
        return;
    }
    pthread_detach(tcb->thread);

    if (out_handle != NULL) {
        *out_handle = (uint32_t)(uintptr_t)tcb;
    }
    ret_u32(cpu, pdPASS);
}

void trap_vTaskDelete(cpu_t *cpu) {
    task_ctrl_t *target = (task_ctrl_t *)arg_ptr(cpu, 0);
    if (target == NULL) {
        target = t_self;
    }
    if (target == NULL) {
        // Deleting the top-level app task (never created via
        // xTaskCreatePinnedToCore, so it has no task_ctrl_t) -- stop the cpu
        // that's running it.
        cpu->halted = true;
        return;
    }
    if (target == t_self) {
        cpu->halted = true;
    } else {
        target->stop_requested = true;
    }
}

// ==== Task notifications ====================================================
// Modeled as a single counter + "has a pending notification" flag per task,
// which covers the common eIncrement/ulTaskGenericNotifyTake
// counting-or-binary-semaphore pattern.

enum { eNoAction = 0, eSetBits = 1, eIncrement = 2, eSetValueWithOverwrite = 3, eSetValueWithoutOverwrite = 4 };

void trap_xTaskGenericNotify(cpu_t *cpu) {
    task_ctrl_t *target = (task_ctrl_t *)arg_ptr(cpu, 0);
    // arg 1 (notification index) ignored: only index 0 is used by apps here.
    uint32_t     value  = arg_u32(cpu, 2);
    uint32_t     action = arg_u32(cpu, 3);
    uint32_t    *prev_out = (uint32_t *)arg_ptr(cpu, 4);

    if (target == NULL) {
        ret_u32(cpu, pdFAIL);
        return;
    }

    pthread_mutex_lock(&target->notify_mutex);
    uint32_t old = target->notify_value;
    switch (action) {
        case eSetBits: target->notify_value |= value; break;
        case eIncrement: target->notify_value++; break;
        case eSetValueWithOverwrite: target->notify_value = value; break;
        case eSetValueWithoutOverwrite:
            if (target->notified) {
                pthread_mutex_unlock(&target->notify_mutex);
                ret_u32(cpu, pdFAIL);
                return;
            }
            target->notify_value = value;
            break;
        default: break;
    }
    target->notified = true;
    pthread_mutex_unlock(&target->notify_mutex);

    if (prev_out != NULL) {
        *prev_out = old;
    }
    ret_u32(cpu, pdPASS);
}

void trap_ulTaskGenericNotifyTake(cpu_t *cpu) {
    // arg 0 (notification index) ignored.
    bool       clear_on_exit = arg_u32(cpu, 1) != 0;
    TickType_t ticks         = arg_u32(cpu, 2);

    task_ctrl_t *self = t_self;
    if (self == NULL) {
        // The top-level app task has no task_ctrl_t (it's not spawned via
        // xTaskCreatePinnedToCore); it never receives notifications either.
        ret_u32(cpu, 0);
        return;
    }

    for (;;) {
        pthread_mutex_lock(&self->notify_mutex);
        if (self->notify_value > 0) {
            uint32_t result;
            if (clear_on_exit) {
                result              = self->notify_value;
                self->notify_value = 0;
            } else {
                self->notify_value--;
                result = self->notify_value + 1;
            }
            self->notified = self->notify_value > 0;
            pthread_mutex_unlock(&self->notify_mutex);
            ret_u32(cpu, result);
            return;
        }
        pthread_mutex_unlock(&self->notify_mutex);

        if (ticks == 0) {
            ret_u32(cpu, 0);
            return;
        }
        usleep(2000);
        if (ticks != portMAX_DELAY) {
            ticks--;
        }
    }
}

// ==== Static-allocation queue/mutex variants =================================
// Real FreeRTOS uses the caller-supplied storage/StaticQueue_t buffer in
// place of its own heap allocation; apps never poke inside either (a
// QueueHandle_t/SemaphoreHandle_t is opaque to them), so it's equivalent --
// and much simpler -- to just ignore the caller's buffer and allocate our
// own via the same compat as the heap-allocated variants.

void trap_xQueueGenericCreateStatic(cpu_t *cpu) {
    UBaseType_t length    = arg_u32(cpu, 0);
    UBaseType_t item_size = arg_u32(cpu, 1);
    if (item_size == 0) {
        item_size = 1;
    }
    // args 2,3 (pucQueueStorage, pxStaticQueue) ignored; arg 4 (queue type)
    // ignored, same as trap_xQueueGenericCreate().
    ret_ptr(cpu, xQueueCreate(length, item_size));
}

void trap_xQueueCreateMutexStatic(cpu_t *cpu) {
    // arg 0 (queue type), arg 1 (pxStaticQueue) ignored.
    QueueHandle_t q = xQueueCreate(1, 1);
    if (q != NULL) {
        uint8_t token = 1;
        xQueueSend(q, &token, 0);  // starts "given" (unlocked), see trap_xQueueCreateMutex().
    }
    ret_ptr(cpu, q);
}

void trap_xQueueGenericSendFromISR(cpu_t *cpu) {
    QueueHandle_t queue             = arg_ptr(cpu, 0);
    void const   *item              = arg_ptr(cpu, 1);
    BaseType_t   *higher_prio_woken = (BaseType_t *)arg_ptr(cpu, 2);
    // arg 3 (xCopyPosition) ignored, same as trap_xQueueGenericSend().
    if (higher_prio_woken != NULL) {
        *higher_prio_woken = pdFALSE;
    }
    uint8_t dummy = 1;
    ret_u32(cpu, (uint32_t)xQueueSend(queue, item != NULL ? item : &dummy, 0));
}

// ==== Critical sections / yielding ===========================================
// This emulator has no real interrupts to race with (every "ISR"-suffixed
// call here is just a regular trap handler running on whichever host thread
// called it), so these are no-ops/immediate-success rather than real
// scheduler operations.

void trap_vPortYield(cpu_t *cpu) {
    (void)cpu;
}
void trap_vPortYieldFromISR(cpu_t *cpu) {
    (void)cpu;
}
void trap_vPortExitCriticalMultiCore(cpu_t *cpu) {
    (void)cpu;
}
void trap_xPortEnterCriticalTimeout(cpu_t *cpu) {
    (void)cpu;
    ret_u32(cpu, pdTRUE);
}
