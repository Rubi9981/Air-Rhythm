#pragma once
#include "freertos/FreeRTOS.h"
typedef void* TaskHandle_t;
TickType_t xTaskGetTickCount(void);
void vTaskDelayUntil(TickType_t*, TickType_t);
void vTaskDelay(TickType_t);
BaseType_t xTaskCreatePinnedToCore(void (*)(void*), const char*, uint32_t, void*,
                                   unsigned, TaskHandle_t*, int);
