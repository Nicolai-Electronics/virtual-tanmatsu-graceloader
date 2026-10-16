#include "freertos/queue.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "guest_mem.h"

// One "tick" is treated as roughly one millisecond, matching pdMS_TO_TICKS.
#define IDLE_STEP_US 2000

struct queue_definition {
    pthread_mutex_t mutex;
    uint8_t*        storage;
    size_t          item_size;
    size_t          capacity;
    size_t          head;
    size_t          count;
};

static void (*s_idle_hook)(void) = NULL;
static pthread_t s_main_thread;
static bool       s_main_thread_set = false;

void freertos_compat_set_idle_hook(void (*hook)(void)) {
    s_idle_hook = hook;
}

// Called once by main.c. The idle hook pumps SDL's event queue, which SDL
// only supports doing from the thread that called SDL_Init() -- so spawned
// FreeRTOS-task threads (see freertos_shim.c's xTaskCreatePinnedToCore) must
// not run it even if *they* end up blocking in a queue/semaphore wait.
void freertos_compat_mark_main_thread(void) {
    s_main_thread     = pthread_self();
    s_main_thread_set = true;
}

static void idle_step(void) {
    if (s_idle_hook != NULL && s_main_thread_set && pthread_equal(pthread_self(), s_main_thread)) {
        s_idle_hook();
    }
    usleep(IDLE_STEP_US);
}

// Exposed for vTaskDelay() (see freertos_shim.c), which needs the same
// "sleep a bit, but keep pumping SDL if we're the main thread" behavior.
void freertos_compat_poll_idle(void) {
    idle_step();
}

QueueHandle_t xQueueCreate(UBaseType_t queue_length, UBaseType_t item_size) {
    if (queue_length == 0 || item_size == 0) {
        return NULL;
    }

    // Allocated in guest memory, not the host heap: this pointer is handed
    // back to the guest as an opaque QueueHandle_t (a 32-bit register value),
    // so it must be a real address below the 2 GiB mark (see guest_mem.h).
    struct queue_definition* queue = guest_mem_calloc(sizeof(struct queue_definition), _Alignof(struct queue_definition));
    if (queue == NULL) {
        return NULL;
    }

    queue->storage = calloc(queue_length, item_size);
    if (queue->storage == NULL) {
        free(queue);
        return NULL;
    }

    queue->item_size = item_size;
    queue->capacity  = queue_length;
    pthread_mutex_init(&queue->mutex, NULL);

    return queue;
}

void vQueueDelete(QueueHandle_t queue) {
    if (queue == NULL) {
        return;
    }
    pthread_mutex_destroy(&queue->mutex);
    free(queue->storage);
    // `queue` itself lives in guest (bump-allocated) memory, not the host
    // heap -- see xQueueCreate() -- so there's nothing to free() here.
}

UBaseType_t uxQueueMessagesWaiting(QueueHandle_t queue) {
    if (queue == NULL) {
        return 0;
    }
    pthread_mutex_lock(&queue->mutex);
    UBaseType_t count = (UBaseType_t)queue->count;
    pthread_mutex_unlock(&queue->mutex);
    return count;
}

BaseType_t xQueueSend(QueueHandle_t queue, const void* item, TickType_t ticks_to_wait) {
    if (queue == NULL || item == NULL) {
        return pdFALSE;
    }

    TickType_t waited = 0;
    for (;;) {
        pthread_mutex_lock(&queue->mutex);
        if (queue->count < queue->capacity) {
            size_t tail = (queue->head + queue->count) % queue->capacity;
            memcpy(queue->storage + tail * queue->item_size, item, queue->item_size);
            queue->count++;
            pthread_mutex_unlock(&queue->mutex);
            return pdTRUE;
        }
        pthread_mutex_unlock(&queue->mutex);

        if (ticks_to_wait == 0) {
            return pdFALSE;
        }
        idle_step();
        if (ticks_to_wait != portMAX_DELAY) {
            waited++;
            if (waited >= ticks_to_wait) {
                return pdFALSE;
            }
        }
    }
}

BaseType_t xQueueReceive(QueueHandle_t queue, void* out_item, TickType_t ticks_to_wait) {
    if (queue == NULL || out_item == NULL) {
        return pdFALSE;
    }

    TickType_t waited = 0;
    for (;;) {
        pthread_mutex_lock(&queue->mutex);
        if (queue->count > 0) {
            memcpy(out_item, queue->storage + queue->head * queue->item_size, queue->item_size);
            queue->head = (queue->head + 1) % queue->capacity;
            queue->count--;
            pthread_mutex_unlock(&queue->mutex);
            return pdTRUE;
        }
        pthread_mutex_unlock(&queue->mutex);

        if (ticks_to_wait == 0) {
            return pdFALSE;
        }
        idle_step();
        if (ticks_to_wait != portMAX_DELAY) {
            waited++;
            if (waited >= ticks_to_wait) {
                return pdFALSE;
            }
        }
    }
}
