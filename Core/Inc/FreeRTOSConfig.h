#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stdint.h>

extern uint32_t SystemCoreClock;
void App_RTOS_AssertFailed(uint32_t line, const char *file);

#define configUSE_PREEMPTION                    1
#define configCPU_CLOCK_HZ                      SystemCoreClock
#define configTICK_RATE_HZ                      1000U
#define configMAX_PRIORITIES                    5
#define configMINIMAL_STACK_SIZE                128U
#define configTOTAL_HEAP_SIZE                   (32U * 1024U)
#define configMAX_TASK_NAME_LEN                 16
#define configUSE_16_BIT_TICKS                   0
#define configUSE_MUTEXES                       0
#define configUSE_TIMERS                        0
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configUSE_MALLOC_FAILED_HOOK            1
#define configCHECK_FOR_STACK_OVERFLOW          2
#define configSUPPORT_DYNAMIC_ALLOCATION         1
#define configUSE_TICKLESS_IDLE                  0
#define configUSE_TASK_NOTIFICATIONS             1

/* Thumb assembly in the RVDS port also uses this threshold. */
#define configPRIO_BITS                          4
#define configKERNEL_INTERRUPT_PRIORITY          (15 << 4)
#define configMAX_SYSCALL_INTERRUPT_PRIORITY     (5 << 4)

#define INCLUDE_vTaskDelay                       1
#define INCLUDE_xTaskDelayUntil                  1
#define INCLUDE_xTaskGetSchedulerState           1
#define INCLUDE_uxTaskGetStackHighWaterMark      1

#define vPortSVCHandler SVC_Handler
#define xPortPendSVHandler PendSV_Handler
#define configASSERT(condition) do { if (!(condition)) App_RTOS_AssertFailed(__LINE__, __FILE__); } while (0)

#endif
