#include "bsp_modbus_time.h"
#include "stm32f4xx_hal.h"

static TIM_HandleTypeDef s_timer;

uint8_t BSP_ModbusTime_Init(void)
{
  RCC_ClkInitTypeDef clock;
  uint32_t latency;
  uint32_t timer_hz = HAL_RCC_GetPCLK1Freq();
  HAL_RCC_GetClockConfig(&clock, &latency);
  /* STM32F407 RM0090: timer clock doubles when APB prescaler != 1. */
  if (clock.APB1CLKDivider != RCC_HCLK_DIV1) timer_hz *= 2U;
  if ((timer_hz < 1000000U) || ((timer_hz % 1000000U) != 0U) ||
      ((timer_hz / 1000000U) > 65536U)) return 0U;
  __HAL_RCC_TIM2_CLK_ENABLE();
  s_timer.Instance = TIM2;
  s_timer.Init.Prescaler = timer_hz / 1000000U - 1U;
  s_timer.Init.CounterMode = TIM_COUNTERMODE_UP;
  s_timer.Init.Period = 0xFFFFFFFFUL;
  s_timer.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  s_timer.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&s_timer) != HAL_OK) return 0U;
  __HAL_TIM_SET_COUNTER(&s_timer, 0U);
  return (uint8_t)(HAL_TIM_Base_Start(&s_timer) == HAL_OK);
}

uint32_t BSP_ModbusTime_Micros(void)
{
  return __HAL_TIM_GET_COUNTER(&s_timer);
}
