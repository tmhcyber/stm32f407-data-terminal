#include "bsp_can_loopback.h"
#include "stm32f4xx_hal.h"

#define LOOPBACK_WAIT_MS 100U
#define LOOPBACK_ACCEPT_ID 0x123U

volatile BSP_CAN_LoopbackReport_t g_bsp_can_loopback_report;
static CAN_HandleTypeDef s_can;
static uint32_t s_mailbox;
static uint32_t s_started_at_ms;

static void Loopback_Fail(BSP_CAN_LoopbackError_t error)
{
  g_bsp_can_loopback_report.error = error;
  g_bsp_can_loopback_report.hal_error = s_can.ErrorCode;
  if (g_bsp_can_loopback_report.state == BSP_CAN_LOOPBACK_WAIT_TX)
  {
    (void)HAL_CAN_AbortTxRequest(&s_can, s_mailbox);
  }
  g_bsp_can_loopback_report.state = BSP_CAN_LOOPBACK_FAILED;
}

void BSP_CAN_Loopback_Init(void)
{
  CAN_FilterTypeDef filter = {0};
  GPIO_InitTypeDef rx_gpio = {0};

  /* Single initialization per reset; never restart an in-flight experiment. */
  if (g_bsp_can_loopback_report.state != BSP_CAN_LOOPBACK_NOT_RUN)
  {
    return;
  }
  g_bsp_can_loopback_report.pclk1_hz = HAL_RCC_GetPCLK1Freq();
  if (g_bsp_can_loopback_report.pclk1_hz != 16000000U)
  {
    Loopback_Fail(BSP_CAN_LOOPBACK_ERROR_CLOCK);
    return;
  }

  /* CAN2 depends on the CAN1 clock for the shared filter/RAM resources. */
  __HAL_RCC_CAN1_CLK_ENABLE();
  __HAL_RCC_CAN2_CLK_ENABLE();
  /* Even loopback startup needs a recessive CAN RX input for bus sync.
   * Route PB12 to CAN2 RX with a weak pull-up. Leave TX/PB13 untouched.
   */
  __HAL_RCC_GPIOB_CLK_ENABLE();
  rx_gpio.Pin = GPIO_PIN_12;
  rx_gpio.Mode = GPIO_MODE_AF_PP;
  rx_gpio.Pull = GPIO_PULLUP;
  rx_gpio.Speed = GPIO_SPEED_FREQ_LOW;
  rx_gpio.Alternate = GPIO_AF9_CAN2;
  HAL_GPIO_Init(GPIOB, &rx_gpio);
  s_can.Instance = CAN2;
  s_can.Init.Prescaler = 2U;
  s_can.Init.Mode = CAN_MODE_SILENT_LOOPBACK;
  s_can.Init.SyncJumpWidth = CAN_SJW_1TQ;
  s_can.Init.TimeSeg1 = CAN_BS1_13TQ;
  s_can.Init.TimeSeg2 = CAN_BS2_2TQ;
  s_can.Init.TimeTriggeredMode = DISABLE;
  s_can.Init.AutoBusOff = DISABLE;
  s_can.Init.AutoWakeUp = DISABLE;
  s_can.Init.AutoRetransmission = DISABLE;
  s_can.Init.ReceiveFifoLocked = ENABLE;
  s_can.Init.TransmitFifoPriority = DISABLE;
  /* Request Init before this HAL clears SLEEP and waits for SLAK.
   * Otherwise its Sleep -> Normal transition needs external RX bus idle
   * before BTR has enabled our internal loopback (RM0090, bxCAN modes).
   * HAL still clears SLEEP and checks both hardware acknowledgements.
   */
  SET_BIT(s_can.Instance->MCR, CAN_MCR_INRQ);
  if (HAL_CAN_Init(&s_can) != HAL_OK)
  {
    Loopback_Fail(BSP_CAN_LOOPBACK_ERROR_INIT);
    return;
  }

  filter.SlaveStartFilterBank = 14U;
  filter.FilterBank = 14U;
  filter.FilterMode = CAN_FILTERMODE_IDMASK;
  filter.FilterScale = CAN_FILTERSCALE_32BIT;
  /* 32-bit filter word: STID at bits 31:21, IDE at bit 2, RTR at bit 1. */
  filter.FilterIdHigh = LOOPBACK_ACCEPT_ID << 5;
  filter.FilterIdLow = 0U;
  filter.FilterMaskIdHigh = 0x7FFU << 5;
  filter.FilterMaskIdLow = 0x0006U;
  filter.FilterFIFOAssignment = CAN_FILTER_FIFO0;
  filter.FilterActivation = ENABLE;
  if (HAL_CAN_ConfigFilter(&s_can, &filter) != HAL_OK)
  {
    Loopback_Fail(BSP_CAN_LOOPBACK_ERROR_FILTER);
    return;
  }
  if (HAL_CAN_Start(&s_can) != HAL_OK)
  {
    Loopback_Fail(BSP_CAN_LOOPBACK_ERROR_START);
    return;
  }
  g_bsp_can_loopback_report.step = 1U;
  g_bsp_can_loopback_report.state = BSP_CAN_LOOPBACK_SEND;
}

static void Loopback_Next(void)
{
  if (g_bsp_can_loopback_report.step == 3U)
  {
    g_bsp_can_loopback_report.state = BSP_CAN_LOOPBACK_PASSED;
  }
  else
  {
    ++g_bsp_can_loopback_report.step;
    g_bsp_can_loopback_report.state = BSP_CAN_LOOPBACK_SEND;
  }
}

