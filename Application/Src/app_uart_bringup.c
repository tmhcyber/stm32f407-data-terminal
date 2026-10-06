#include "app_uart_bringup.h"

#include "bsp_uart.h"

#define APP_UART_BRINGUP_SEND_PERIOD_MS  1000U
#define APP_UART_BRINGUP_SEND_TIMEOUT_MS 20U

static const uint8_t s_uart_bringup_message[] = "UART1_TX_OK\r\n";
static uint8_t s_uart_ready;
static uint8_t s_first_attempt_pending;

volatile App_UART_BringupReport_t g_app_uart_bringup_report =
{
  APP_UART_BRINGUP_STATUS_NOT_RUN,
  APP_UART_BRINGUP_STATUS_NOT_RUN,
  0U,
  0U,
  0U
};

static App_UART_BringupStatus_t App_UART_Bringup_MapStatus(
  BSP_UART_Status_t status)
{
  switch (status)
  {
    case BSP_UART_STATUS_OK:
      return APP_UART_BRINGUP_STATUS_OK;

    case BSP_UART_STATUS_NOT_INITIALIZED:
      return APP_UART_BRINGUP_STATUS_NOT_INITIALIZED;

    case BSP_UART_STATUS_INVALID_ARGUMENT:
      return APP_UART_BRINGUP_STATUS_INVALID_ARGUMENT;

    case BSP_UART_STATUS_TIMEOUT:
      return APP_UART_BRINGUP_STATUS_TIMEOUT;

    case BSP_UART_STATUS_BUSY:
      return APP_UART_BRINGUP_STATUS_BUSY;

    case BSP_UART_STATUS_ERROR:
    default:
      return APP_UART_BRINGUP_STATUS_ERROR;
  }
}

void App_UART_Bringup_Init(void)
{
  BSP_UART_Status_t status;

  s_uart_ready = 0U;
  s_first_attempt_pending = 0U;
  g_app_uart_bringup_report.init_status =
    APP_UART_BRINGUP_STATUS_NOT_RUN;
  g_app_uart_bringup_report.last_send_status =
    APP_UART_BRINGUP_STATUS_NOT_RUN;
  g_app_uart_bringup_report.last_attempt_at_ms = 0U;
  g_app_uart_bringup_report.successful_send_count = 0U;
  g_app_uart_bringup_report.failed_send_count = 0U;

  status = BSP_UART1_Init();
  g_app_uart_bringup_report.init_status =
    App_UART_Bringup_MapStatus(status);
  if (status == BSP_UART_STATUS_OK)
  {
    s_uart_ready = 1U;
    s_first_attempt_pending = 1U;
  }
}

void App_UART_Bringup_Process(uint32_t now_ms)
{
  BSP_UART_Status_t status;

  if (s_uart_ready == 0U)
  {
    return;
  }

  if ((s_first_attempt_pending == 0U) &&
      ((uint32_t)(now_ms -
                  g_app_uart_bringup_report.last_attempt_at_ms) <
       APP_UART_BRINGUP_SEND_PERIOD_MS))
  {
    return;
  }

  s_first_attempt_pending = 0U;
  g_app_uart_bringup_report.last_attempt_at_ms = now_ms;
  status = BSP_UART1_Write(s_uart_bringup_message,
                           (uint16_t)(sizeof(s_uart_bringup_message) - 1U),
                           APP_UART_BRINGUP_SEND_TIMEOUT_MS);
  g_app_uart_bringup_report.last_send_status =
    App_UART_Bringup_MapStatus(status);

  if (status == BSP_UART_STATUS_OK)
  {
    ++g_app_uart_bringup_report.successful_send_count;
  }
  else
  {
    ++g_app_uart_bringup_report.failed_send_count;
  }
}
