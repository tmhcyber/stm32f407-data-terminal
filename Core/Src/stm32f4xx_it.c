#include "main.h"
#include "stm32f4xx_it.h"
#include "bsp_system_diagnostics.h"
#include "bsp_can_link_test.h"

extern const uint8_t * const g_bsp_system_diagnostics_fault_stack_top;

void NMI_Handler(void)
{
}

__asm void Fault_Handler_Common(void)
{
  IMPORT  g_bsp_system_diagnostics_fault_stack_top
  IMPORT  BSP_SystemDiagnostics_CaptureFault
  CPSID   I
  MOV     R1, LR
  TST     R1, #4
  ITE     EQ
  MRSEQ   R2, MSP
  MRSNE   R2, PSP
  LDR     R3, =g_bsp_system_diagnostics_fault_stack_top
  LDR     R3, [R3]
  MSR     MSP, R3
  NOP
  B       BSP_SystemDiagnostics_CaptureFault
}

__asm void HardFault_Handler(void)
{
  MOVS    R0, #1
  B       Fault_Handler_Common
}

__asm void MemManage_Handler(void)
{
  MOVS    R0, #2
  B       Fault_Handler_Common
}

__asm void BusFault_Handler(void)
{
  MOVS    R0, #3
  B       Fault_Handler_Common
}

__asm void UsageFault_Handler(void)
{
  MOVS    R0, #4
  B       Fault_Handler_Common
}

void SVC_Handler(void)
{
}

void DebugMon_Handler(void)
{
}

void PendSV_Handler(void)
{
}

void SysTick_Handler(void)
{
  HAL_IncTick();
}

void CAN2_RX0_IRQHandler(void)
{
  BSP_CAN_LinkTest_RX0_IRQHandler();
}
