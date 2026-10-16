#include "handle_table.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_HANDLES 4096

static pthread_mutex_t s_mutex = PTHREAD_MUTEX_INITIALIZER;
static void            *g_handles[MAX_HANDLES];
static uint32_t          g_next = 1;  // 0 is reserved as "invalid"

uint32_t handle_alloc(void *ptr) {
    pthread_mutex_lock(&s_mutex);
    if (g_next >= MAX_HANDLES) {
        fprintf(stderr, "handle_table: exhausted (%d handles)\n", MAX_HANDLES);
        exit(1);
    }
    uint32_t h         = g_next++;
    g_handles[h]       = ptr;
    pthread_mutex_unlock(&s_mutex);
    return h;
}

void *handle_get(uint32_t handle) {
    if (handle == 0 || handle >= MAX_HANDLES) {
        return NULL;
    }
    pthread_mutex_lock(&s_mutex);
    void *ptr = g_handles[handle];
    pthread_mutex_unlock(&s_mutex);
    return ptr;
}

void handle_free(uint32_t handle) {
    if (handle > 0 && handle < MAX_HANDLES) {
        pthread_mutex_lock(&s_mutex);
        g_handles[handle] = NULL;
        pthread_mutex_unlock(&s_mutex);
    }
}
