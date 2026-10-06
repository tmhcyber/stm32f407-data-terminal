#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stdint.h>
extern uint32_t SystemCoreClock;
void Experiment_Panic(uint32_t reason);

#define configUSE_PREEMPTION                    1
#define configUSE_TIME_SLICING                   1
#define configCPU_CLOCK_HZ                      SystemCoreClock
#define configTICK_RATE_HZ                      1000U
#define configMAX_PRIORITIES                    4
#define configMINIMAL_STACK_SIZE                128U
#define configTOTAL_HEAP_SIZE                   (12U * 1024U)
#define configMAX_TASK_NAME_LEN                 12
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_MUTEXES                       0
#define configUSE_TIMERS                        0
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configUSE_MALLOC_FAILED_HOOK            1
#define configCHECK_FOR_STACK_OVERFLOW          2
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configSUPPORT_STATIC_ALLOCATION         0
#define configUSE_TRACE_FACILITY                0
#define configUSE_TICKLESS_IDLE                 0
#define configPRIO_BITS                         4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY 15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5
/* ARMCC 5's inline assembler also consumes these: no C integer suffixes. */
#define configKERNEL_INTERRUPT_PRIORITY         (15 << 4)
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    (5 << 4)

#define INCLUDE_vTaskDelay                      1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_uxTaskGetStackHighWaterMark      1

/* Match the existing ST startup vector names. SysTick is wrapped separately. */
#define vPortSVCHandler SVC_Handler
#define xPortPendSVHandler PendSV_Handler
#define configASSERT(condition) do { if (!(condition)) Experiment_Panic(10U); } while (0)

#endif
