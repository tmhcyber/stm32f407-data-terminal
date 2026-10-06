#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "app_can_service.h"
#include "app_data_service.h"
#include "../../BSP/Src/bsp_can_link_test.c"

CAN_TypeDef test_can2;
uint32_t test_clocks;
static uint32_t calls, fail_call, clock_hz, tx_count, abort_count, box;
static uint32_t pending, length, first_byte, last_tx_id, tx_result;
static uint32_t reset_count;
static uint32_t primask, irq_enabled, clear_pending_count, in_irq;
uint32_t __get_PRIMASK(void) { return primask; }
void __disable_irq(void) { primask = 1U; }
void __set_PRIMASK(uint32_t value) { primask = value; }
void __DMB(void) { assert(primask == 1U); }
void __DSB(void) { }
void __ISB(void) { }
void HAL_NVIC_DisableIRQ(int irq) { assert(irq == CAN2_RX0_IRQn); irq_enabled = 0U; }
void HAL_NVIC_EnableIRQ(int irq) { assert(irq == CAN2_RX0_IRQn); irq_enabled = 1U; }
void HAL_NVIC_ClearPendingIRQ(int irq) { assert(irq == CAN2_RX0_IRQn); ++clear_pending_count; }
void HAL_NVIC_SetPriority(int irq, uint32_t priority, uint32_t subpriority)
{ assert(irq == CAN2_RX0_IRQn && priority == 5U && subpriority == 0U); }
static void dispatch_rx(void)
{
  if (irq_enabled && (pending || (test_can2.RF0R & CAN_FLAG_FOV0))) {
    assert(primask == 0U);
    in_irq = 1U;
    BSP_CAN_LinkTest_RX0_IRQHandler();
    in_irq = 0U;
  }
}
static void process(uint32_t now_ms)
{
  dispatch_rx();
  BSP_CAN_LinkTest_Process(now_ms);
}
void test_can_force_reset(void)
{
  ++reset_count;
  memset(&test_can2, 0, sizeof(test_can2));
  pending = 0U;
}
static HAL_StatusTypeDef status(void)
{ return (++calls == fail_call) ? HAL_ERROR : HAL_OK; }
HAL_StatusTypeDef HAL_RCC_OscConfig(RCC_OscInitTypeDef *o)
{ assert(o->HSEState == RCC_HSE_ON); return status(); }
HAL_StatusTypeDef HAL_RCC_ClockConfig(RCC_ClkInitTypeDef *c, uint32_t l)
{ assert(c->SYSCLKSource == RCC_SYSCLKSOURCE_HSE && l == 0U); return status(); }
uint32_t HAL_RCC_GetPCLK1Freq(void) { return clock_hz; }
void HAL_GPIO_Init(void *p, GPIO_InitTypeDef *g)
{ assert(p == GPIOB && g->Pin == (GPIO_PIN_12 | GPIO_PIN_13)); }
HAL_StatusTypeDef HAL_CAN_Init(CAN_HandleTypeDef *h)
{
  assert(test_clocks == 7U && (h->Instance->MCR & CAN_MCR_INRQ));
  assert(h->Init.Mode == CAN_MODE_NORMAL && h->Init.Prescaler == 5U);
  assert(h->Init.TimeSeg1 == CAN_BS1_7TQ && h->Init.AutoRetransmission == DISABLE);
  return status();
}
HAL_StatusTypeDef HAL_CAN_ConfigFilter(CAN_HandleTypeDef *h, CAN_FilterTypeDef *f)
{
  (void)h;
  assert(f->FilterBank == 14U && f->SlaveStartFilterBank == 14U);
  assert(f->FilterIdHigh == (0x123U << 5) && f->FilterMaskIdLow == 6U);
  return status();
}
HAL_StatusTypeDef HAL_CAN_Start(CAN_HandleTypeDef *h) { (void)h; return status(); }
HAL_StatusTypeDef HAL_CAN_AbortTxRequest(CAN_HandleTypeDef *h, uint32_t m)
{ (void)h; assert(m == (1U << box)); ++abort_count; return HAL_OK; }
HAL_StatusTypeDef HAL_CAN_AddTxMessage(CAN_HandleTypeDef *h, CAN_TxHeaderTypeDef *t, uint8_t d[], uint32_t *m)
{
  assert(in_irq == 0U);
  assert(t->IDE == CAN_ID_STD && t->RTR == CAN_RTR_DATA);
  if (s_request_handler != 0) {
    assert(t->StdId == 0x322U && t->DLC == 5U);
    assert(d[0] == 0U && d[1] == 0U && d[4] == 0U);
    assert(d[5] == 0U && d[6] == 0U && d[7] == 0U);
  } else {
    assert(t->DLC == 2U);
    assert((t->StdId == 0x321U && d[0] == 1U && d[1] == 2U) ||
           (t->StdId == 0x322U && d[0] == 0xA1U && d[1] == 0xA2U));
  }
  last_tx_id = t->StdId; ++tx_count;
  *m = 1U << box; h->Instance->TSR = tx_result << (8U * box);
  return status();
}
uint32_t HAL_CAN_GetRxFifoFillLevel(CAN_HandleTypeDef *h, uint32_t f)
{ (void)h; (void)f; return pending; }
HAL_StatusTypeDef HAL_CAN_GetRxMessage(CAN_HandleTypeDef *h, uint32_t f, CAN_RxHeaderTypeDef *r, uint8_t d[])
{
  assert(in_irq == 1U);
  (void)h; (void)f; --pending;
  r->StdId = 0x123U; r->IDE = CAN_ID_STD; r->RTR = CAN_RTR_DATA; r->DLC = length;
  memset(d, 0, 8); d[0] = (uint8_t)first_byte; d[1] = 2U; return status();
}
static void reset_case(void)
{
  BSP_CAN_LinkReport_t empty = {0};
  g_bsp_can_link_report = empty; g_bsp_can_link_send_request = 0U;
  g_bsp_can_link_recover_request = 0U;
  primask = irq_enabled = clear_pending_count = in_irq = 0U;
  s_rx_enabled = s_rx_failed = 0U;
  g_bsp_can_link_hold_rx_processing = 0U;
  BSP_CAN_QueueClear();
  reset_count = s_recovery_started_at_ms = 0U;
  memset(&s_can, 0, sizeof(s_can)); memset(&test_can2, 0, sizeof(test_can2));
  s_mailbox = s_started_at_ms = 0U;
  BSP_CAN_LinkTest_SetRequestHandler(0);
  test_clocks = calls = fail_call = tx_count = abort_count = box = pending = last_tx_id = 0U;
  clock_hz = 25000000U; length = 2U; first_byte = 1U; tx_result = 3U;
}
static uint8_t observed[8];
static uint32_t observed_count;
static uint8_t observe_request(uint32_t id, uint8_t extended, uint8_t remote,
    uint8_t dlc, const uint8_t *data, uint8_t *response)
{
  assert(in_irq == 0U && id == 0x123U && extended == 0U && remote == 0U);
  assert(dlc == 2U && observed_count < 8U);
  (void)response;
  observed[observed_count++] = data[0];
  return 0U;
}

