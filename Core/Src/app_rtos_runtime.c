#include "app_rtos_runtime.h"

#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "app_diagnostic.h"
#include "app_modbus_service.h"
#include "app_sht30_collector.h"
#include "app_uplink_service.h"
#include "app_watchdog_gate.h"
#include "bsp_can_link_test.h"
#include "bsp_rs485.h"
#include "bsp_service_notify.h"
#include "bsp_system_diagnostics.h"
#include "bsp_system_timing.h"
#include "bsp_system_watchdog.h"

#define APP_RTOS_STARTUP_GRACE_MS  100U
#define APP_RTOS_MONITOR_PERIOD_MS 10U
#define APP_RTOS_CAN_BUDGET        3U
#define APP_RTOS_FAILURE_MAGIC     0x52544F53UL

typedef enum
{
  APP_RTOS_FAILURE_ASSERT = 1,
  APP_RTOS_FAILURE_ALLOCATION,
  APP_RTOS_FAILURE_STACK,
  APP_RTOS_FAILURE_SCHEDULER,
  APP_RTOS_FAILURE_IWDG,
  APP_RTOS_FAILURE_GATE
} App_RtosFailure_t;

typedef struct
{
  uint32_t reason;
  uint32_t detail;
} App_RtosFailureRecord_t;

typedef struct
{
  App_SystemMonitorReport_t monitor;
  App_WatchdogGateReport_t gate;
} App_RtosHealthReport_t;

extern BSP_SystemBootReport_t g_bsp_system_boot_report;
extern volatile App_SystemMonitorResult_t g_app_system_monitor_process_status;
extern volatile BSP_SystemWatchdogStatus_t g_bsp_system_watchdog_init_status;
extern volatile App_WatchdogGateResult_t g_app_watchdog_gate_init_status;

volatile App_RtosReport_t g_app_rtos_report;
volatile App_RtosFailureRecord_t g_app_rtos_previous_failure;
const char * volatile g_app_rtos_assert_file;
/* 0 at every boot. 1-3 hold a worker at its loop boundary, 4-5 cause a
 * one-shot collector UsageFault (basic / with an active FP context). */
volatile uint32_t g_app_rtos_test_request;

__attribute__((section("NoInit"), zero_init, aligned(8), used))
static volatile struct
{
  uint32_t magic;
  App_RtosFailureRecord_t record;
} s_retained_failure;

static TaskHandle_t s_tasks[APP_RTOS_TASK_COUNT];
static volatile App_SystemHealthInputs_t s_health;
static volatile App_RtosHealthReport_t s_health_report;
static volatile uint32_t s_pending_timing;
static volatile uint8_t s_notifications_ready;

static void App_RTOS_Fail(App_RtosFailure_t reason, uint32_t detail)
{
  __disable_irq();
  s_retained_failure.magic = 0U;
  s_retained_failure.record.reason = (uint32_t)reason;
  s_retained_failure.record.detail = detail;
  __DMB();
  s_retained_failure.magic = APP_RTOS_FAILURE_MAGIC;
  __DSB();
  Error_Handler();
}

void App_RTOS_AssertFailed(uint32_t line, const char *file)
{
  g_app_rtos_assert_file = file;
  App_RTOS_Fail(APP_RTOS_FAILURE_ASSERT, line);
}

void vApplicationMallocFailedHook(void)
{
  App_RTOS_Fail(APP_RTOS_FAILURE_ALLOCATION, 0U);
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
  unsigned index;
  (void)name;
  for (index = 0U; index < APP_RTOS_TASK_COUNT; ++index)
  {
    if (s_tasks[index] == task) break;
  }
  App_RTOS_Fail(APP_RTOS_FAILURE_STACK, index);
}

static void App_RTOS_NotifyFromISR(TaskHandle_t task)
{
  BaseType_t higher_priority_woken = pdFALSE;
  if ((s_notifications_ready != 0U) && (task != NULL))
  {
    vTaskNotifyGiveFromISR(task, &higher_priority_woken);
    portYIELD_FROM_ISR(higher_priority_woken);
  }
}

void BSP_ServiceNotify_CommunicationFromISR(void)
{
  App_RTOS_NotifyFromISR(s_tasks[APP_RTOS_COMMUNICATION]);
}

void BSP_ServiceNotify_UplinkFromISR(void)
{
  App_RTOS_NotifyFromISR(s_tasks[APP_RTOS_UPLINK]);
}

