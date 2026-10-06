#include "app_rs485_service.h"

#include <string.h>

#include "bsp_rs485.h"

#define APP_RS485_TICK_FORWARD_LIMIT_MS 0x80000000UL

static const uint8_t s_ping_request[] = "V1|PING\r\n";
static const uint8_t s_pong_response[] = "V1|PONG\r\n";

static uint8_t s_rx_frame[APP_RS485_FRAME_STORAGE_BYTES];
static uint8_t s_last_byte_was_cr;

volatile App_RS485Report_t g_app_rs485_report;

static void App_RS485Service_ResetFrame(void)
{
  g_app_rs485_report.current_frame_length = 0U;
  g_app_rs485_report.has_last_rx_byte = 0U;
  s_last_byte_was_cr = 0U;
  s_rx_frame[0] = '\0';
}

static void App_RS485Service_EnterRxWait(void)
{
  App_RS485Service_ResetFrame();
  g_app_rs485_report.state = APP_RS485_STATE_RX_WAIT;
}

static void App_RS485Service_EnterDiscard(uint32_t timestamp_ms,
                                          uint8_t last_byte_was_cr)
{
  g_app_rs485_report.current_frame_length = 0U;
  g_app_rs485_report.has_last_rx_byte = 1U;
  g_app_rs485_report.last_rx_byte_at_ms = timestamp_ms;
  s_last_byte_was_cr = last_byte_was_cr;
  s_rx_frame[0] = '\0';
  g_app_rs485_report.state = APP_RS485_STATE_RX_DISCARD;
}

static void App_RS485Service_ClearReport(void)
{
  (void)memset((void *)&g_app_rs485_report,
               0,
               sizeof(g_app_rs485_report));
  g_app_rs485_report.state = APP_RS485_STATE_NOT_INITIALIZED;
  App_RS485Service_ResetFrame();
}

static uint8_t App_RS485Service_IsPing(void)
{
  const uint8_t expected_length = (uint8_t)(sizeof(s_ping_request) - 1U);

  return (uint8_t)(
    (g_app_rs485_report.current_frame_length == expected_length) &&
    (memcmp(s_rx_frame, s_ping_request, expected_length) == 0));
}

static void App_RS485Service_ApplyGapBeforeByte(uint32_t timestamp_ms)
{
  if ((g_app_rs485_report.has_last_rx_byte != 0U) &&
      ((uint32_t)(timestamp_ms -
                  g_app_rs485_report.last_rx_byte_at_ms) >=
       APP_RS485_INTERBYTE_TIMEOUT_MS))
  {
    ++g_app_rs485_report.interbyte_timeout_count;
    App_RS485Service_EnterRxWait();
  }
}

static void App_RS485Service_ProcessDiscardByte(
  const BSP_RS485_RxByteEvent_t *event)
{
  uint8_t delimiter_complete;

  delimiter_complete = (uint8_t)((s_last_byte_was_cr != 0U) &&
                                 (event->byte == (uint8_t)'\n'));
  g_app_rs485_report.has_last_rx_byte = 1U;
  g_app_rs485_report.last_rx_byte_at_ms = event->timestamp_ms;
  if (delimiter_complete != 0U)
  {
    App_RS485Service_EnterRxWait();
    return;
  }

  s_last_byte_was_cr = (uint8_t)(event->byte == (uint8_t)'\r');
}

static void App_RS485Service_ProcessReceiveByte(
  const BSP_RS485_RxByteEvent_t *event)
{
  uint8_t frame_length;
  uint8_t delimiter_complete;

  frame_length = g_app_rs485_report.current_frame_length;
  s_rx_frame[frame_length] = event->byte;
  ++frame_length;
  g_app_rs485_report.current_frame_length = frame_length;
  g_app_rs485_report.has_last_rx_byte = 1U;
  g_app_rs485_report.last_rx_byte_at_ms = event->timestamp_ms;

  delimiter_complete = (uint8_t)(
    (frame_length >= 2U) &&
    (s_rx_frame[frame_length - 2U] == (uint8_t)'\r') &&
    (s_rx_frame[frame_length - 1U] == (uint8_t)'\n'));
  if (delimiter_complete != 0U)
  {
    s_rx_frame[frame_length] = '\0';
    ++g_app_rs485_report.complete_frame_count;
    if (App_RS485Service_IsPing() != 0U)
    {
      ++g_app_rs485_report.valid_ping_count;
      App_RS485Service_ResetFrame();
      g_app_rs485_report.rx_byte_count += BSP_RS485_FlushRx();
      g_app_rs485_report.state = APP_RS485_STATE_RESPONSE_PENDING;
    }
    else
    {
      ++g_app_rs485_report.semantic_invalid_count;
      App_RS485Service_EnterRxWait();
    }
    return;
  }

  if (frame_length >= APP_RS485_MAX_FRAME_BYTES)
  {
    ++g_app_rs485_report.overlength_count;
    App_RS485Service_EnterDiscard(
      event->timestamp_ms,
      (uint8_t)(event->byte == (uint8_t)'\r'));
  }
}

static void App_RS485Service_ProcessRxBytes(void)
{
  BSP_RS485_RxByteEvent_t event;

  while (BSP_RS485_TakeRxByte(&event) != 0U)
  {
    ++g_app_rs485_report.rx_byte_count;
    App_RS485Service_ApplyGapBeforeByte(event.timestamp_ms);

    if (g_app_rs485_report.state == APP_RS485_STATE_RX_DISCARD)
    {
      App_RS485Service_ProcessDiscardByte(&event);
    }
    else if (g_app_rs485_report.state == APP_RS485_STATE_RX_WAIT)
    {
      App_RS485Service_ProcessReceiveByte(&event);
    }

    if (g_app_rs485_report.state == APP_RS485_STATE_RESPONSE_PENDING)
    {
      return;
    }
  }
}

