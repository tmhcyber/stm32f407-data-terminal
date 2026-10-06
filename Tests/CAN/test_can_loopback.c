#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "stm32f4xx_hal.h"
/* Include the actual diagnostic so each case can simulate a fresh MCU reset. */
#include "../../BSP/Src/bsp_can_loopback.c"

CAN_TypeDef test_can2;
uint32_t test_clocks;
static uint32_t clock_hz, pending, mailbox_index, abort_count;
static uint32_t calls, rx_id, rx_dlc;
static int fail_at, tx_mode, force_rejected_rx, bad_payload, lose_rx;
static int rx_configured;

void HAL_GPIO_Init(void *port, GPIO_InitTypeDef *gpio)
{
  assert(port == GPIOB && test_clocks == 7U);
  assert(gpio->Pin == GPIO_PIN_12 && gpio->Mode == GPIO_MODE_AF_PP);
  assert(gpio->Pull == GPIO_PULLUP && gpio->Alternate == GPIO_AF9_CAN2);
  rx_configured = 1;
}

uint32_t HAL_RCC_GetPCLK1Freq(void) { return clock_hz; }
static HAL_StatusTypeDef status(void)
{
  ++calls;
  return ((int)calls == fail_at) ? HAL_ERROR : HAL_OK;
}
HAL_StatusTypeDef HAL_CAN_Init(CAN_HandleTypeDef *h)
{
  assert(test_clocks == 7U && h->Instance == CAN2 && rx_configured);
  assert(h->Init.Mode == CAN_MODE_SILENT_LOOPBACK);
  assert(h->Init.Prescaler == 2U && h->Init.AutoRetransmission == DISABLE);
  /* Cold-start contract: request Init before HAL attempts to leave Sleep.
   * This checks call ordering, not real hardware acknowledgement timing.
   */
  if ((h->Instance->MCR & CAN_MCR_INRQ) == 0U) return HAL_ERROR;
  h->Instance->MCR &= ~CAN_MCR_SLEEP;
  return status();
}
HAL_StatusTypeDef HAL_CAN_ConfigFilter(CAN_HandleTypeDef *h, CAN_FilterTypeDef *f)
{
  (void)h;
  assert(f->FilterBank == 14U && f->SlaveStartFilterBank == 14U);
  assert(f->FilterIdHigh == (0x123U << 5) && f->FilterIdLow == 0U);
  assert(f->FilterMaskIdHigh == (0x7FFU << 5) && f->FilterMaskIdLow == 6U);
  return status();
}
HAL_StatusTypeDef HAL_CAN_Start(CAN_HandleTypeDef *h) { (void)h; return status(); }
HAL_StatusTypeDef HAL_CAN_AbortTxRequest(CAN_HandleTypeDef *h, uint32_t mailbox)
{
  (void)h; assert(mailbox == (1U << mailbox_index)); ++abort_count; return HAL_OK;
}
HAL_StatusTypeDef HAL_CAN_AddTxMessage(CAN_HandleTypeDef *h, CAN_TxHeaderTypeDef *tx, uint8_t data[], uint32_t *mailbox)
{
  HAL_StatusTypeDef result = status();
  assert(tx->DLC == 2U && data[0] == 1U && data[1] == 2U);
  *mailbox = 1U << mailbox_index;
  /* Supply hardware outcomes explicitly; this is not a filter/bus emulator. */
  h->Instance->TSR = (tx_mode == 0) ? 0U :
    ((tx_mode == 1 ? 3U : 1U) << (mailbox_index * 8U));
  pending = (uint32_t)(!lose_rx && (tx->StdId == 0x123U || force_rejected_rx));
  return result;
}
uint32_t HAL_CAN_GetRxFifoFillLevel(CAN_HandleTypeDef *h, uint32_t fifo)
{ (void)h; assert(fifo == 0U); return pending; }
HAL_StatusTypeDef HAL_CAN_GetRxMessage(CAN_HandleTypeDef *h, uint32_t fifo, CAN_RxHeaderTypeDef *rx, uint8_t data[])
{
  (void)h; (void)fifo;
  rx->StdId = rx_id; rx->IDE = 0U; rx->RTR = 0U; rx->DLC = rx_dlc;
  memset(data, 0, 8U); data[0] = (uint8_t)(bad_payload ? 9U : 1U); data[1] = 2U;
  --pending;
  return status();
}
static void reset_case(void)
{
  BSP_CAN_LoopbackReport_t empty = {0};
  g_bsp_can_loopback_report = empty;
  memset(&s_can, 0, sizeof(s_can)); memset(&test_can2, 0, sizeof(test_can2));
  test_can2.MCR = CAN_MCR_SLEEP;
  clock_hz = 16000000U; test_clocks = pending = abort_count = calls = 0U;
  mailbox_index = 0U; s_mailbox = s_started_at_ms = 0U;
  fail_at = 0; tx_mode = 1; force_rejected_rx = bad_payload = lose_rx = 0;
  rx_configured = 0;
  rx_id = 0x123U; rx_dlc = 2U;
}
static void run(uint32_t start)
{
  uint32_t i;
  BSP_CAN_Loopback_Init();
  for (i = 0U; i < 400U; ++i) BSP_CAN_Loopback_Process(start + i);
}
int main(void)
{
  unsigned i;
  const BSP_CAN_LoopbackError_t hal_errors[] = {
    BSP_CAN_LOOPBACK_ERROR_INIT, BSP_CAN_LOOPBACK_ERROR_FILTER,
    BSP_CAN_LOOPBACK_ERROR_START, BSP_CAN_LOOPBACK_ERROR_SUBMIT,
    BSP_CAN_LOOPBACK_ERROR_RX
  };
  for (i = 0U; i < 3U; ++i)
  {
    reset_case(); mailbox_index = i; run(i == 2U ? UINT32_MAX - 50U : 0U);
    assert(g_bsp_can_loopback_report.state == BSP_CAN_LOOPBACK_PASSED);
    assert(g_bsp_can_loopback_report.submitted_count == 3U);
    assert(g_bsp_can_loopback_report.tx_ok_count == 3U);
    assert(g_bsp_can_loopback_report.rx_match_count == 2U);
    assert(g_bsp_can_loopback_report.rejected_count == 1U);
    BSP_CAN_Loopback_Init(); BSP_CAN_Loopback_Process(1000U);
    assert(g_bsp_can_loopback_report.submitted_count == 3U);
  }
  for (i = 0U; i < 5U; ++i)
  {
    reset_case(); fail_at = (int)i + 1; run(0U);
    assert(g_bsp_can_loopback_report.state == BSP_CAN_LOOPBACK_FAILED);
    assert(g_bsp_can_loopback_report.error == hal_errors[i]);
  }
  reset_case(); clock_hz = 8000000U; run(0U);
  assert(g_bsp_can_loopback_report.error == BSP_CAN_LOOPBACK_ERROR_CLOCK && calls == 0U);
  reset_case(); tx_mode = 0; run(UINT32_MAX - 50U);
  assert(g_bsp_can_loopback_report.error == BSP_CAN_LOOPBACK_ERROR_TX_TIMEOUT && abort_count == 1U);
  assert(g_bsp_can_loopback_report.tx_ok_count == 0U);
  reset_case(); tx_mode = 2; run(0U);
  assert(g_bsp_can_loopback_report.error == BSP_CAN_LOOPBACK_ERROR_TX);
  reset_case(); lose_rx = 1; run(0U);
  assert(g_bsp_can_loopback_report.error == BSP_CAN_LOOPBACK_ERROR_RX_TIMEOUT);
  reset_case(); force_rejected_rx = 1; run(0U);
  assert(g_bsp_can_loopback_report.error == BSP_CAN_LOOPBACK_ERROR_UNEXPECTED_RX);
  assert(g_bsp_can_loopback_report.rejected_count == 0U);
  reset_case(); bad_payload = 1; run(0U);
  assert(g_bsp_can_loopback_report.error == BSP_CAN_LOOPBACK_ERROR_UNEXPECTED_RX);
  reset_case(); rx_dlc = 8U; run(0U);
  assert(g_bsp_can_loopback_report.error == BSP_CAN_LOOPBACK_ERROR_UNEXPECTED_RX);
  reset_case(); pending = 1U; run(0U);
  assert(g_bsp_can_loopback_report.error == BSP_CAN_LOOPBACK_ERROR_UNEXPECTED_RX);
  puts("PASS: 16 CAN diagnostic scenarios (production C, fake HAL; no bus simulation).");
  return 0;
}