static uint32_t App_RTOS_TimingReason(App_RtosTaskId_t task, uint32_t age_ms)
{
  const uint32_t warning_ms = (task == APP_RTOS_COMMUNICATION) ? 5U : 50U;
  const uint32_t hard_ms = (task == APP_RTOS_COMMUNICATION) ? 25U : 100U;
  if (age_ms >= hard_ms) return APP_SYSTEM_HEALTH_REASON_LOOP_HARD_OVERRUN;
  if (age_ms > warning_ms) return APP_SYSTEM_HEALTH_REASON_LOOP_WARNING;
  return APP_SYSTEM_HEALTH_REASON_NONE;
}

/* Caller holds the short critical section that also publishes service health. */
static void App_RTOS_RecordProgress(App_RtosTaskId_t task, uint32_t elapsed_cycles)
{
  volatile App_RtosTaskReport_t *report = &g_app_rtos_report.task[task];
  uint32_t now_ms = HAL_GetTick();
  uint32_t gap_ms = now_ms - report->last_completed_at_ms;
  if ((task != APP_RTOS_MONITOR) &&
      ((uint32_t)(now_ms - g_app_rtos_report.scheduler_started_at_ms) >=
       APP_RTOS_STARTUP_GRACE_MS))
  {
    s_pending_timing |= App_RTOS_TimingReason(task, gap_ms);
  }
  if (gap_ms > report->max_progress_gap_ms) report->max_progress_gap_ms = gap_ms;
  if (elapsed_cycles > report->max_service_cycles) report->max_service_cycles = elapsed_cycles;
  report->last_completed_at_ms = now_ms;
  ++report->completed_count;
}

void App_RTOS_GetReport(App_RtosReport_t *report)
{
  taskENTER_CRITICAL();
  *report = g_app_rtos_report;
  taskEXIT_CRITICAL();
}

static void App_RTOS_CheckHold(App_RtosTaskId_t task)
{
  if (g_app_rtos_test_request == (uint32_t)task + 1U)
  {
    g_app_rtos_test_request = 0U;
    for (;;) vTaskDelay(1U);
  }
}

__asm static void App_RTOS_InjectUsageFault(uint32_t use_fp)
{
  CMP     R0, #0
  BEQ     fault_instruction
  VMOV    S0, R0
fault_instruction
  DCI     0xDE00
  BX      LR
}

static void App_RTOS_CommunicationTask(void *argument)
{
  TickType_t budget_tick = xTaskGetTickCount();
  uint32_t rx_budget = BSP_RS485_RX_QUEUE_CAPACITY;
  uint32_t can_budget = APP_RTOS_CAN_BUDGET;
  (void)argument;
  for (;;)
  {
    TickType_t tick;
    uint32_t start_cycles;
    uint32_t elapsed_cycles;
    App_RTOS_CheckHold(APP_RTOS_COMMUNICATION);
    tick = xTaskGetTickCount();
    if (tick != budget_tick)
    {
      budget_tick = tick;
      rx_budget = BSP_RS485_RX_QUEUE_CAPACITY;
      can_budget = APP_RTOS_CAN_BUDGET;
    }
    start_cycles = BSP_SystemTiming_GetCycles();
    rx_budget -= App_ModbusService_ProcessBudget(rx_budget);
    while ((can_budget != 0U) &&
           (BSP_CAN_LinkTest_ProcessOne(HAL_GetTick()) != 0U))
    {
      --can_budget;
    }
    elapsed_cycles = BSP_SystemTiming_GetCycles() - start_cycles;
    taskENTER_CRITICAL();
    App_RTOS_RecordProgress(APP_RTOS_COMMUNICATION, elapsed_cycles);
    taskEXIT_CRITICAL();
    if ((rx_budget == 0U) || (can_budget == 0U))
    {
      vTaskDelay(1U);
      (void)ulTaskNotifyTake(pdTRUE, 0U);
    }
    else
    {
      (void)ulTaskNotifyTake(pdTRUE, 1U);
    }
  }
}