static void App_RS485Service_HandleRxFault(uint32_t now_ms)
{
  BSP_RS485_RxFaultEvent_t event;

  event = BSP_RS485_TakeRxFaultEvent();
  if ((event.overflow_count == 0U) && (event.uart_error_count == 0U))
  {
    return;
  }

  g_app_rs485_report.rx_queue_overflow_count += event.overflow_count;
  g_app_rs485_report.uart_error_count += event.uart_error_count;
  if (event.uart_error_count != 0U)
  {
    g_app_rs485_report.last_uart_error_flags =
      event.last_uart_error_flags;
  }
  g_app_rs485_report.rx_byte_count += event.overflow_count;
  g_app_rs485_report.rx_byte_count += BSP_RS485_FlushRx();

  if ((g_app_rs485_report.state == APP_RS485_STATE_RX_WAIT) ||
      (g_app_rs485_report.state == APP_RS485_STATE_RX_DISCARD))
  {
    App_RS485Service_EnterDiscard(now_ms, 0U);
  }
}

static void App_RS485Service_HandleTxEvent(void)
{
  BSP_RS485_TxEvent_t event;
  uint32_t elapsed_ms;

  event = BSP_RS485_TakeTxEvent();
  if (event.type == BSP_RS485_TX_EVENT_NONE)
  {
    return;
  }

  if (g_app_rs485_report.state != APP_RS485_STATE_TX_WAIT_COMPLETE)
  {
    ++g_app_rs485_report.unexpected_tx_event_count;
    return;
  }

  elapsed_ms = (uint32_t)(event.timestamp_ms -
                          g_app_rs485_report.tx_started_at_ms);
  if (event.type == BSP_RS485_TX_EVENT_COMPLETE)
  {
    if (elapsed_ms < APP_RS485_TX_COMPLETE_TIMEOUT_MS)
    {
      ++g_app_rs485_report.tx_success_count;
      g_app_rs485_report.last_tx_completed_at_ms = event.timestamp_ms;
    }
    else
    {
      ++g_app_rs485_report.tx_timeout_count;
      ++g_app_rs485_report.tx_late_complete_count;
    }
  }
  else
  {
    ++g_app_rs485_report.tx_error_count;
    g_app_rs485_report.last_uart_error_flags = event.uart_error_flags;
  }

  App_RS485Service_EnterRxWait();
}

static void App_RS485Service_StartPendingResponse(uint32_t now_ms)
{
  BSP_RS485_Status_t status;

  status = BSP_RS485_WriteAsync(
    s_pong_response,
    (uint16_t)(sizeof(s_pong_response) - 1U));
  if (status == BSP_RS485_STATUS_OK)
  {
    ++g_app_rs485_report.tx_start_count;
    g_app_rs485_report.tx_started_at_ms = now_ms;
    g_app_rs485_report.state = APP_RS485_STATE_TX_WAIT_COMPLETE;
  }
  else
  {
    ++g_app_rs485_report.tx_start_failure_count;
    ++g_app_rs485_report.tx_error_count;
    App_RS485Service_EnterRxWait();
  }
}

static void App_RS485Service_CheckRxSilence(uint32_t now_ms)
{
  uint32_t elapsed_ms;

  elapsed_ms = (uint32_t)(now_ms -
                          g_app_rs485_report.last_rx_byte_at_ms);
  if ((g_app_rs485_report.has_last_rx_byte != 0U) &&
      (elapsed_ms < APP_RS485_TICK_FORWARD_LIMIT_MS) &&
      (elapsed_ms >= APP_RS485_INTERBYTE_TIMEOUT_MS))
  {
    ++g_app_rs485_report.interbyte_timeout_count;
    App_RS485Service_EnterRxWait();
  }
}

App_RS485ServiceResult_t App_RS485Service_Init(void)
{
  BSP_RS485_Status_t status;

  App_RS485Service_ClearReport();
  status = BSP_RS485_Init();
  if (status != BSP_RS485_STATUS_OK)
  {
    ++g_app_rs485_report.uart_error_count;
    return APP_RS485_SERVICE_RESULT_TRANSPORT_ERROR;
  }

  g_app_rs485_report.state = APP_RS485_STATE_RX_WAIT;
  return APP_RS485_SERVICE_RESULT_OK;
}

void App_RS485Service_Process(uint32_t now_ms)
{
  if (g_app_rs485_report.state == APP_RS485_STATE_NOT_INITIALIZED)
  {
    return;
  }

  App_RS485Service_HandleRxFault(now_ms);
  App_RS485Service_HandleTxEvent();

  if (g_app_rs485_report.state == APP_RS485_STATE_TX_WAIT_COMPLETE)
  {
    if ((uint32_t)(now_ms - g_app_rs485_report.tx_started_at_ms) >=
        APP_RS485_TX_COMPLETE_TIMEOUT_MS)
    {
      (void)BSP_RS485_AbortTransmit();
      ++g_app_rs485_report.tx_timeout_count;
      App_RS485Service_EnterRxWait();
    }
    return;
  }

  if (g_app_rs485_report.state == APP_RS485_STATE_RESPONSE_PENDING)
  {
    App_RS485Service_StartPendingResponse(now_ms);
    return;
  }

  App_RS485Service_ProcessRxBytes();
  if ((g_app_rs485_report.state == APP_RS485_STATE_RX_WAIT) ||
      (g_app_rs485_report.state == APP_RS485_STATE_RX_DISCARD))
  {
    App_RS485Service_CheckRxSilence(now_ms);
  }
}
