#include "freertos/semphr.h"

#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

#include "guest_mem.h"

#define IDLE_STEP_US 2000

struct semaphore_definition {
    pthread_mutex_t mutex;
    int             count;
    int             max_count;
};

// Allocated in guest memory, not the host heap: handed back to the guest as
// an opaque SemaphoreHandle_t (a 32-bit register value) -- see guest_mem.h.
static struct semaphore_definition *alloc_sem(void) {
    return guest_mem_calloc(sizeof(struct semaphore_definition), _Alignof(struct semaphore_definition));
}

SemaphoreHandle_t xSemaphoreCreateBinary(void) {
    struct semaphore_definition* sem = alloc_sem();
    if (sem == NULL) {
        return NULL;
    }
    pthread_mutex_init(&sem->mutex, NULL);
    sem->count     = 0;
    sem->max_count = 1;
    return sem;
}

SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    struct semaphore_definition* sem = alloc_sem();
    if (sem == NULL) {
        return NULL;
    }
    pthread_mutex_init(&sem->mutex, NULL);
    sem->count     = 1;
    sem->max_count = 1;
    return sem;
}

void vSemaphoreDelete(SemaphoreHandle_t semaphore) {
    if (semaphore == NULL) {
        return;
    }
    pthread_mutex_destroy(&semaphore->mutex);
    // `semaphore` lives in guest (bump-allocated) memory -- nothing to free.
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticks_to_wait) {
    if (semaphore == NULL) {
        return pdFALSE;
    }

    TickType_t waited = 0;
    for (;;) {
        pthread_mutex_lock(&semaphore->mutex);
        if (semaphore->count > 0) {
            semaphore->count--;
            pthread_mutex_unlock(&semaphore->mutex);
            return pdTRUE;
        }
        pthread_mutex_unlock(&semaphore->mutex);

        if (ticks_to_wait == 0) {
            return pdFALSE;
        }
        usleep(IDLE_STEP_US);
        if (ticks_to_wait != portMAX_DELAY) {
            waited++;
            if (waited >= ticks_to_wait) {
                return pdFALSE;
            }
        }
    }
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore) {
    if (semaphore == NULL) {
        return pdFALSE;
    }
    pthread_mutex_lock(&semaphore->mutex);
    if (semaphore->count < semaphore->max_count) {
        semaphore->count++;
    }
    pthread_mutex_unlock(&semaphore->mutex);
    return pdTRUE;
}
