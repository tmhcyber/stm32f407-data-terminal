#include "bsp_experiment.h"
#include "app_queue_experiment.h"
#include "FreeRTOS.h"
#include "task.h"

int main(void)
{
    static const char banner[] =
        "RTOSS_BOOT|v=4|kernel=11.1.0|source=sht30|addr=0x44|clock=25000000|tick_hz=1000|queue=1|policy=latest\r\n";
    if (BSP_Experiment_Init() != HAL_OK) Experiment_Panic(1U);
    if (BSP_Experiment_Write(banner, sizeof(banner) - 1U) != HAL_OK)
        Experiment_Panic(2U);
    App_QueueExperiment_Create();
    vTaskStartScheduler();
    Experiment_Panic(6U);
}

void vApplicationMallocFailedHook(void)
{
    Experiment_Panic(8U);
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    (void)name;
    Experiment_Panic(9U);
}
