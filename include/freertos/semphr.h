#pragma once

// Minimal desktop stand-in for ESP-IDF/FreeRTOS's freertos/semphr.h.

#include "freertos/FreeRTOS.h"

SemaphoreHandle_t xSemaphoreCreateBinary(void);
SemaphoreHandle_t xSemaphoreCreateMutex(void);
void              vSemaphoreDelete(SemaphoreHandle_t semaphore);

BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticks_to_wait);
BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore);
