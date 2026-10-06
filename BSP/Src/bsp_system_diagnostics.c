#include "bsp_system_diagnostics.h"

#include "stm32f4xx.h"

#define BSP_SYSTEM_DIAGNOSTICS_FAULT_STACK_SIZE  512U

#if (BSP_SYSTEM_DIAGNOSTICS_RAW_RESET_BOR != RCC_CSR_BORRSTF)
#error "BOR reset flag mask differs from the STM32F407 device header"
#endif
#if (BSP_SYSTEM_DIAGNOSTICS_RAW_RESET_PIN != RCC_CSR_PINRSTF)
#error "PIN reset flag mask differs from the STM32F407 device header"
#endif
#if (BSP_SYSTEM_DIAGNOSTICS_RAW_RESET_POR != RCC_CSR_PORRSTF)
#error "POR reset flag mask differs from the STM32F407 device header"
#endif
#if (BSP_SYSTEM_DIAGNOSTICS_RAW_RESET_SOFTWARE != RCC_CSR_SFTRSTF)
#error "Software reset flag mask differs from the STM32F407 device header"
#endif
#if (BSP_SYSTEM_DIAGNOSTICS_RAW_RESET_IWDG != RCC_CSR_IWDGRSTF)
#error "IWDG reset flag mask differs from the STM32F407 device header"
#endif
#if (BSP_SYSTEM_DIAGNOSTICS_RAW_RESET_WWDG != RCC_CSR_WWDGRSTF)
#error "WWDG reset flag mask differs from the STM32F407 device header"
#endif
#if (BSP_SYSTEM_DIAGNOSTICS_RAW_RESET_LOW_POWER != RCC_CSR_LPWRRSTF)
#error "Low-power reset flag mask differs from the STM32F407 device header"
#endif

__attribute__((section("FaultStack"), zero_init, aligned(8), used))
static uint8_t s_fault_emergency_stack[
  BSP_SYSTEM_DIAGNOSTICS_FAULT_STACK_SIZE];

const uint8_t * const g_bsp_system_diagnostics_fault_stack_top =
  &s_fault_emergency_stack[BSP_SYSTEM_DIAGNOSTICS_FAULT_STACK_SIZE];

__attribute__((section("NoInit"), zero_init, aligned(8), used))
static volatile BSP_SystemFaultRecord_t s_retained_fault;

static void BSP_SystemDiagnostics_ClearRetainedPayload(void)
{
  s_retained_fault.checksum = 0U;
  s_retained_fault.version = 0U;
  s_retained_fault.size = 0U;
  s_retained_fault.fault_type = 0U;
  s_retained_fault.exc_return = 0U;
  s_retained_fault.original_sp = 0U;
  s_retained_fault.core_frame_sp = 0U;
  s_retained_fault.frame_valid = 0U;
  s_retained_fault.extended_frame = 0U;
  s_retained_fault.frame_reject_reasons = 0U;
  s_retained_fault.r0 = 0U;
  s_retained_fault.r1 = 0U;
  s_retained_fault.r2 = 0U;
  s_retained_fault.r3 = 0U;
  s_retained_fault.r12 = 0U;
  s_retained_fault.lr = 0U;
  s_retained_fault.pc = 0U;
  s_retained_fault.xpsr = 0U;
  s_retained_fault.cfsr = 0U;
  s_retained_fault.hfsr = 0U;
  s_retained_fault.dfsr = 0U;
  s_retained_fault.afsr = 0U;
  s_retained_fault.mmfar = 0U;
  s_retained_fault.bfar = 0U;
  s_retained_fault.shcsr = 0U;
  s_retained_fault.icsr = 0U;
}

