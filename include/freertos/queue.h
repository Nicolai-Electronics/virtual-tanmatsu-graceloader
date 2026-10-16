#pragma once

// Minimal desktop stand-in for ESP-IDF/FreeRTOS's freertos/queue.h.

#include "freertos/FreeRTOS.h"

QueueHandle_t xQueueCreate(UBaseType_t queue_length, UBaseType_t item_size);
void          vQueueDelete(QueueHandle_t queue);

BaseType_t xQueueSend(QueueHandle_t queue, const void* item, TickType_t ticks_to_wait);
BaseType_t xQueueReceive(QueueHandle_t queue, void* out_item, TickType_t ticks_to_wait);

UBaseType_t uxQueueMessagesWaiting(QueueHandle_t queue);
