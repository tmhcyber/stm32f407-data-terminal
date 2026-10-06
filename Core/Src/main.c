#include "main.h"
#include "app_bringup.h"
#include "app_data_service.h"
#include "app_rs485_service.h"
#include "app_sht30_collector.h"
#include "app_snapshot_consumer.h"
#include "app_system_monitor.h"
#include "app_uplink_service.h"
#include "app_watchdog_gate.h"
#include "bsp_system_diagnostics.h"
#include "bsp_system_timing.h"
#include "bsp_system_watchdog.h"
#include "bsp_can_loopback.h"
#include "bsp_can_link_test.h"
#include "app_can_service.h"

/* 1: external fixed-frame experiment; 0: original HSI silent loopback.
 * Rebuild and reset after changing. Never initialize both CAN2 owners.
 */
#define APP_CAN_EXTERNAL_TEST 1

volatile uint32_t g_app_snapshot_event_report_version;
volatile uint32_t g_app_snapshot_event_report_count;
volatile App_DataResult_t g_app_snapshot_event_report_status;
volatile App_SnapshotEvent_t g_app_snapshot_event_report;
volatile App_UplinkServiceResult_t g_app_uplink_init_status;
volatile App_UplinkServiceResult_t g_app_uplink_submit_status;
volatile uint8_t g_app_uplink_heartbeat_test_hold_collection;
volatile App_RS485ServiceResult_t g_app_rs485_init_status;
volatile BSP_SystemTimingStatus_t g_bsp_system_timing_init_status;
volatile App_SystemMonitorResult_t g_app_system_monitor_init_status;
volatile App_SystemMonitorResult_t g_app_system_monitor_process_status;
volatile App_WatchdogGateResult_t g_app_watchdog_gate_init_status;
volatile App_WatchdogGateResult_t g_app_watchdog_gate_evaluate_status;
volatile App_WatchdogGateResult_t g_app_watchdog_gate_refresh_record_status;
volatile BSP_SystemWatchdogStatus_t g_bsp_system_watchdog_init_status;
volatile BSP_SystemWatchdogStatus_t g_bsp_system_watchdog_refresh_status;
BSP_SystemBootReport_t g_bsp_system_boot_report;

static App_SnapshotConsumer_t s_app_snapshot_consumer;

static void App_SystemMonitor_FillHealthInputs(
  App_SystemHealthInputs_t *health)
{
  health->collector_fault =
    (uint8_t)(g_app_sht30_collector_report.state ==
              APP_SHT30_COLLECTOR_STATE_FAULT);
  health->has_valid_data =
    (uint8_t)(g_app_sht30_collector_report.has_valid_measurement != 0U);
  health->source_stale =
    (uint8_t)(g_app_sht30_collector_report.quality ==
              APP_SHT30_QUALITY_STALE);
  health->source_offline =
    (uint8_t)(g_app_sht30_collector_report.quality ==
              APP_SHT30_QUALITY_OFFLINE);
  health->last_success_at_ms =
    g_app_sht30_collector_report.last_success_at_ms;
  health->uplink_init_failed =
    (uint8_t)(g_app_uplink_init_status != APP_UPLINK_SERVICE_RESULT_OK);
  health->has_completed_tx =
    g_app_uplink_service_report.has_successful_transmission;
  health->last_completed_at_ms =
    g_app_uplink_service_report.last_completed_at_ms;
  health->uplink_completed_count =
    g_app_uplink_service_report.completed_count;
  health->uplink_error_count =
    g_app_uplink_service_report.tx_error_count;
}

static void App_SnapshotEvent_ReportProcess(void)
{
  App_SnapshotEvent_t event;
  App_DataResult_t result;
  uint8_t event_ready;

  result = App_SnapshotConsumer_Process(&s_app_snapshot_consumer,
                                        &event,
                                        &event_ready);
  g_app_snapshot_event_report_status = result;
  if ((result != APP_DATA_RESULT_OK) || (event_ready == 0U))
  {
    return;
  }

  ++g_app_snapshot_event_report_version;
  g_app_snapshot_event_report = event;
  ++g_app_snapshot_event_report_count;
  ++g_app_snapshot_event_report_version;
  g_app_uplink_submit_status = App_UplinkService_SubmitEvent(&event);
}

