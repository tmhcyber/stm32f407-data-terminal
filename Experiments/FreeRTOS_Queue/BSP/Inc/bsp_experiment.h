#ifndef BSP_EXPERIMENT_H
#define BSP_EXPERIMENT_H

#include <stdint.h>
#include "stm32f4xx_hal.h"

HAL_StatusTypeDef BSP_Experiment_Init(void);
HAL_StatusTypeDef BSP_Experiment_Write(const char *text, uint16_t length);
void Experiment_Panic(uint32_t reason);
extern volatile uint32_t g_experiment_fault;

#endif
