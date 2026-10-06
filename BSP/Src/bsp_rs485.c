#include "bsp_rs485.h"

#include "stm32f4xx_hal.h"

#define BSP_RS485_RX_QUEUE_MASK  (BSP_RS485_RX_QUEUE_CAPACITY - 1U)

#if ((BSP_RS485_RX_QUEUE_CAPACITY == 0U) || \
     ((BSP_RS485_RX_QUEUE_CAPACITY & BSP_RS485_RX_QUEUE_MASK) != 0U) || \
     (BSP_RS485_RX_QUEUE_CAPACITY > 128U))
#error "BSP_RS485_RX_QUEUE_CAPACITY must be a power of two from 1 to 128."
#endif

static UART_HandleTypeDef s_uart2_handle;
static volatile uint8_t s_initialized;
static uint8_t s_rx_staging_byte;
static volatile uint8_t s_tx_in_progress;
static volatile uint8_t s_rx_head;
static volatile uint8_t s_rx_tail;
static BSP_RS485_RxByteEvent_t s_rx_queue[BSP_RS485_RX_QUEUE_CAPACITY];
static volatile BSP_RS485_TxEvent_t s_tx_event;
static volatile BSP_RS485_RxFaultEvent_t s_rx_fault_event;

static BSP_RS485_Status_t BSP_RS485_MapStatus(HAL_StatusTypeDef status)
{
  switch (status)
  {
    case HAL_OK:
      return BSP_RS485_STATUS_OK;

    case HAL_BUSY:
      return BSP_RS485_STATUS_BUSY;

    case HAL_TIMEOUT:
      return BSP_RS485_STATUS_TIMEOUT;

    case HAL_ERROR:
    default:
      return BSP_RS485_STATUS_ERROR;
  }
}

static void BSP_RS485_SetReceiveDirection(void)
{
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_0, GPIO_PIN_RESET);
}

static void BSP_RS485_SetSendDirection(void)
{
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_0, GPIO_PIN_SET);
}

static void BSP_RS485_InitDirectionPin(void)
{
  GPIO_InitTypeDef gpio_init;

  __HAL_RCC_GPIOC_CLK_ENABLE();
  BSP_RS485_SetReceiveDirection();

  gpio_init.Pin = GPIO_PIN_0;
  gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
  gpio_init.Pull = GPIO_PULLDOWN;
  gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
  gpio_init.Alternate = 0U;
  HAL_GPIO_Init(GPIOC, &gpio_init);
  BSP_RS485_SetReceiveDirection();
}

static void BSP_RS485_ClearState(void)
{
  s_initialized = 0U;
  s_rx_staging_byte = 0U;
  s_tx_in_progress = 0U;
  s_rx_head = 0U;
  s_rx_tail = 0U;
  s_tx_event.type = BSP_RS485_TX_EVENT_NONE;
  s_tx_event.timestamp_ms = 0U;
  s_tx_event.uart_error_flags = 0U;
  s_rx_fault_event.overflow_count = 0U;
  s_rx_fault_event.last_overflow_at_ms = 0U;
  s_rx_fault_event.uart_error_count = 0U;
  s_rx_fault_event.last_uart_error_at_ms = 0U;
  s_rx_fault_event.last_uart_error_flags = 0U;
}

BSP_RS485_Status_t BSP_RS485_Init(void)
{
  HAL_StatusTypeDef status;
  uint32_t primask;

  BSP_RS485_ClearState();
  BSP_RS485_InitDirectionPin();

  s_uart2_handle.Instance = USART2;
  s_uart2_handle.Init.BaudRate = 115200U;
  s_uart2_handle.Init.WordLength = UART_WORDLENGTH_8B;
  s_uart2_handle.Init.StopBits = UART_STOPBITS_1;
  s_uart2_handle.Init.Parity = UART_PARITY_NONE;
  s_uart2_handle.Init.Mode = UART_MODE_TX_RX;
  s_uart2_handle.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  s_uart2_handle.Init.OverSampling = UART_OVERSAMPLING_16;

  status = HAL_UART_Init(&s_uart2_handle);
  if (status != HAL_OK)
  {
    BSP_RS485_SetReceiveDirection();
    return BSP_RS485_MapStatus(status);
  }

  /*
   * Publish the initialized flag atomically with arming RX. Otherwise an
   * immediately pending RX interrupt could run after HAL enables RXNE but
   * before s_initialized becomes visible, drop the byte, and leave RX
   * unarmed. Preserve the caller's interrupt state instead of always
   * enabling interrupts on exit.
   */
  primask = __get_PRIMASK();
  __disable_irq();
  status = HAL_UART_Receive_IT(&s_uart2_handle, &s_rx_staging_byte, 1U);
  if (status == HAL_OK)
  {
    s_initialized = 1U;
  }
  if (primask == 0U)
  {
    __enable_irq();
  }
  if (status != HAL_OK)
  {
    BSP_RS485_SetReceiveDirection();
    return BSP_RS485_MapStatus(status);
  }

  return BSP_RS485_STATUS_OK;
}