static void App_SystemWatchdog_Process(uint32_t now_ms)
{
  App_SystemMonitorReport_t monitor_report;
  App_SystemMonitorResult_t monitor_report_status;
  App_WatchdogGateInputs_t gate_inputs;
  App_WatchdogGateDecision_t gate_decision;
  App_WatchdogGateRefreshResult_t refresh_result;
  uint32_t refresh_completed_at_ms;

  monitor_report_status = App_SystemMonitor_GetReport(&monitor_report);
  gate_inputs.monitor_evaluation_valid =
    (uint8_t)((g_app_system_monitor_process_status ==
               APP_SYSTEM_MONITOR_RESULT_OK) &&
              (monitor_report_status == APP_SYSTEM_MONITOR_RESULT_OK));
  if (monitor_report_status == APP_SYSTEM_MONITOR_RESULT_OK)
  {
    gate_inputs.active_reasons = monitor_report.active_reasons;
  }
  else
  {
    gate_inputs.active_reasons = APP_SYSTEM_HEALTH_REASON_NONE;
  }

  gate_decision = APP_WATCHDOG_GATE_DECISION_LATCHED;
  g_app_watchdog_gate_evaluate_status = App_WatchdogGate_Evaluate(
    &gate_inputs,
    now_ms,
    &gate_decision);
  if ((g_app_watchdog_gate_evaluate_status !=
       APP_WATCHDOG_GATE_RESULT_OK) ||
      (gate_decision != APP_WATCHDOG_GATE_DECISION_REFRESH_DUE))
  {
    return;
  }

  g_bsp_system_watchdog_refresh_status = BSP_SystemWatchdog_Refresh();
  refresh_completed_at_ms = HAL_GetTick();
  if (g_bsp_system_watchdog_refresh_status ==
      BSP_SYSTEM_WATCHDOG_STATUS_OK)
  {
    refresh_result = APP_WATCHDOG_GATE_REFRESH_SUCCESS;
  }
  else
  {
    refresh_result = APP_WATCHDOG_GATE_REFRESH_FAILURE;
  }
  g_app_watchdog_gate_refresh_record_status =
    App_WatchdogGate_RecordRefreshResult(refresh_result,
                                         refresh_completed_at_ms);
}

