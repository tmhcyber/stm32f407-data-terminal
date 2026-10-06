#include "bsp_can_link_test.h"
#include "bsp_can_rx_queue.h"
#include "stm32f4xx_hal.h"

volatile BSP_CAN_LinkReport_t g_bsp_can_link_report;
volatile uint32_t g_bsp_can_link_send_request;
volatile uint32_t g_bsp_can_link_recover_request;
volatile uint32_t g_bsp_can_link_hold_rx_processing;
static CAN_HandleTypeDef s_can;
static uint32_t s_mailbox, s_started_at_ms;
static BSP_CAN_RequestHandler_t s_request_handler;
static uint32_t s_recovery_started_at_ms;
static volatile uint8_t s_rx_enabled, s_rx_failed;
#define LINK_RX_INTERRUPTS (CAN_IT_RX_FIFO0_MSG_PENDING | CAN_IT_RX_FIFO0_OVERRUN)
static void Link_ConfigureController(void);

static void Link_StopRx(void)
{
  HAL_NVIC_DisableIRQ(CAN2_RX0_IRQn);
  __DSB();
  __ISB();
  s_rx_enabled = 0U;
  if (s_can.Instance != 0) CLEAR_BIT(s_can.Instance->IER, LINK_RX_INTERRUPTS);
  HAL_NVIC_ClearPendingIRQ(CAN2_RX0_IRQn);
}

static void Link_StartRx(void)
{
  /* RX remains disabled through init/reset. Clear software before opening IRQ.
   * Frames received after controller start remain valid new FIFO arrivals. */
  BSP_CAN_QueueClear();
  s_rx_failed = 0U;
  HAL_NVIC_ClearPendingIRQ(CAN2_RX0_IRQn);
  HAL_NVIC_SetPriority(CAN2_RX0_IRQn, 5U, 0U);
  s_rx_enabled = 1U;
  SET_BIT(s_can.Instance->IER, LINK_RX_INTERRUPTS);
  HAL_NVIC_EnableIRQ(CAN2_RX0_IRQn);
}

void BSP_CAN_LinkTest_RX0_IRQHandler(void)
{
  uint32_t budget;
  if (s_rx_enabled == 0U) return;
  ++g_bsp_can_link_report.rx_irq_count;
  if (__HAL_CAN_GET_FLAG(&s_can, CAN_FLAG_FOV0) != 0U) {
    ++g_bsp_can_link_report.rx_fifo_overrun_count;
    __HAL_CAN_CLEAR_FLAG(&s_can, CAN_FLAG_FOV0);
  }
  /* Hardware FIFO has three slots. Bound one invocation even if more arrive.
   * Do not call HAL_CAN_IRQHandler: TX completion belongs to main-loop polling.
   * No snapshot, protocol callback, send or wait is permitted here. */
  for (budget = 0U; budget < 3U; ++budget) {
    CAN_RxHeaderTypeDef rx = {0};
    BSP_CAN_QueueFrame_t frame = {0};
    if (HAL_CAN_GetRxFifoFillLevel(&s_can, CAN_RX_FIFO0) == 0U) break;
    if (HAL_CAN_GetRxMessage(&s_can, CAN_RX_FIFO0, &rx, frame.data) != HAL_OK) {
      ++g_bsp_can_link_report.rx_read_error_count;
      s_rx_failed = 1U;
      Link_StopRx(); /* Avoid a repeated IRQ on a FIFO that cannot be read. */
      break;
    }
    frame.id = (rx.IDE == CAN_ID_STD) ? rx.StdId : rx.ExtId;
    frame.is_extended = (uint8_t)(rx.IDE != CAN_ID_STD);
    frame.is_remote = (uint8_t)(rx.RTR != CAN_RTR_DATA);
    frame.dlc = (uint8_t)rx.DLC;
    if (BSP_CAN_QueuePush(frame) == 0U)
      ++g_bsp_can_link_report.rx_queue_drop_count;
    else ++g_bsp_can_link_report.rx_enqueued_count;
  }
}

void BSP_CAN_LinkTest_SetRequestHandler(BSP_CAN_RequestHandler_t handler)
{
  s_request_handler = handler;
}

