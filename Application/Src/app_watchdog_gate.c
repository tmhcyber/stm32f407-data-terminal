#include "app_watchdog_gate.h"

#include <string.h>

static App_WatchdogGateReport_t s_report;

volatile uint32_t g_app_watchdog_gate_report_version;
volatile App_WatchdogGateReport_t g_app_watchdog_gate_report;

static void App_WatchdogGate_Publish(void)
{
  ++g_app_watchdog_gate_report_version;
  g_app_watchdog_gate_report = s_report;
  ++g_app_watchdog_gate_report_version;
}

static void App_WatchdogGate_SetDecision(
  App_WatchdogGateDecision_t decision,
  uint32_t now_ms)
{
  if (decision != s_report.decision)
  {
    s_report.decision = decision;
    s_report.decision_since_ms = now_ms;
    ++s_report.decision_transition_count;
  }
}

static void App_WatchdogGate_LatchInternalReason(uint32_t reason,
                                                 uint32_t now_ms)
{
  s_report.latched_reasons |= reason;
  App_WatchdogGate_SetDecision(APP_WATCHDOG_GATE_DECISION_LATCHED,
                               now_ms);
}

App_WatchdogGateResult_t App_WatchdogGate_Init(uint32_t now_ms)
{
  (void)memset(&s_report, 0, sizeof(s_report));
  g_app_watchdog_gate_report_version = 0U;
  s_report.initialized = 1U;
  s_report.decision = APP_WATCHDOG_GATE_DECISION_WAIT_PERIOD;
  s_report.decision_since_ms = now_ms;
  s_report.last_successful_refresh_at_ms = now_ms;
  App_WatchdogGate_Publish();
  return APP_WATCHDOG_GATE_RESULT_OK;
}

App_WatchdogGateResult_t App_WatchdogGate_Evaluate(
  const App_WatchdogGateInputs_t *inputs,
  uint32_t now_ms,
  App_WatchdogGateDecision_t *decision)
{
  uint32_t new_latch_reasons;
  uint32_t unknown_reasons;

  if (s_report.initialized == 0U)
  {
    return APP_WATCHDOG_GATE_RESULT_NOT_INITIALIZED;
  }
  if ((inputs == 0) || (decision == 0))
  {
    App_WatchdogGate_LatchInternalReason(
      APP_WATCHDOG_GATE_INTERNAL_INPUT_INVALID,
      now_ms);
    App_WatchdogGate_Publish();
    return APP_WATCHDOG_GATE_RESULT_INVALID_ARGUMENT;
  }

  ++s_report.evaluation_count;
  s_report.active_reasons = inputs->active_reasons;
  unknown_reasons = inputs->active_reasons &
                    ~APP_WATCHDOG_GATE_KNOWN_REASONS;
  s_report.unknown_active_reasons |= unknown_reasons;
  if (inputs->monitor_evaluation_valid == 0U)
  {
    App_WatchdogGate_LatchInternalReason(
      APP_WATCHDOG_GATE_INTERNAL_INPUT_INVALID,
      now_ms);
  }
  if (unknown_reasons != 0U)
  {
    App_WatchdogGate_LatchInternalReason(
      APP_WATCHDOG_GATE_INTERNAL_UNKNOWN_REASON,
      now_ms);
  }

  new_latch_reasons = inputs->active_reasons &
                      APP_WATCHDOG_GATE_LATCH_REASONS;
  s_report.latched_reasons |= new_latch_reasons;
  s_report.active_hold_reasons = inputs->active_reasons &
                                 APP_WATCHDOG_GATE_HOLD_REASONS;

  if (s_report.latched_reasons != 0U)
  {
    App_WatchdogGate_SetDecision(APP_WATCHDOG_GATE_DECISION_LATCHED,
                                 now_ms);
  }
  else if (s_report.active_hold_reasons != 0U)
  {
    App_WatchdogGate_SetDecision(APP_WATCHDOG_GATE_DECISION_HOLD,
                                 now_ms);
  }
  else if ((uint32_t)(now_ms -
                      s_report.last_successful_refresh_at_ms) >=
           APP_WATCHDOG_GATE_REFRESH_PERIOD_MS)
  {
    App_WatchdogGate_SetDecision(APP_WATCHDOG_GATE_DECISION_REFRESH_DUE,
                                 now_ms);
  }
  else
  {
    App_WatchdogGate_SetDecision(APP_WATCHDOG_GATE_DECISION_WAIT_PERIOD,
                                 now_ms);
  }

  *decision = s_report.decision;
  App_WatchdogGate_Publish();
  if (inputs->monitor_evaluation_valid == 0U)
  {
    return APP_WATCHDOG_GATE_RESULT_INVALID_INPUT;
  }
  if (unknown_reasons != 0U)
  {
    return APP_WATCHDOG_GATE_RESULT_INVALID_INPUT;
  }
  return APP_WATCHDOG_GATE_RESULT_OK;
}

App_WatchdogGateResult_t App_WatchdogGate_RecordRefreshResult(
  App_WatchdogGateRefreshResult_t refresh_result,
  uint32_t now_ms)
{
  if (s_report.initialized == 0U)
  {
    return APP_WATCHDOG_GATE_RESULT_NOT_INITIALIZED;
  }
  if ((refresh_result != APP_WATCHDOG_GATE_REFRESH_SUCCESS) &&
      (refresh_result != APP_WATCHDOG_GATE_REFRESH_FAILURE))
  {
    App_WatchdogGate_LatchInternalReason(
      APP_WATCHDOG_GATE_INTERNAL_INPUT_INVALID,
      now_ms);
    App_WatchdogGate_Publish();
    return APP_WATCHDOG_GATE_RESULT_INVALID_ARGUMENT;
  }
  if (s_report.decision != APP_WATCHDOG_GATE_DECISION_REFRESH_DUE)
  {
    App_WatchdogGate_LatchInternalReason(
      APP_WATCHDOG_GATE_INTERNAL_INPUT_INVALID,
      now_ms);
    App_WatchdogGate_Publish();
    return APP_WATCHDOG_GATE_RESULT_INVALID_STATE;
  }

  if (refresh_result == APP_WATCHDOG_GATE_REFRESH_SUCCESS)
  {
    s_report.last_successful_refresh_at_ms = now_ms;
    ++s_report.refresh_completed_count;
    App_WatchdogGate_SetDecision(APP_WATCHDOG_GATE_DECISION_WAIT_PERIOD,
                                 now_ms);
    App_WatchdogGate_Publish();
    return APP_WATCHDOG_GATE_RESULT_OK;
  }

  ++s_report.refresh_failed_count;
  App_WatchdogGate_LatchInternalReason(
    APP_WATCHDOG_GATE_INTERNAL_REFRESH_FAILED,
    now_ms);
  App_WatchdogGate_Publish();
  return APP_WATCHDOG_GATE_RESULT_OK;
}
