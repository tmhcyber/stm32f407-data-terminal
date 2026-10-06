#include "bsp_uart.h"

#include "bsp_rs485.h"

#include "stm32f4xx_hal.h"

static UART_HandleTypeDef s_uart1_handle;
static uint8_t s_uart1_initialized;
static volatile uint8_t s_uart1_tx_in_progress;
static volatile BSP_UART_TxEvent_t s_uart1_tx_event;

static BSP_UART_Status_t BSP_UART_MapStatus(HAL_StatusTypeDef hal_status)
{
  switch (hal_status)
  {
    case HAL_OK:
      return BSP_UART_STATUS_OK;

    case HAL_TIMEOUT:
      return BSP_UART_STATUS_TIMEOUT;

    case HAL_BUSY:
      return BSP_UART_STATUS_BUSY;

    case HAL_ERROR:
    default:
      return BSP_UART_STATUS_ERROR;
  }
}

BSP_UART_Status_t BSP_UART1_Init(void)
{
  HAL_StatusTypeDef hal_status;

  s_uart1_initialized = 0U;
  s_uart1_tx_in_progress = 0U;
  s_uart1_tx_event.type = BSP_UART_TX_EVENT_NONE;
  s_uart1_tx_event.timestamp_ms = 0U;
  s_uart1_handle.Instance = USART1;
  s_uart1_handle.Init.BaudRate = 115200U;
  s_uart1_handle.Init.WordLength = UART_WORDLENGTH_8B;
  s_uart1_handle.Init.StopBits = UART_STOPBITS_1;
  s_uart1_handle.Init.Parity = UART_PARITY_NONE;
  s_uart1_handle.Init.Mode = UART_MODE_TX;
  s_uart1_handle.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  s_uart1_handle.Init.OverSampling = UART_OVERSAMPLING_16;

  hal_status = HAL_UART_Init(&s_uart1_handle);
  if (hal_status == HAL_OK)
  {
    s_uart1_initialized = 1U;
  }

  return BSP_UART_MapStatus(hal_status);
}

BSP_UART_Status_t BSP_UART1_Write(const uint8_t *data,
                                  uint16_t length,
                                  uint32_t timeout_ms)
{
  if (s_uart1_initialized == 0U)
  {
    return BSP_UART_STATUS_NOT_INITIALIZED;
  }

  if ((data == 0) || (length == 0U) || (timeout_ms == 0U))
  {
    return BSP_UART_STATUS_INVALID_ARGUMENT;
  }

  return BSP_UART_MapStatus(HAL_UART_Transmit(&s_uart1_handle,
                                              (uint8_t *)data,
                                              length,
                                              timeout_ms));
}

BSP_UART_Status_t BSP_UART1_WriteAsync(const uint8_t *data,
                                       uint16_t length)
{
  HAL_StatusTypeDef hal_status;

  if (s_uart1_initialized == 0U)
  {
    return BSP_UART_STATUS_NOT_INITIALIZED;
  }

  if ((data == 0) || (length == 0U))
  {
    return BSP_UART_STATUS_INVALID_ARGUMENT;
  }

  if ((s_uart1_tx_in_progress != 0U) ||
      (s_uart1_tx_event.type != BSP_UART_TX_EVENT_NONE))
  {
    return BSP_UART_STATUS_BUSY;
  }

  s_uart1_tx_in_progress = 1U;
  hal_status = HAL_UART_Transmit_IT(&s_uart1_handle,
                                    (uint8_t *)data,
                                    length);
  if (hal_status != HAL_OK)
  {
    s_uart1_tx_in_progress = 0U;
  }

  return BSP_UART_MapStatus(hal_status);
}

BSP_UART_TxEvent_t BSP_UART1_TakeTxEvent(void)
{
  BSP_UART_TxEvent_t event;
  uint32_t primask;

  primask = __get_PRIMASK();
  __disable_irq();
  event.type = s_uart1_tx_event.type;
  event.timestamp_ms = s_uart1_tx_event.timestamp_ms;
  s_uart1_tx_event.type = BSP_UART_TX_EVENT_NONE;
  s_uart1_tx_event.timestamp_ms = 0U;
  if (primask == 0U)
  {
    __enable_irq();
  }

  return event;
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *uart_handle)
{
  if (uart_handle == NULL)
  {
    return;
  }

  if (uart_handle == &s_uart1_handle)
  {
    s_uart1_tx_in_progress = 0U;
    s_uart1_tx_event.timestamp_ms = HAL_GetTick();
    s_uart1_tx_event.type = BSP_UART_TX_EVENT_COMPLETE;
    return;
  }

  if (uart_handle->Instance == USART2)
  {
    BSP_RS485_HandleTxCompleteIRQ(HAL_GetTick());
  }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *uart_handle)
{
  if (uart_handle == NULL)
  {
    return;
  }

  if (uart_handle->Instance == USART2)
  {
    BSP_RS485_HandleRxCompleteIRQ(HAL_GetTick());
  }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart_handle)
{
  if (uart_handle == NULL)
  {
    return;
  }

  if (uart_handle == &s_uart1_handle)
  {
    s_uart1_tx_in_progress = 0U;
    s_uart1_tx_event.timestamp_ms = HAL_GetTick();
    s_uart1_tx_event.type = BSP_UART_TX_EVENT_ERROR;
    return;
  }

  if (uart_handle->Instance == USART2)
  {
    BSP_RS485_HandleUartErrorIRQ(HAL_GetTick(),
                                HAL_UART_GetError(uart_handle));
  }
}

void USART1_IRQHandler(void)
{
  HAL_UART_IRQHandler(&s_uart1_handle);
}

void HAL_UART_MspInit(UART_HandleTypeDef *uart_handle)
{
  GPIO_InitTypeDef gpio_init;

  if (uart_handle->Instance == USART1)
  {
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();

    gpio_init.Pin = GPIO_PIN_9;
    gpio_init.Mode = GPIO_MODE_AF_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio_init.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOA, &gpio_init);

    HAL_NVIC_SetPriority(USART1_IRQn, 5U, 0U);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
    return;
  }

  if (uart_handle->Instance == USART2)
  {
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART2_CLK_ENABLE();

    gpio_init.Pin = GPIO_PIN_2 | GPIO_PIN_3;
    gpio_init.Mode = GPIO_MODE_AF_PP;
    gpio_init.Pull = GPIO_PULLUP;
    gpio_init.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio_init.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(GPIOA, &gpio_init);

    HAL_NVIC_SetPriority(USART2_IRQn, 5U, 0U);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
  }
}