static void App_RTOS_CollectorTask(void *argument)
{
  (void)argument;
  for (;;)
  {
    App_SHT30_CollectorReport_t collector;
    uint32_t start_cycles;
    uint32_t elapsed_cycles;
    uint32_t request;
    App_RTOS_CheckHold(APP_RTOS_COLLECTOR);
    request = g_app_rtos_test_request;
    if ((request == 4U) || (request == 5U))
    {
      g_app_rtos_test_request = 0U;
      App_RTOS_InjectUsageFault(request - 4U);
    }
    start_cycles = BSP_SystemTiming_GetCycles();
    App_SHT30_Collector_Process(HAL_GetTick());
    elapsed_cycles = BSP_SystemTiming_GetCycles() - start_cycles;
    collector = g_app_sht30_collector_report;
    taskENTER_CRITICAL();
    s_health.collector_fault = (uint8_t)(collector.state == APP_SHT30_COLLECTOR_STATE_FAULT);
    s_health.has_valid_data = collector.has_valid_measurement;
    s_health.source_stale = (uint8_t)(collector.quality == APP_SHT30_QUALITY_STALE);
    s_health.source_offline = (uint8_t)(collector.quality == APP_SHT30_QUALITY_OFFLINE);
    s_health.last_success_at_ms = collector.last_success_at_ms;
    App_RTOS_RecordProgress(APP_RTOS_COLLECTOR, elapsed_cycles);
    taskEXIT_CRITICAL();
    vTaskDelay(1U);
  }
}

static void App_RTOS_UplinkTask(void *argument)
{
  (void)argument;
  for (;;)
  {
    App_UplinkServiceReport_t uplink;
    uint32_t start_cycles;
    uint32_t elapsed_cycles;
    App_RTOS_CheckHold(APP_RTOS_UPLINK);
    start_cycles = BSP_SystemTiming_GetCycles();
    App_SnapshotEvent_ReportProcess();
    if (App_UplinkService_DiagnosticDue(HAL_GetTick()))
    {
      App_RtosHealthReport_t health_report;
      App_Diagnostic_t diagnostic;
      taskENTER_CRITICAL();
      health_report = s_health_report;
      taskEXIT_CRITICAL();
      App_Diagnostic_Map(&diagnostic, &g_bsp_system_boot_report,
                         &health_report.monitor, &health_report.gate, HAL_GetTick());
      App_UplinkService_SubmitDiagnostic(&diagnostic);
    }
    App_UplinkService_ProcessCurrent(HAL_GetTick);
    elapsed_cycles = BSP_SystemTiming_GetCycles() - start_cycles;
    uplink = g_app_uplink_service_report;
    taskENTER_CRITICAL();
    s_health.has_completed_tx = uplink.has_successful_transmission;
    s_health.last_completed_at_ms = uplink.last_completed_at_ms;
    s_health.uplink_completed_count = uplink.completed_count;
    s_health.uplink_error_count = uplink.tx_error_count;
    App_RTOS_RecordProgress(APP_RTOS_UPLINK, elapsed_cycles);
    taskEXIT_CRITICAL();
    (void)ulTaskNotifyTake(pdTRUE, 1U);
  }
}

static void App_RTOS_ReadResources(void)
{
  unsigned index;
  /* Highest priority task is the only writer and no worker can run here. */
  for (index = 0U; index < APP_RTOS_TASK_COUNT; ++index)
  {
    g_app_rtos_report.task[index].stack_min_free_words =
      (uint32_t)uxTaskGetStackHighWaterMark(s_tasks[index]);
  }
  g_app_rtos_report.heap_free_bytes = (uint32_t)xPortGetFreeHeapSize();
  g_app_rtos_report.heap_min_free_bytes = (uint32_t)xPortGetMinimumEverFreeHeapSize();
}