void BSP_CAN_Loopback_Process(uint32_t now_ms)
{
  uint32_t shift;
  uint32_t tsr;
  uint32_t i;
  CAN_TxHeaderTypeDef tx = {0};
  CAN_RxHeaderTypeDef rx = {0};
  /* HAL reads all eight bytes even when DLC is two. */
  uint8_t data[8] = {1U, 2U, 0U, 0U, 0U, 0U, 0U, 0U};

  switch (g_bsp_can_loopback_report.state)
  {
    case BSP_CAN_LOOPBACK_SEND:
      if (HAL_CAN_GetRxFifoFillLevel(&s_can, CAN_RX_FIFO0) != 0U)
      {
        Loopback_Fail(BSP_CAN_LOOPBACK_ERROR_UNEXPECTED_RX);
        return;
      }
      tx.StdId = (g_bsp_can_loopback_report.step == 2U) ?
        0x120U : LOOPBACK_ACCEPT_ID;
      tx.IDE = CAN_ID_STD;
      tx.RTR = CAN_RTR_DATA;
      tx.DLC = 2U;
      tx.TransmitGlobalTime = DISABLE;
      /* W1C: only completion flags, never a read-modify-write of TSR. */
      s_can.Instance->TSR = CAN_TSR_RQCP0 | CAN_TSR_RQCP1 | CAN_TSR_RQCP2;
      if (HAL_CAN_AddTxMessage(&s_can, &tx, data, &s_mailbox) != HAL_OK)
      {
        Loopback_Fail(BSP_CAN_LOOPBACK_ERROR_SUBMIT);
        return;
      }
      ++g_bsp_can_loopback_report.submitted_count;
      s_started_at_ms = now_ms;
      g_bsp_can_loopback_report.state = BSP_CAN_LOOPBACK_WAIT_TX;
      return;

    case BSP_CAN_LOOPBACK_WAIT_TX:
      shift = (s_mailbox == CAN_TX_MAILBOX0) ? 0U :
        ((s_mailbox == CAN_TX_MAILBOX1) ? 8U : 16U);
      tsr = s_can.Instance->TSR;
      g_bsp_can_loopback_report.last_tsr = tsr;
      g_bsp_can_loopback_report.last_esr = s_can.Instance->ESR;
      if ((tsr & (CAN_TSR_RQCP0 << shift)) != 0U)
      {
        if ((tsr & (CAN_TSR_TXOK0 << shift)) == 0U)
        {
          Loopback_Fail(BSP_CAN_LOOPBACK_ERROR_TX);
          return;
        }
        s_can.Instance->TSR = CAN_TSR_RQCP0 << shift;
        ++g_bsp_can_loopback_report.tx_ok_count;
        s_started_at_ms = now_ms;
        g_bsp_can_loopback_report.state = BSP_CAN_LOOPBACK_WAIT_RX;
      }
      else if ((uint32_t)(now_ms - s_started_at_ms) >= LOOPBACK_WAIT_MS)
      {
        Loopback_Fail(BSP_CAN_LOOPBACK_ERROR_TX_TIMEOUT);
      }
      return;

    case BSP_CAN_LOOPBACK_WAIT_RX:
      if (HAL_CAN_GetRxFifoFillLevel(&s_can, CAN_RX_FIFO0) != 0U)
      {
        if (HAL_CAN_GetRxMessage(&s_can, CAN_RX_FIFO0, &rx, data) != HAL_OK)
        {
          Loopback_Fail(BSP_CAN_LOOPBACK_ERROR_RX);
          return;
        }
        g_bsp_can_loopback_report.last_rx_id = rx.StdId;
        g_bsp_can_loopback_report.last_rx_ide = rx.IDE;
        g_bsp_can_loopback_report.last_rx_rtr = rx.RTR;
        g_bsp_can_loopback_report.last_rx_dlc = rx.DLC;
        for (i = 0U; i < 8U; ++i)
        {
          g_bsp_can_loopback_report.last_rx_data[i] = data[i];
        }
        if ((g_bsp_can_loopback_report.step == 2U) ||
            (rx.StdId != LOOPBACK_ACCEPT_ID) || (rx.IDE != CAN_ID_STD) ||
            (rx.RTR != CAN_RTR_DATA) || (rx.DLC != 2U) ||
            (data[0] != 1U) || (data[1] != 2U) ||
            (HAL_CAN_GetRxFifoFillLevel(&s_can, CAN_RX_FIFO0) != 0U))
        {
          Loopback_Fail(BSP_CAN_LOOPBACK_ERROR_UNEXPECTED_RX);
          return;
        }
        ++g_bsp_can_loopback_report.rx_match_count;
        Loopback_Next();
      }
      else if ((uint32_t)(now_ms - s_started_at_ms) >= LOOPBACK_WAIT_MS)
      {
        if (g_bsp_can_loopback_report.step == 2U)
        {
          ++g_bsp_can_loopback_report.rejected_count;
          Loopback_Next();
        }
        else
        {
          Loopback_Fail(BSP_CAN_LOOPBACK_ERROR_RX_TIMEOUT);
        }
      }
      return;

    default:
      return; /* Not started, passed or failed: no further transmissions. */
  }
}
