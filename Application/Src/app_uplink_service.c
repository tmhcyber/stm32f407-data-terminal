#include "app_uplink_service.h"

#include "bsp_uart.h"

static App_DataSnapshot_t s_latest_snapshot;
static App_UplinkMessage_t s_pending_message;
static char s_tx_buffer[APP_UPLINK_TEXT_BUFFER_SIZE];
static uint8_t s_retry_waiting;
static uint32_t s_last_start_attempt_at_ms;

volatile App_UplinkServiceReport_t g_app_uplink_service_report;

static void App_UplinkService_ClearReport(void)
{
  g_app_uplink_service_report.state =
    APP_UPLINK_SERVICE_STATE_NOT_INITIALIZED;
  g_app_uplink_service_report.last_format_result =
    APP_UPLINK_TEXT_RESULT_OK;
  g_app_uplink_service_report.active_reason = APP_UPLINK_REASON_NOT_SET;
  g_app_uplink_service_report.pending_reason = APP_UPLINK_REASON_NOT_SET;
  g_app_uplink_service_report.has_latest_snapshot = 0U;
  g_app_uplink_service_report.pending_valid = 0U;
  g_app_uplink_service_report.has_successful_transmission = 0U;
  g_app_uplink_service_report.last_completed_at_ms = 0U;
  g_app_uplink_service_report.tx_start_count = 0U;
  g_app_uplink_service_report.completed_count = 0U;
  g_app_uplink_service_report.event_completed_count = 0U;
  g_app_uplink_service_report.heartbeat_completed_count = 0U;
  g_app_uplink_service_report.format_error_count = 0U;
  g_app_uplink_service_report.coalesced_count = 0U;
  g_app_uplink_service_report.tx_busy_count = 0U;
  g_app_uplink_service_report.tx_error_count = 0U;
}

static void App_UplinkService_SetPending(
  App_UplinkReason_t reason,
  App_SnapshotEventFlags_t flags,
  uint32_t skipped_count,
  const App_DataSnapshot_t *snapshot,
  uint8_t count_replacement)
{
  if ((count_replacement != 0U) &&
      (g_app_uplink_service_report.pending_valid != 0U))
  {
    ++g_app_uplink_service_report.coalesced_count;
  }

  s_pending_message.reason = reason;
  s_pending_message.flags = flags;
  s_pending_message.skipped_count = skipped_count;
  s_pending_message.coalesced_count =
    g_app_uplink_service_report.coalesced_count;
  s_pending_message.snapshot = *snapshot;
  g_app_uplink_service_report.pending_reason = reason;
  g_app_uplink_service_report.pending_valid = 1U;
}

static void App_UplinkService_HandleTxEvent(
  BSP_UART_TxEvent_t tx_event)
{
  if (tx_event.type == BSP_UART_TX_EVENT_NONE)
  {
    return;
  }

  if (g_app_uplink_service_report.state !=
      APP_UPLINK_SERVICE_STATE_SENDING)
  {
    ++g_app_uplink_service_report.tx_error_count;
    return;
  }

  g_app_uplink_service_report.state = APP_UPLINK_SERVICE_STATE_IDLE;
  if (tx_event.type == BSP_UART_TX_EVENT_COMPLETE)
  {
    g_app_uplink_service_report.has_successful_transmission = 1U;
    g_app_uplink_service_report.last_completed_at_ms =
      tx_event.timestamp_ms;
    ++g_app_uplink_service_report.completed_count;
    if (g_app_uplink_service_report.active_reason ==
        APP_UPLINK_REASON_EVENT)
    {
      ++g_app_uplink_service_report.event_completed_count;
    }
    else if (g_app_uplink_service_report.active_reason ==
             APP_UPLINK_REASON_HEARTBEAT)
    {
      ++g_app_uplink_service_report.heartbeat_completed_count;
    }
  }
  else
  {
    ++g_app_uplink_service_report.tx_error_count;
    if ((g_app_uplink_service_report.pending_valid == 0U) &&
        (g_app_uplink_service_report.has_latest_snapshot != 0U))
    {
      App_UplinkService_SetPending(APP_UPLINK_REASON_HEARTBEAT,
                                   APP_SNAPSHOT_EVENT_NONE,
                                   0U,
                                   &s_latest_snapshot,
                                   0U);
    }
    s_retry_waiting = 1U;
    s_last_start_attempt_at_ms = tx_event.timestamp_ms;
  }
  g_app_uplink_service_report.active_reason = APP_UPLINK_REASON_NOT_SET;
}