static void Link_Fail(BSP_CAN_LinkError_t error)
{
  Link_StopRx();
  BSP_CAN_QueueClear();
  g_bsp_can_link_hold_rx_processing = 0U;
  g_bsp_can_link_report.error = error;
  g_bsp_can_link_report.hal_error = s_can.ErrorCode;
  if (s_can.Instance != 0) {
    g_bsp_can_link_report.last_tsr = s_can.Instance->TSR;
    g_bsp_can_link_report.last_esr = s_can.Instance->ESR;
    if (g_bsp_can_link_report.state == BSP_CAN_LINK_WAIT_TX)
      (void)HAL_CAN_AbortTxRequest(&s_can, s_mailbox);
  }
  ++g_bsp_can_link_report.failure_count;
  g_bsp_can_link_report.last_failure_error = error;
  g_bsp_can_link_report.last_failure_tsr = g_bsp_can_link_report.last_tsr;
  g_bsp_can_link_report.last_failure_esr = g_bsp_can_link_report.last_esr;
  g_bsp_can_link_send_request = 0U;
  g_bsp_can_link_report.state = BSP_CAN_LINK_FAILED;
}

void BSP_CAN_LinkTest_Init(void)
{
  RCC_OscInitTypeDef osc = {0};
  RCC_ClkInitTypeDef clk = {0};
  if (g_bsp_can_link_report.state != BSP_CAN_LINK_NOT_RUN) return;

  /* Board schematic Y8=25MHz. Direct HSE, no PLL. HAL updates SysTick
   * and SystemCoreClock before the rest of this project's initialization.
   */
  osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  osc.HSEState = RCC_HSE_ON;
  osc.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
    Link_Fail(BSP_CAN_LINK_ERROR_CLOCK); return;
  }
  clk.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                  RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  clk.SYSCLKSource = RCC_SYSCLKSOURCE_HSE;
  clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
  clk.APB1CLKDivider = RCC_HCLK_DIV1;
  clk.APB2CLKDivider = RCC_HCLK_DIV1;
  if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_0) != HAL_OK) {
    Link_Fail(BSP_CAN_LINK_ERROR_CLOCK); return;
  }
  g_bsp_can_link_report.pclk1_hz = HAL_RCC_GetPCLK1Freq();
  if (g_bsp_can_link_report.pclk1_hz != 25000000U) {
    Link_Fail(BSP_CAN_LINK_ERROR_CLOCK); return;
  }
  Link_ConfigureController();
  if (g_bsp_can_link_report.state == BSP_CAN_LINK_FAILED) return;
  if (HAL_CAN_Start(&s_can) != HAL_OK) {
    Link_Fail(BSP_CAN_LINK_ERROR_START); return;
  }
  g_bsp_can_link_report.state = BSP_CAN_LINK_READY;
  Link_StartRx();
}

static void Link_ConfigureController(void)
{
  GPIO_InitTypeDef gpio = {0};
  CAN_FilterTypeDef filter = {0};
  __HAL_RCC_CAN1_CLK_ENABLE();
  __HAL_RCC_CAN2_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  gpio.Pin = GPIO_PIN_12 | GPIO_PIN_13;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_PULLUP;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  gpio.Alternate = GPIO_AF9_CAN2;
  HAL_GPIO_Init(GPIOB, &gpio);
  s_can.Instance = CAN2;
  /* 25MHz / (5 * (1+7+2)) = 500kbit/s; sample point 80%. */
  s_can.Init.Prescaler = 5U;
  s_can.Init.Mode = CAN_MODE_NORMAL;
  s_can.Init.SyncJumpWidth = CAN_SJW_1TQ;
  s_can.Init.TimeSeg1 = CAN_BS1_7TQ;
  s_can.Init.TimeSeg2 = CAN_BS2_2TQ;
  s_can.Init.AutoRetransmission = DISABLE;
  s_can.Init.AutoBusOff = DISABLE;
  s_can.Init.ReceiveFifoLocked = ENABLE;
  SET_BIT(s_can.Instance->MCR, CAN_MCR_INRQ);
  if (HAL_CAN_Init(&s_can) != HAL_OK) {
    Link_Fail(BSP_CAN_LINK_ERROR_INIT); return;
  }
  filter.SlaveStartFilterBank = 14U;
  filter.FilterBank = 14U;
  filter.FilterMode = CAN_FILTERMODE_IDMASK;
  filter.FilterScale = CAN_FILTERSCALE_32BIT;
  filter.FilterIdHigh = 0x123U << 5;
  filter.FilterMaskIdHigh = 0x7FFU << 5;
  filter.FilterMaskIdLow = 6U;
  filter.FilterFIFOAssignment = CAN_FILTER_FIFO0;
  filter.FilterActivation = ENABLE;
  if (HAL_CAN_ConfigFilter(&s_can, &filter) != HAL_OK) {
    Link_Fail(BSP_CAN_LINK_ERROR_FILTER); return;
  }
}

