#include "bsp_system_timing.h"

#include "stm32f4xx.h"

#define BSP_SYSTEM_TIMING_OVERHEAD_SAMPLES  16U

static uint32_t s_core_clock_hz;
static uint32_t s_read_overhead_cycles;

BSP_SystemTimingStatus_t BSP_SystemTiming_Init(void)
{
  uint32_t completed_at_cycles;
  uint32_t index;
  uint32_t minimum_delta;
  uint32_t started_at_cycles;

  s_core_clock_hz = 0U;
  s_read_overhead_cycles = 0U;

  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0U;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

  if (((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0U) ||
      (SystemCoreClock == 0U))
  {
    return BSP_SYSTEM_TIMING_STATUS_UNAVAILABLE;
  }

  started_at_cycles = DWT->CYCCNT;
  __NOP();
  __NOP();
  __NOP();
  __NOP();
  completed_at_cycles = DWT->CYCCNT;
  if (completed_at_cycles == started_at_cycles)
  {
    return BSP_SYSTEM_TIMING_STATUS_UNAVAILABLE;
  }

  minimum_delta = 0xFFFFFFFFUL;
  for (index = 0U; index < BSP_SYSTEM_TIMING_OVERHEAD_SAMPLES; ++index)
  {
    uint32_t started_at_cycles;
    uint32_t completed_at_cycles;
    uint32_t delta_cycles;

    started_at_cycles = DWT->CYCCNT;
    completed_at_cycles = DWT->CYCCNT;
    delta_cycles = completed_at_cycles - started_at_cycles;
    if (delta_cycles < minimum_delta)
    {
      minimum_delta = delta_cycles;
    }
  }

  s_core_clock_hz = SystemCoreClock;
  s_read_overhead_cycles = minimum_delta;
  return BSP_SYSTEM_TIMING_STATUS_OK;
}

uint32_t BSP_SystemTiming_GetCycles(void)
{
  return DWT->CYCCNT;
}

uint32_t BSP_SystemTiming_GetCoreClockHz(void)
{
  return s_core_clock_hz;
}

uint32_t BSP_SystemTiming_GetReadOverheadCycles(void)
{
  return s_read_overhead_cycles;
}
