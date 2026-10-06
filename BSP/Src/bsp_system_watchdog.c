#include "bsp_system_watchdog.h"

#include "stm32f4xx_hal.h"

static IWDG_HandleTypeDef s_iwdg_handle;
static uint8_t s_initialized;

static BSP_SystemWatchdogStatus_t BSP_SystemWatchdog_MapStatus(
  HAL_StatusTypeDef hal_status)
{
  switch (hal_status)
  {
    case HAL_OK:
      return BSP_SYSTEM_WATCHDOG_STATUS_OK;

    case HAL_BUSY:
      return BSP_SYSTEM_WATCHDOG_STATUS_HAL_BUSY;

    case HAL_TIMEOUT:
      return BSP_SYSTEM_WATCHDOG_STATUS_HAL_TIMEOUT;

    case HAL_ERROR:
    default:
      return BSP_SYSTEM_WATCHDOG_STATUS_HAL_ERROR;
  }
}

BSP_SystemWatchdogStatus_t BSP_SystemWatchdog_Init(void)
{
  HAL_StatusTypeDef hal_status;

  s_initialized = 0U;
  s_iwdg_handle.Instance = IWDG;
  s_iwdg_handle.Init.Prescaler = IWDG_PRESCALER_32;
  s_iwdg_handle.Init.Reload = BSP_SYSTEM_WATCHDOG_RELOAD_VALUE;

  __HAL_DBGMCU_FREEZE_IWDG();
  hal_status = HAL_IWDG_Init(&s_iwdg_handle);
  if (hal_status == HAL_OK)
  {
    s_initialized = 1U;
  }

  return BSP_SystemWatchdog_MapStatus(hal_status);
}

BSP_SystemWatchdogStatus_t BSP_SystemWatchdog_Refresh(void)
{
  if (s_initialized == 0U)
  {
    return BSP_SYSTEM_WATCHDOG_STATUS_NOT_INITIALIZED;
  }

  return BSP_SystemWatchdog_MapStatus(
    HAL_IWDG_Refresh(&s_iwdg_handle));
}