void BSP_SystemDiagnostics_CaptureBootReport(BSP_SystemBootReport_t *report)
{
  BSP_SystemFaultRecord_t retained_copy;

  if (report == NULL)
  {
    return;
  }

  report->reset.captured = 0U;
  report->reset.raw_rcc_csr = RCC->CSR;
  report->reset.reasons = BSP_SystemDiagnostics_DecodeResetReasons(
    report->reset.raw_rcc_csr);
  report->reset.captured = 1U;
  RCC->CSR |= RCC_CSR_RMVF;

  retained_copy = s_retained_fault;
  report->previous_fault_status =
    BSP_SystemDiagnostics_ConsumeFaultRecord(&retained_copy,
                                             &report->previous_fault);
  s_retained_fault.commit = BSP_SYSTEM_DIAGNOSTICS_FAULT_RECORD_INVALID;
  __DMB();
}

void BSP_SystemDiagnostics_EnableConfigurableFaults(void)
{
  SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk |
                SCB_SHCSR_BUSFAULTENA_Msk |
                SCB_SHCSR_USGFAULTENA_Msk;
  __DSB();
  __ISB();
}

void BSP_SystemDiagnostics_CaptureFault(BSP_SystemFaultType_t fault_type,
                                        uint32_t exc_return,
                                        const uint32_t *stack_pointer)
{
  BSP_SystemFaultRecord_t checksum_copy;
  const uint32_t *core_frame;
  uint32_t core_frame_sp;

  __disable_irq();
  s_retained_fault.commit = BSP_SYSTEM_DIAGNOSTICS_FAULT_RECORD_INVALID;
  __DMB();
  BSP_SystemDiagnostics_ClearRetainedPayload();

  s_retained_fault.version = BSP_SYSTEM_DIAGNOSTICS_FAULT_RECORD_VERSION;
  s_retained_fault.size = (uint32_t)sizeof(s_retained_fault);
  s_retained_fault.fault_type = (uint32_t)fault_type;
  s_retained_fault.exc_return = exc_return;
  s_retained_fault.original_sp = (uint32_t)stack_pointer;
  s_retained_fault.extended_frame =
    (uint32_t)((exc_return &
                BSP_SYSTEM_DIAGNOSTICS_EXC_RETURN_BASIC_FRAME) == 0U);

  s_retained_fault.cfsr = SCB->CFSR;
  s_retained_fault.hfsr = SCB->HFSR;
  s_retained_fault.dfsr = SCB->DFSR;
  s_retained_fault.afsr = SCB->AFSR;
  s_retained_fault.mmfar = SCB->MMFAR;
  s_retained_fault.bfar = SCB->BFAR;
  s_retained_fault.shcsr = SCB->SHCSR;
  s_retained_fault.icsr = SCB->ICSR;

  s_retained_fault.frame_reject_reasons =
    BSP_SystemDiagnostics_EvaluateFrame(s_retained_fault.original_sp,
                                         exc_return,
                                         s_retained_fault.cfsr,
                                         &core_frame_sp);
  if (s_retained_fault.frame_reject_reasons ==
      BSP_SYSTEM_DIAGNOSTICS_FRAME_REJECT_NONE)
  {
    core_frame = (const uint32_t *)core_frame_sp;
    s_retained_fault.core_frame_sp = core_frame_sp;
    s_retained_fault.r0 = core_frame[0];
    s_retained_fault.r1 = core_frame[1];
    s_retained_fault.r2 = core_frame[2];
    s_retained_fault.r3 = core_frame[3];
    s_retained_fault.r12 = core_frame[4];
    s_retained_fault.lr = core_frame[5];
    s_retained_fault.pc = core_frame[6];
    s_retained_fault.xpsr = core_frame[7];
    s_retained_fault.frame_valid = 1U;
  }

  __DMB();
  checksum_copy = s_retained_fault;
  s_retained_fault.checksum =
    BSP_SystemDiagnostics_CalculateFaultChecksum(&checksum_copy);
  __DMB();
  s_retained_fault.commit = BSP_SYSTEM_DIAGNOSTICS_FAULT_RECORD_COMMIT;
  __DSB();

  while (1)
  {
    __NOP();
  }
}