static void Link_RecoverProcess(uint32_t now_ms)
{
  if (g_bsp_can_link_report.state == BSP_CAN_LINK_RECOVER_INIT) {
    /* Wait across loop passes, before entering HAL's synchronous init.
     * Once INAK=1 and SLAK=0 its acknowledgement waits are already met.
     */
    if ((CAN2->MSR & (CAN_MSR_INAK | CAN_MSR_SLAK)) == CAN_MSR_INAK) {
      Link_ConfigureController();
      if (g_bsp_can_link_report.state == BSP_CAN_LINK_FAILED) return;
      CLEAR_BIT(CAN2->MCR, CAN_MCR_INRQ);
      s_recovery_started_at_ms = now_ms;
      g_bsp_can_link_report.state = BSP_CAN_LINK_RECOVER_START;
    } else if ((uint32_t)(now_ms - s_recovery_started_at_ms) >= 100U)
      Link_Fail(BSP_CAN_LINK_ERROR_INIT);
  } else if (g_bsp_can_link_report.state == BSP_CAN_LINK_RECOVER_START) {
    if ((CAN2->MSR & CAN_MSR_INAK) == 0U) {
      /* HAL bookkeeping/validation after hardware bus synchronization. */
      if (HAL_CAN_Start(&s_can) != HAL_OK) {
        Link_Fail(BSP_CAN_LINK_ERROR_START); return;
      }
      g_bsp_can_link_report.error = BSP_CAN_LINK_ERROR_NONE;
      g_bsp_can_link_report.hal_error = s_can.ErrorCode;
      g_bsp_can_link_report.last_tsr = CAN2->TSR;
      g_bsp_can_link_report.last_esr = CAN2->ESR;
      ++g_bsp_can_link_report.recovery_count;
      g_bsp_can_link_report.state = BSP_CAN_LINK_READY;
      Link_StartRx();
    } else if ((uint32_t)(now_ms - s_recovery_started_at_ms) >= 100U)
      Link_Fail(BSP_CAN_LINK_ERROR_START);
  }
}

static void Link_SendData(uint32_t id, const uint8_t *payload,
    uint8_t dlc, uint32_t now_ms)
{
  CAN_TxHeaderTypeDef tx = {0};
  uint8_t data[8] = {0}; /* HAL reads all 8 bytes, regardless of DLC. */
  uint32_t i;
  tx.StdId = id; tx.IDE = CAN_ID_STD; tx.RTR = CAN_RTR_DATA; tx.DLC = dlc;
  for (i = 0U; i < dlc; ++i) data[i] = payload[i];
  s_can.Instance->TSR = CAN_TSR_RQCP0 | CAN_TSR_RQCP1 | CAN_TSR_RQCP2;
  if (HAL_CAN_AddTxMessage(&s_can, &tx, data, &s_mailbox) != HAL_OK) {
    Link_Fail(BSP_CAN_LINK_ERROR_SUBMIT); return;
  }
  ++g_bsp_can_link_report.submitted_count;
  s_started_at_ms = now_ms;
  g_bsp_can_link_report.state = BSP_CAN_LINK_WAIT_TX;
}

static void Link_Send(uint32_t id, uint8_t first, uint8_t second, uint32_t now_ms)
{
  uint8_t data[2];
  data[0] = first; data[1] = second;
  Link_SendData(id, data, 2U, now_ms);
}