int main(void)
{
  uint8_t has_previous_loop_start;
  uint32_t previous_loop_start_cycles;

  BSP_SystemDiagnostics_CaptureBootReport(&g_bsp_system_boot_report);
  BSP_SystemDiagnostics_EnableConfigurableFaults();
  HAL_Init();
#if APP_CAN_EXTERNAL_TEST
  BSP_CAN_LinkTest_Init();
#else
  BSP_CAN_Loopback_Init();
#endif
  g_app_rs485_init_status = App_RS485Service_Init();
  App_Bringup_RunOnce();
  App_DataService_Init();
#if APP_CAN_EXTERNAL_TEST
  BSP_CAN_LinkTest_SetRequestHandler(App_CAN_HandleRequest);
#endif
  App_SHT30_Collector_Init();
  App_SnapshotConsumer_Init(&s_app_snapshot_consumer);
  g_app_uplink_submit_status = APP_UPLINK_SERVICE_RESULT_NOT_RUN;
  g_app_uplink_init_status = App_UplinkService_Init();
  g_app_uplink_heartbeat_test_hold_collection = 0U;
  g_app_snapshot_event_report_version = 0U;
  g_app_snapshot_event_report_count = 0U;
  g_app_snapshot_event_report_status = APP_DATA_RESULT_NOT_INITIALIZED;
  g_bsp_system_timing_init_status = BSP_SystemTiming_Init();
  if (g_bsp_system_timing_init_status == BSP_SYSTEM_TIMING_STATUS_OK)
  {
    g_app_system_monitor_init_status = App_SystemMonitor_Init(
      BSP_SystemTiming_GetCoreClockHz(),
      BSP_SystemTiming_GetReadOverheadCycles(),
      HAL_GetTick());
  }
  else
  {
    g_app_system_monitor_init_status = App_SystemMonitor_Init(0U,
                                                               0U,
                                                               HAL_GetTick());
  }
  g_app_system_monitor_process_status = APP_SYSTEM_MONITOR_RESULT_OK;
  g_bsp_system_watchdog_refresh_status =
    BSP_SYSTEM_WATCHDOG_STATUS_NOT_RUN;
  g_app_watchdog_gate_evaluate_status =
    APP_WATCHDOG_GATE_RESULT_NOT_INITIALIZED;
  g_app_watchdog_gate_refresh_record_status =
    APP_WATCHDOG_GATE_RESULT_NOT_INITIALIZED;
  g_bsp_system_watchdog_init_status = BSP_SystemWatchdog_Init();
  if (g_bsp_system_watchdog_init_status !=
      BSP_SYSTEM_WATCHDOG_STATUS_OK)
  {
    Error_Handler();
  }
  g_app_watchdog_gate_init_status = App_WatchdogGate_Init(HAL_GetTick());
  if (g_app_watchdog_gate_init_status != APP_WATCHDOG_GATE_RESULT_OK)
  {
    Error_Handler();
  }
  has_previous_loop_start = 0U;
  previous_loop_start_cycles = 0U;

  while (1)
  {
    App_SystemTimingSample_t timing;
    App_SystemHealthInputs_t health;
    uint32_t now_ms;
    uint32_t loop_started_at_cycles;
    uint32_t service_started_at_cycles;
    uint32_t service_completed_at_cycles;

    loop_started_at_cycles = BSP_SystemTiming_GetCycles();
    now_ms = HAL_GetTick();
    timing.loop_gap_valid = has_previous_loop_start;
    timing.loop_gap_cycles =
      loop_started_at_cycles - previous_loop_start_cycles;
    previous_loop_start_cycles = loop_started_at_cycles;
    has_previous_loop_start =
      (uint8_t)(g_bsp_system_timing_init_status ==
                BSP_SYSTEM_TIMING_STATUS_OK);

    App_RS485Service_Process(now_ms);
#if APP_CAN_EXTERNAL_TEST
    BSP_CAN_LinkTest_Process(HAL_GetTick());
#else
    BSP_CAN_Loopback_Process(HAL_GetTick());
#endif

    service_started_at_cycles = BSP_SystemTiming_GetCycles();
    if (g_app_uplink_heartbeat_test_hold_collection == 0U)
    {
      App_SHT30_Collector_Process(now_ms);
    }
    service_completed_at_cycles = BSP_SystemTiming_GetCycles();
    timing.collector_cycles =
      service_completed_at_cycles - service_started_at_cycles;

    service_started_at_cycles = BSP_SystemTiming_GetCycles();
    App_SnapshotEvent_ReportProcess();
    service_completed_at_cycles = BSP_SystemTiming_GetCycles();
    timing.snapshot_cycles =
      service_completed_at_cycles - service_started_at_cycles;

    service_started_at_cycles = BSP_SystemTiming_GetCycles();
    App_UplinkService_Process(now_ms);
    service_completed_at_cycles = BSP_SystemTiming_GetCycles();
    timing.uplink_cycles =
      service_completed_at_cycles - service_started_at_cycles;
    timing.loop_work_cycles =
      service_completed_at_cycles - loop_started_at_cycles;

    App_SystemMonitor_FillHealthInputs(&health);
    /* Copy event-derived timestamps before taking time used for age checks. */
    now_ms = HAL_GetTick();
    g_app_system_monitor_process_status = App_SystemMonitor_Process(
      &timing,
      &health,
      now_ms);
    App_SystemWatchdog_Process(HAL_GetTick());
  }
}

void Error_Handler(void)
{
  __disable_irq();

  while (1)
  {
  }
}
