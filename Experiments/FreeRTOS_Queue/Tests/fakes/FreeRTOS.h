#ifndef TEST_FREERTOS_H
#define TEST_FREERTOS_H
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
typedef int BaseType_t;
typedef uint32_t TickType_t;
typedef unsigned UBaseType_t;
typedef void *TaskHandle_t;
typedef void *QueueHandle_t;
#define pdPASS 1
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(ms) (ms)
void TestEnterCritical(void);
void TestExitCritical(void);
#define taskENTER_CRITICAL() TestEnterCritical()
#define taskEXIT_CRITICAL() TestExitCritical()
#endif