static void test_rx_irq_queue(void)
{
  uint32_t i;
  reset_case(); BSP_CAN_LinkTest_Init();
  assert(irq_enabled && test_can2.IER == LINK_RX_INTERRUPTS);
  observed_count = 0U;
  BSP_CAN_LinkTest_SetRequestHandler(observe_request);
  /* A main pass cannot read hardware FIFO without an IRQ. */
  pending = 1U; first_byte = 10U;
  BSP_CAN_LinkTest_Process(0U);
  assert(pending == 1U && observed_count == 0U);
  g_bsp_can_link_hold_rx_processing = 1U;
  for (i = 0U; i < 4U; ++i) {
    pending = 1U; first_byte = 10U + i;
    dispatch_rx(); BSP_CAN_LinkTest_Process(i);
  }
  assert(observed_count == 0U && tx_count == 0U && pending == 0U);
  assert(g_bsp_can_link_report.rx_irq_count == 4U);
  assert(g_bsp_can_link_report.rx_enqueued_count == 3U);
  assert(g_bsp_can_link_report.rx_queue_drop_count == 1U);
  assert(g_bsp_can_link_report.rx_count == 0U);
  g_bsp_can_link_hold_rx_processing = 0U;
  BSP_CAN_LinkTest_Process(4U);
  assert(observed_count == 1U && observed[0] == 10U);
  BSP_CAN_LinkTest_Process(5U); BSP_CAN_LinkTest_Process(6U);
  assert(observed_count == 3U && observed[1] == 11U && observed[2] == 12U);
  BSP_CAN_LinkTest_Process(7U); assert(observed_count == 3U);
  /* Sticky overrun is counted/cleared; TX flags are not consumed by RX IRQ. */
  test_can2.TSR = 3U; test_can2.RF0R = CAN_FLAG_FOV0;
  dispatch_rx(); dispatch_rx();
  assert(g_bsp_can_link_report.rx_fifo_overrun_count == 1U);
  assert(test_can2.RF0R == 0U && test_can2.TSR == 3U);
  /* Model continuing arrivals: per-invocation work remains bounded to 3. */
  pending = 5U; dispatch_rx();
  assert(pending == 2U);
  dispatch_rx(); assert(pending == 0U);
  assert(g_bsp_can_link_report.rx_queue_drop_count == 3U);
  Link_Fail(BSP_CAN_LINK_ERROR_TX);
  assert(irq_enabled == 0U && test_can2.IER == 0U);
  assert(g_bsp_can_link_hold_rx_processing == 0U);
  pending = 2U; dispatch_rx(); assert(pending == 2U);
  g_bsp_can_link_recover_request = 1U; process(10U);
  assert(pending == 0U && irq_enabled == 0U);
  test_can2.MSR = CAN_MSR_INAK; process(11U);
  assert(irq_enabled == 0U);
  test_can2.MSR = 0U; process(12U);
  assert(irq_enabled == 1U && g_bsp_can_link_report.recovery_count == 1U);
  process(13U); assert(observed_count == 3U); /* No queued old work replay. */
  pending = 1U; first_byte = 99U; process(14U);
  assert(observed_count == 4U && observed[3] == 99U);
  assert(g_bsp_can_link_report.rx_queue_drop_count == 3U);
  assert(clear_pending_count > 0U);

  reset_case(); BSP_CAN_LinkTest_Init();
  pending = 1U; fail_call = calls + 1U; dispatch_rx();
  assert(irq_enabled == 0U && s_rx_failed == 1U);
  assert(g_bsp_can_link_report.rx_read_error_count == 1U);
  assert(g_bsp_can_link_report.state == BSP_CAN_LINK_READY); /* Deferred fault. */
  BSP_CAN_LinkTest_Process(0U);
  assert(g_bsp_can_link_report.state == BSP_CAN_LINK_FAILED);
  assert(g_bsp_can_link_report.error == BSP_CAN_LINK_ERROR_RX);
}
int main(void)
{
  uint32_t i;
  const BSP_CAN_LinkError_t errors[] = {BSP_CAN_LINK_ERROR_CLOCK,
    BSP_CAN_LINK_ERROR_CLOCK, BSP_CAN_LINK_ERROR_INIT,
    BSP_CAN_LINK_ERROR_FILTER, BSP_CAN_LINK_ERROR_START};
  for (i = 0; i < 3U; ++i) {
    reset_case(); box = i; BSP_CAN_LinkTest_Init();
    process(0U); assert(tx_count == 0U); /* No unsolicited TX. */
    g_bsp_can_link_send_request = 1U; process(1U);
    process(2U);
    assert(g_bsp_can_link_report.tx_ok_count == 1U && last_tx_id == 0x321U);
    pending = 1U; process(3U); process(4U);
    assert(g_bsp_can_link_report.rx_match_count == 1U && last_tx_id == 0x322U);
    assert(g_bsp_can_link_report.tx_ok_count == 2U);
    BSP_CAN_LinkTest_Init(); process(500U); assert(tx_count == 2U);
  }
  for (i = 0; i < 5U; ++i) {
    reset_case(); fail_call = i + 1U; BSP_CAN_LinkTest_Init();
    assert(g_bsp_can_link_report.error == errors[i]);
    process(0U); assert(tx_count == 0U);
  }
  reset_case(); clock_hz = 16000000U; BSP_CAN_LinkTest_Init();
  assert(g_bsp_can_link_report.error == BSP_CAN_LINK_ERROR_CLOCK && test_clocks == 0U);
  reset_case(); BSP_CAN_LinkTest_Init(); fail_call = 6U;
  g_bsp_can_link_send_request = 1U; process(0U);
  assert(g_bsp_can_link_report.error == BSP_CAN_LINK_ERROR_SUBMIT);
  reset_case(); BSP_CAN_LinkTest_Init(); fail_call = 6U; pending = 1U;
  process(0U); assert(g_bsp_can_link_report.error == BSP_CAN_LINK_ERROR_RX);
  reset_case(); BSP_CAN_LinkTest_Init(); tx_result = 1U;
  g_bsp_can_link_send_request = 1U; process(0U); process(1U);
  assert(g_bsp_can_link_report.error == BSP_CAN_LINK_ERROR_TX && abort_count == 1U);
  reset_case(); BSP_CAN_LinkTest_Init(); tx_result = 0U;
  g_bsp_can_link_send_request = 1U; process(UINT32_MAX - 50U);
  process(48U); assert(g_bsp_can_link_report.state == BSP_CAN_LINK_WAIT_TX);
  process(49U);
  assert(g_bsp_can_link_report.error == BSP_CAN_LINK_ERROR_TX_TIMEOUT && abort_count == 1U);
  g_bsp_can_link_send_request = 1U; process(200U); assert(tx_count == 1U);
  reset_case(); BSP_CAN_LinkTest_Init(); pending = 1U; length = 8U;
  process(0U); assert(pending == 0U && g_bsp_can_link_report.invalid_count == 1U);
  pending = 1U; length = 2U; first_byte = 0U; process(1U);
  assert(g_bsp_can_link_report.invalid_count == 2U && tx_count == 0U);
  reset_case(); BSP_CAN_LinkTest_Init(); tx_result = 0U;
  g_bsp_can_link_send_request = 1U; process(0U);
  pending = 1U; process(1U);
  assert(g_bsp_can_link_report.busy_count == 1U && tx_count == 1U);
  reset_case(); BSP_CAN_LinkTest_Init(); App_DataService_Init();
  BSP_CAN_LinkTest_SetRequestHandler(App_CAN_HandleRequest);
  pending = 1U; length = 2U; process(0U);
  assert(tx_count == 0U); /* old fixed request must be rejected */
  pending = 1U; length = 1U; process(1U); process(2U);
  assert(tx_count == 1U && g_bsp_can_link_report.tx_ok_count == 1U);
  /* Manual recovery retains diagnostics and never replays stale work. */
  reset_case(); BSP_CAN_LinkTest_Init(); tx_result = 1U;
  test_can2.ESR = 0x00080030U;
  g_bsp_can_link_send_request = 1U;
  process(0U); process(1U);
  assert(g_bsp_can_link_report.failure_count == 1U);
  pending = 3U; g_bsp_can_link_send_request = 1U;
  g_bsp_can_link_recover_request = 1U; process(2U);
  assert(reset_count == 1U && pending == 0U && g_bsp_can_link_send_request == 0U);
  assert(g_bsp_can_link_report.state == BSP_CAN_LINK_RECOVER_INIT);
  test_can2.MSR = CAN_MSR_INAK;
  process(3U);
  assert(g_bsp_can_link_report.state == BSP_CAN_LINK_RECOVER_START);
  test_can2.MSR = 0U; process(4U);
  assert(g_bsp_can_link_report.state == BSP_CAN_LINK_READY);
  assert(g_bsp_can_link_report.recovery_count == 1U && tx_count == 1U);
  assert(g_bsp_can_link_report.last_failure_esr == 0x00080030U);
  assert(g_bsp_can_link_report.last_failure_error == BSP_CAN_LINK_ERROR_TX);
  assert(g_bsp_can_link_report.error == BSP_CAN_LINK_ERROR_NONE);
  tx_result = 3U; App_DataService_Init();
  BSP_CAN_LinkTest_SetRequestHandler(App_CAN_HandleRequest);
  pending = 1U; length = 1U; process(5U); process(6U);
  assert(g_bsp_can_link_report.submitted_count == 2U && g_bsp_can_link_report.tx_ok_count == 1U);
  g_bsp_can_link_recover_request = 1U; process(7U);
  assert(reset_count == 1U); /* READY ignores an accidental recovery request. */
  /* Recovery Init timeout across tick rollover; no automatic retry. */
  reset_case(); BSP_CAN_LinkTest_Init(); Link_Fail(BSP_CAN_LINK_ERROR_TX);
  g_bsp_can_link_recover_request = 1U; process(UINT32_MAX - 50U);
  process(48U); assert(g_bsp_can_link_report.state == BSP_CAN_LINK_RECOVER_INIT);
  process(49U); assert(g_bsp_can_link_report.state == BSP_CAN_LINK_FAILED);
  process(1000U); assert(reset_count == 1U && g_bsp_can_link_report.recovery_count == 0U);
  /* Each HAL reconfiguration failure and hardware Start timeout stays failed. */
  for (i = 0U; i < 3U; ++i) {
    reset_case(); BSP_CAN_LinkTest_Init(); Link_Fail(BSP_CAN_LINK_ERROR_TX);
    g_bsp_can_link_recover_request = 1U; process(0U);
    fail_call = calls + i + 1U; test_can2.MSR = CAN_MSR_INAK;
    process(1U); test_can2.MSR = 0U; process(2U);
    assert(g_bsp_can_link_report.state == BSP_CAN_LINK_FAILED && g_bsp_can_link_report.recovery_count == 0U);
  }
  reset_case(); BSP_CAN_LinkTest_Init(); Link_Fail(BSP_CAN_LINK_ERROR_TX);
  g_bsp_can_link_recover_request = 1U; process(0U);
  test_can2.MSR = CAN_MSR_INAK; process(1U); process(101U);
  assert(g_bsp_can_link_report.state == BSP_CAN_LINK_FAILED && g_bsp_can_link_report.error == BSP_CAN_LINK_ERROR_START);
  test_rx_irq_queue();
  puts("PASS: CAN transport/recovery and RX IRQ queue: deferred business, bounded drain, overflow/drop, FIFO order, masked failure and stale-work cleanup (fake HAL).");
  return 0;
}