static void App_RTOS_MonitorTask(void *argument)
{
  TickType_t period_tick;
  uint32_t resources_at_ms;
  unsigned index;
  (void)argument;
  g_app_rtos_report.scheduler_started_at_ms = HAL_GetTick();
  for (index = 0U; index < APP_RTOS_TASK_COUNT; ++index)
  {
    g_app_rtos_report.task[index].last_completed_at_ms =
      g_app_rtos_report.scheduler_started_at_ms;
  }
  g_bsp_system_watchdog_init_status = BSP_SystemWatchdog_Init();
  if (g_bsp_system_watchdog_init_status != BSP_SYSTEM_WATCHDOG_STATUS_OK)
  {
    App_RTOS_Fail(APP_RTOS_FAILURE_IWDG, (uint32_t)g_bsp_system_watchdog_init_status);
  }
  g_app_watchdog_gate_init_status = App_WatchdogGate_Init(HAL_GetTick());
  if (g_app_watchdog_gate_init_status != APP_WATCHDOG_GATE_RESULT_OK)
  {
    App_RTOS_Fail(APP_RTOS_FAILURE_GATE, (uint32_t)g_app_watchdog_gate_init_status);
  }
  s_notifications_ready = 1U;
  resources_at_ms = HAL_GetTick() - 1000U;
  period_tick = xTaskGetTickCount();
  for (;;)
  {
    uint32_t start_cycles = BSP_SystemTiming_GetCycles();
    App_SystemMonitorReport_t monitor_report;
    uint32_t timing_reasons = s_pending_timing;
    App_SystemHealthInputs_t health = s_health;
    uint32_t now_ms = HAL_GetTick();
    uint32_t warning_mask = 0U;
    uint32_t stalled_mask = 0U;
    s_pending_timing = 0U;

    if ((uint32_t)(now_ms - g_app_rtos_report.scheduler_started_at_ms) >=
        APP_RTOS_STARTUP_GRACE_MS)
    {
      for (index = 0U; index < APP_RTOS_MONITOR; ++index)
      {
        uint32_t age_ms = now_ms - g_app_rtos_report.task[index].last_completed_at_ms;
        uint32_t reason = App_RTOS_TimingReason((App_RtosTaskId_t)index, age_ms);
        if (reason == APP_SYSTEM_HEALTH_REASON_LOOP_WARNING) warning_mask |= 1UL << index;
        if (reason == APP_SYSTEM_HEALTH_REASON_LOOP_HARD_OVERRUN) stalled_mask |= 1UL << index;
        timing_reasons |= reason;
      }
    }
    g_app_rtos_report.warning_mask = warning_mask;
    g_app_rtos_report.stalled_mask = stalled_mask;
    g_app_system_monitor_process_status =
      App_SystemMonitor_ProcessTaskTiming(timing_reasons, &health, now_ms);
    App_SystemWatchdog_Process(HAL_GetTick());
    if (App_SystemMonitor_GetReport(&monitor_report) == APP_SYSTEM_MONITOR_RESULT_OK)
    {
      /* Workers cannot preempt this highest-priority producer. The reader
       * copies under a critical section so this pair stays coherent. */
      s_health_report.monitor = monitor_report;
      s_health_report.gate = g_app_watchdog_gate_report;
    }
    if ((uint32_t)(now_ms - resources_at_ms) >= 1000U)
    {
      resources_at_ms = now_ms;
      App_RTOS_ReadResources();
    }
    App_RTOS_RecordProgress(APP_RTOS_MONITOR, BSP_SystemTiming_GetCycles() - start_cycles);
    (void)xTaskDelayUntil(&period_tick, pdMS_TO_TICKS(APP_RTOS_MONITOR_PERIOD_MS));
  }
}

void App_RTOS_Run(uint8_t uplink_init_failed)
{
  static const struct
  {
    TaskFunction_t function;
    const char *name;
    uint16_t stack_words;
    UBaseType_t priority;
  } tasks[APP_RTOS_TASK_COUNT] = {
    { App_RTOS_CommunicationTask, "Communication", 768U, 3U },
    { App_RTOS_CollectorTask, "Collector", 512U, 2U },
    { App_RTOS_UplinkTask, "Uplink", 1024U, 1U },
    { App_RTOS_MonitorTask, "Monitor", 512U, 4U }
  };
  unsigned index;
  if ((s_retained_failure.magic == APP_RTOS_FAILURE_MAGIC) &&
      (s_retained_failure.record.reason >= APP_RTOS_FAILURE_ASSERT) &&
      (s_retained_failure.record.reason <= APP_RTOS_FAILURE_GATE))
  {
    g_app_rtos_previous_failure = s_retained_failure.record;
  }
  s_retained_failure.magic = 0U;
  s_health.uplink_init_failed = uplink_init_failed;
  for (index = 0U; index < APP_RTOS_TASK_COUNT; ++index)
  {
    if (xTaskCreate(tasks[index].function, tasks[index].name, tasks[index].stack_words,
                    NULL, tasks[index].priority, &s_tasks[index]) != pdPASS)
    {
      App_RTOS_Fail(APP_RTOS_FAILURE_ALLOCATION, index);
      return;
    }
  }
  vTaskStartScheduler();
  App_RTOS_Fail(APP_RTOS_FAILURE_SCHEDULER, 0U);
}
