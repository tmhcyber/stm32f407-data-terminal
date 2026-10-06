#include "FreeRTOS.h"
BaseType_t xTaskCreate(void (*task)(void *), const char *name, unsigned depth,
                     void *arg, UBaseType_t priority, TaskHandle_t *handle);
void vTaskDelay(TickType_t ticks);
TickType_t xTaskGetTickCount(void);
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t handle);
