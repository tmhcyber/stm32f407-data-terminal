#include "FreeRTOS.h"
QueueHandle_t xQueueCreate(UBaseType_t count, UBaseType_t size);
BaseType_t xQueueSend(QueueHandle_t queue, const void *data, TickType_t wait);
BaseType_t xQueueOverwrite(QueueHandle_t queue, const void *data);
BaseType_t xQueueReceive(QueueHandle_t queue, void *data, TickType_t wait);
