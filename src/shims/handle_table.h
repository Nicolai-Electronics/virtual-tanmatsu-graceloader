#pragma once

// Small-integer indirection for opaque host pointers that must be handed
// back to the guest (32-bit registers) but come from allocators we don't
// control the placement of -- namely libc's FILE*/DIR*, which can live
// anywhere in the full 64-bit host address space. (Handles we allocate
// ourselves, like FreeRTOS queues/semaphores, instead live in guest memory
// directly -- see guest_mem_calloc() -- and don't need this table.)

#include <stdint.h>

uint32_t handle_alloc(void *ptr);
void    *handle_get(uint32_t handle);
void     handle_free(uint32_t handle);