BSP_RS485_Status_t BSP_RS485_WriteAsync(const uint8_t *data,
                                        uint16_t length)
{
  HAL_StatusTypeDef status;
  uint32_t primask;

  if (s_initialized == 0U)
  {
    return BSP_RS485_STATUS_NOT_INITIALIZED;
  }

  if ((data == 0) || (length == 0U))
  {
    return BSP_RS485_STATUS_INVALID_ARGUMENT;
  }

  primask = __get_PRIMASK();
  __disable_irq();
  if ((s_tx_in_progress != 0U) ||
      (s_tx_event.type != BSP_RS485_TX_EVENT_NONE))
  {
    if (primask == 0U)
    {
      __enable_irq();
    }
    return BSP_RS485_STATUS_BUSY;
  }
  s_tx_in_progress = 1U;
  if (primask == 0U)
  {
    __enable_irq();
  }

  BSP_RS485_SetSendDirection();
  status = HAL_UART_Transmit_IT(&s_uart2_handle, (uint8_t *)data, length);
  if (status != HAL_OK)
  {
    primask = __get_PRIMASK();
    __disable_irq();
    s_tx_in_progress = 0U;
    if (primask == 0U)
    {
      __enable_irq();
    }
    BSP_RS485_SetReceiveDirection();
  }

  return BSP_RS485_MapStatus(status);
}

BSP_RS485_Status_t BSP_RS485_AbortTransmit(void)
{
  HAL_StatusTypeDef status;
  uint32_t primask;

  if (s_initialized == 0U)
  {
    return BSP_RS485_STATUS_NOT_INITIALIZED;
  }

  status = HAL_UART_AbortTransmit(&s_uart2_handle);
  BSP_RS485_SetReceiveDirection();

  primask = __get_PRIMASK();
  __disable_irq();
  s_tx_in_progress = 0U;
  s_tx_event.type = BSP_RS485_TX_EVENT_NONE;
  s_tx_event.timestamp_ms = 0U;
  s_tx_event.uart_error_flags = 0U;
  if (primask == 0U)
  {
    __enable_irq();
  }

  return BSP_RS485_MapStatus(status);
}

uint8_t BSP_RS485_TakeRxByte(BSP_RS485_RxByteEvent_t *event)
{
  uint32_t primask;

  if (event == 0)
  {
    return 0U;
  }

  primask = __get_PRIMASK();
  __disable_irq();
  if (s_rx_head == s_rx_tail)
  {
    if (primask == 0U)
    {
      __enable_irq();
    }
    return 0U;
  }

  *event = s_rx_queue[s_rx_tail & BSP_RS485_RX_QUEUE_MASK];
  ++s_rx_tail;
  if (primask == 0U)
  {
    __enable_irq();
  }
  return 1U;
}

BSP_RS485_TxEvent_t BSP_RS485_TakeTxEvent(void)
{
  BSP_RS485_TxEvent_t event;
  uint32_t primask;

  primask = __get_PRIMASK();
  __disable_irq();
  event.type = s_tx_event.type;
  event.timestamp_ms = s_tx_event.timestamp_ms;
  event.uart_error_flags = s_tx_event.uart_error_flags;
  s_tx_event.type = BSP_RS485_TX_EVENT_NONE;
  s_tx_event.timestamp_ms = 0U;
  s_tx_event.uart_error_flags = 0U;
  if (primask == 0U)
  {
    __enable_irq();
  }
  return event;
}