App_UplinkServiceResult_t App_UplinkService_Init(void)
{
  BSP_UART_Status_t status;

  App_UplinkService_ClearReport();
  s_retry_waiting = 0U;
  s_last_start_attempt_at_ms = 0U;
  s_tx_buffer[0] = '\0';

  status = BSP_UART1_Init();
  if (status != BSP_UART_STATUS_OK)
  {
    ++g_app_uplink_service_report.tx_error_count;
    return APP_UPLINK_SERVICE_RESULT_TRANSPORT_ERROR;
  }

  g_app_uplink_service_report.state = APP_UPLINK_SERVICE_STATE_IDLE;
  return APP_UPLINK_SERVICE_RESULT_OK;
}

App_UplinkServiceResult_t App_UplinkService_SubmitEvent(
  const App_SnapshotEvent_t *event)
{
  if (event == 0)
  {
    return APP_UPLINK_SERVICE_RESULT_INVALID_ARGUMENT;
  }

  if (g_app_uplink_service_report.state ==
      APP_UPLINK_SERVICE_STATE_NOT_INITIALIZED)
  {
    return APP_UPLINK_SERVICE_RESULT_NOT_INITIALIZED;
  }

  s_latest_snapshot = event->snapshot;
  g_app_uplink_service_report.has_latest_snapshot = 1U;
  App_UplinkService_SetPending(APP_UPLINK_REASON_EVENT,
                               event->flags,
                               event->skipped_count,
                               &s_latest_snapshot,
                               1U);
  return APP_UPLINK_SERVICE_RESULT_OK;
}

void App_UplinkService_Process(uint32_t now_ms)
{
  App_UplinkTextResult_t format_result;
  BSP_UART_Status_t tx_status;
  BSP_UART_TxEvent_t tx_event;
  uint32_t output_length;
  uint8_t tx_completed_this_call;

  if (g_app_uplink_service_report.state ==
      APP_UPLINK_SERVICE_STATE_NOT_INITIALIZED)
  {
    return;
  }

  tx_event = BSP_UART1_TakeTxEvent();
  tx_completed_this_call =
    (uint8_t)(tx_event.type == BSP_UART_TX_EVENT_COMPLETE);
  App_UplinkService_HandleTxEvent(tx_event);

  if (g_app_uplink_service_report.state ==
      APP_UPLINK_SERVICE_STATE_SENDING)
  {
    return;
  }

  if ((tx_completed_this_call == 0U) &&
      (g_app_uplink_service_report.pending_valid == 0U) &&
      (g_app_uplink_service_report.has_latest_snapshot != 0U) &&
      (g_app_uplink_service_report.has_successful_transmission != 0U) &&
      ((uint32_t)(now_ms -
                  g_app_uplink_service_report.last_completed_at_ms) >=
       APP_UPLINK_HEARTBEAT_PERIOD_MS))
  {
    App_UplinkService_SetPending(APP_UPLINK_REASON_HEARTBEAT,
                                 APP_SNAPSHOT_EVENT_NONE,
                                 0U,
                                 &s_latest_snapshot,
                                 0U);
  }

  if (g_app_uplink_service_report.pending_valid == 0U)
  {
    return;
  }

  if ((s_retry_waiting != 0U) &&
      ((uint32_t)(now_ms - s_last_start_attempt_at_ms) <
       APP_UPLINK_RETRY_PERIOD_MS))
  {
    return;
  }

  format_result = App_SnapshotTextFormatter_Format(
    &s_pending_message,
    s_tx_buffer,
    sizeof(s_tx_buffer),
    &output_length);
  g_app_uplink_service_report.last_format_result = format_result;
  if (format_result != APP_UPLINK_TEXT_RESULT_OK)
  {
    ++g_app_uplink_service_report.format_error_count;
    g_app_uplink_service_report.has_latest_snapshot = 0U;
    g_app_uplink_service_report.pending_valid = 0U;
    g_app_uplink_service_report.pending_reason =
      APP_UPLINK_REASON_NOT_SET;
    s_retry_waiting = 0U;
    return;
  }

  s_last_start_attempt_at_ms = now_ms;
  tx_status = BSP_UART1_WriteAsync((const uint8_t *)s_tx_buffer,
                                   (uint16_t)output_length);
  if (tx_status == BSP_UART_STATUS_OK)
  {
    ++g_app_uplink_service_report.tx_start_count;
    g_app_uplink_service_report.state =
      APP_UPLINK_SERVICE_STATE_SENDING;
    g_app_uplink_service_report.active_reason =
      s_pending_message.reason;
    g_app_uplink_service_report.pending_valid = 0U;
    g_app_uplink_service_report.pending_reason =
      APP_UPLINK_REASON_NOT_SET;
    s_retry_waiting = 0U;
  }
  else
  {
    s_retry_waiting = 1U;
    if (tx_status == BSP_UART_STATUS_BUSY)
    {
      ++g_app_uplink_service_report.tx_busy_count;
    }
    else
    {
      ++g_app_uplink_service_report.tx_error_count;
    }
  }
}
