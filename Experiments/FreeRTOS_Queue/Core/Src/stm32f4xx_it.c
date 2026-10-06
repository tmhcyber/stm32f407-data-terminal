#include "bsp_experiment.h"
#include "FreeRTOS.h"
#include "task.h"

void xPortSysTickHandler(void);

void SysTick_Handler(void)
{
    /* Both HAL and RTOS use 1 ms here. HAL timeouts must still advance before
     * scheduler start (HSE startup/UART banner), but RTOS lists aren't ready. */
    HAL_IncTick();
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
        xPortSysTickHandler();
}

/* SVC and PendSV come from portable/RVDS/ARM_CM4F via FreeRTOSConfig aliases. */
void HardFault_Handler(void)  { Experiment_Panic(11U); }
void MemManage_Handler(void)  { Experiment_Panic(12U); }
void BusFault_Handler(void)   { Experiment_Panic(13U); }
void UsageFault_Handler(void) { Experiment_Panic(14U); }
void NMI_Handler(void)        { Experiment_Panic(15U); }