BSP_RS485_RxFaultEvent_t BSP_RS485_TakeRxFaultEvent(void)
{
  BSP_RS485_RxFaultEvent_t event;
  uint32_t primask;

  primask = __get_PRIMASK();
  __disable_irq();
  event.overflow_count = s_rx_fault_event.overflow_count;
  event.last_overflow_at_ms = s_rx_fault_event.last_overflow_at_ms;
  event.uart_error_count = s_rx_fault_event.uart_error_count;
  event.last_uart_error_at_ms = s_rx_fault_event.last_uart_error_at_ms;
  event.last_uart_error_flags = s_rx_fault_event.last_uart_error_flags;
  s_rx_fault_event.overflow_count = 0U;
  s_rx_fault_event.last_overflow_at_ms = 0U;
  s_rx_fault_event.uart_error_count = 0U;
  s_rx_fault_event.last_uart_error_at_ms = 0U;
  s_rx_fault_event.last_uart_error_flags = 0U;
  if (primask == 0U)
  {
    __enable_irq();
  }
  return event;
}

uint8_t BSP_RS485_FlushRx(void)
{
  uint8_t flushed_count;
  uint32_t primask;

  primask = __get_PRIMASK();
  __disable_irq();
  flushed_count = (uint8_t)(s_rx_head - s_rx_tail);
  s_rx_tail = s_rx_head;
  if (primask == 0U)
  {
    __enable_irq();
  }
  return flushed_count;
}

void BSP_RS485_HandleRxCompleteIRQ(uint32_t timestamp_ms)
{
  uint8_t queued_count;
  HAL_StatusTypeDef status;

  if (s_initialized == 0U)
  {
    return;
  }

  queued_count = (uint8_t)(s_rx_head - s_rx_tail);
  if (queued_count >= BSP_RS485_RX_QUEUE_CAPACITY)
  {
    ++s_rx_fault_event.overflow_count;
    s_rx_fault_event.last_overflow_at_ms = timestamp_ms;
  }
  else
  {
    s_rx_queue[s_rx_head & BSP_RS485_RX_QUEUE_MASK].byte =
      s_rx_staging_byte;
    s_rx_queue[s_rx_head & BSP_RS485_RX_QUEUE_MASK].timestamp_ms =
      timestamp_ms;
    ++s_rx_head;
  }

  /*
   * HAL may deliver RxCpltCallback before ErrorCallback for the same byte.
   * Starting a new receive here would clear huart->ErrorCode, so let the
   * error callback preserve the flags and re-arm reception instead.
   */
  if (HAL_UART_GetError(&s_uart2_handle) != HAL_UART_ERROR_NONE)
  {
    return;
  }

  status = HAL_UART_Receive_IT(&s_uart2_handle, &s_rx_staging_byte, 1U);
  if (status != HAL_OK)
  {
    ++s_rx_fault_event.uart_error_count;
    s_rx_fault_event.last_uart_error_at_ms = timestamp_ms;
    s_rx_fault_event.last_uart_error_flags = HAL_UART_GetError(
      &s_uart2_handle);
  }
}

void BSP_RS485_HandleTxCompleteIRQ(uint32_t timestamp_ms)
{
  BSP_RS485_SetReceiveDirection();
  if ((s_initialized == 0U) || (s_tx_in_progress == 0U))
  {
    return;
  }

  s_tx_in_progress = 0U;
  s_tx_event.timestamp_ms = timestamp_ms;
  s_tx_event.uart_error_flags = 0U;
  s_tx_event.type = BSP_RS485_TX_EVENT_COMPLETE;
}

void BSP_RS485_HandleUartErrorIRQ(uint32_t timestamp_ms,
                                 uint32_t uart_error_flags)
{
  HAL_StatusTypeDef rx_status;

  BSP_RS485_SetReceiveDirection();
  if (s_initialized == 0U)
  {
    return;
  }

  ++s_rx_fault_event.uart_error_count;
  s_rx_fault_event.last_uart_error_at_ms = timestamp_ms;
  s_rx_fault_event.last_uart_error_flags = uart_error_flags;

  if (s_tx_in_progress != 0U)
  {
    s_tx_in_progress = 0U;
    s_tx_event.timestamp_ms = timestamp_ms;
    s_tx_event.uart_error_flags = uart_error_flags;
    s_tx_event.type = BSP_RS485_TX_EVENT_ERROR;
  }

  rx_status = HAL_UART_Receive_IT(&s_uart2_handle, &s_rx_staging_byte, 1U);
  if ((rx_status != HAL_OK) && (rx_status != HAL_BUSY))
  {
    ++s_rx_fault_event.uart_error_count;
    s_rx_fault_event.last_uart_error_at_ms = timestamp_ms;
    s_rx_fault_event.last_uart_error_flags |= HAL_UART_GetError(
      &s_uart2_handle);
  }
}

void USART2_IRQHandler(void)
{
  HAL_UART_IRQHandler(&s_uart2_handle);
}