void BSP_CAN_LinkTest_Process(uint32_t now_ms)
{
  uint32_t shift, tsr, i;
  BSP_CAN_QueueFrame_t frame;
  if (g_bsp_can_link_recover_request != 0U) {
    g_bsp_can_link_recover_request = 0U;
    if ((g_bsp_can_link_report.state == BSP_CAN_LINK_FAILED) &&
        (s_can.Instance == CAN2) && (HAL_RCC_GetPCLK1Freq() == 25000000U)) {
      CAN_HandleTypeDef empty = {0};
      ++g_bsp_can_link_report.recovery_attempts;
      g_bsp_can_link_send_request = 0U;
      Link_StopRx();
      BSP_CAN_QueueClear();
      s_rx_failed = 0U;
      g_bsp_can_link_hold_rx_processing = 0U;
      /* Reset only CAN2: discard pending mailboxes, FIFO and error state.
       * CAN1's shared filter block is not reset; bank14 is reconfigured.
       */
      __HAL_RCC_CAN2_FORCE_RESET();
      __HAL_RCC_CAN2_RELEASE_RESET();
      HAL_NVIC_ClearPendingIRQ(CAN2_RX0_IRQn);
      s_can = empty;
      s_can.Instance = CAN2;
      s_mailbox = 0U;
      SET_BIT(CAN2->MCR, CAN_MCR_INRQ);
      CLEAR_BIT(CAN2->MCR, CAN_MCR_SLEEP);
      s_recovery_started_at_ms = now_ms;
      g_bsp_can_link_report.state = BSP_CAN_LINK_RECOVER_INIT;
      return;
    }
  }
  if ((g_bsp_can_link_report.state == BSP_CAN_LINK_RECOVER_INIT) ||
      (g_bsp_can_link_report.state == BSP_CAN_LINK_RECOVER_START)) {
    g_bsp_can_link_send_request = 0U;
    Link_RecoverProcess(now_ms);
    return;
  }
  if ((g_bsp_can_link_report.state == BSP_CAN_LINK_NOT_RUN) ||
      (g_bsp_can_link_report.state == BSP_CAN_LINK_FAILED)) return;
  if (s_rx_failed != 0U) {
    Link_Fail(BSP_CAN_LINK_ERROR_RX); return;
  }
  g_bsp_can_link_report.last_esr = s_can.Instance->ESR;
  if (g_bsp_can_link_report.state == BSP_CAN_LINK_WAIT_TX) {
    shift = (s_mailbox == CAN_TX_MAILBOX0) ? 0U :
            ((s_mailbox == CAN_TX_MAILBOX1) ? 8U : 16U);
    tsr = s_can.Instance->TSR;
    g_bsp_can_link_report.last_tsr = tsr;
    if ((tsr & (CAN_TSR_RQCP0 << shift)) != 0U) {
      if ((tsr & (CAN_TSR_TXOK0 << shift)) == 0U) {
        Link_Fail(BSP_CAN_LINK_ERROR_TX); return;
      }
      s_can.Instance->TSR = CAN_TSR_RQCP0 << shift;
      ++g_bsp_can_link_report.tx_ok_count;
      g_bsp_can_link_report.state = BSP_CAN_LINK_READY;
    } else if ((uint32_t)(now_ms - s_started_at_ms) >= 100U) {
      Link_Fail(BSP_CAN_LINK_ERROR_TX_TIMEOUT); return;
    }
  }
  /* One queued frame per loop; ISR is the only reader of hardware RX FIFO. */
  if ((g_bsp_can_link_hold_rx_processing == 0U) &&
      (BSP_CAN_QueuePop(&frame) != 0U)) {
    ++g_bsp_can_link_report.rx_count;
    g_bsp_can_link_report.last_rx_id = frame.id;
    g_bsp_can_link_report.last_rx_dlc = frame.dlc;
    for (i = 0U; i < 8U; ++i) g_bsp_can_link_report.last_rx_data[i] = frame.data[i];
    if (s_request_handler != 0) {
      uint8_t response[8] = {0};
      uint8_t length = s_request_handler(
          frame.id, frame.is_extended, frame.is_remote, frame.dlc,
          frame.data, response);
      if ((length > 0U) && (length <= 8U)) {
        ++g_bsp_can_link_report.rx_match_count;
        if (g_bsp_can_link_report.state == BSP_CAN_LINK_READY)
          Link_SendData(0x322U, response, length, now_ms);
        else ++g_bsp_can_link_report.busy_count;
      } else ++g_bsp_can_link_report.invalid_count;
    } else if ((frame.id == 0x123U) && (frame.is_extended == 0U) &&
        (frame.is_remote == 0U) && (frame.dlc == 2U) &&
        (frame.data[0] == 1U) && (frame.data[1] == 2U)) {
      ++g_bsp_can_link_report.rx_match_count;
      if (g_bsp_can_link_report.state == BSP_CAN_LINK_READY)
        Link_Send(0x322U, 0xA1U, 0xA2U, now_ms);
      else ++g_bsp_can_link_report.busy_count;
    } else ++g_bsp_can_link_report.invalid_count;
  }
  if ((g_bsp_can_link_report.state == BSP_CAN_LINK_READY) &&
      (g_bsp_can_link_send_request != 0U)) {
    g_bsp_can_link_send_request = 0U;
    Link_Send(0x321U, 1U, 2U, now_ms);
  }
}
