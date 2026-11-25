#pragma once
#include <stddef.h>
#include "FreeRTOS.h"
typedef void (*TaskFunction_t)(void *);
TickType_t xTaskGetTickCount(void);
BaseType_t xTaskCreate(TaskFunction_t fn, const char *name, uint32_t stack,
                      void *arg, unsigned priority, void *handle);
