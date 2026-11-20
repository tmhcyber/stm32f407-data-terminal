#ifndef APP_RTOS_RUNTIME_H
#define APP_RTOS_RUNTIME_H

#include <stdint.h>

typedef enum
{
  APP_RTOS_COMMUNICATION = 0,
  APP_RTOS_COLLECTOR,
  APP_RTOS_UPLINK,
  APP_RTOS_MONITOR,
  APP_RTOS_TASK_COUNT
} App_RtosTaskId_t;

typedef struct
{
  uint32_t completed_count;
  uint32_t last_completed_at_ms;
  uint32_t max_progress_gap_ms;
  uint32_t max_service_cycles;
  uint32_t stack_min_free_words;
} App_RtosTaskReport_t;

typedef struct
{
  App_RtosTaskReport_t task[APP_RTOS_TASK_COUNT];
  uint32_t scheduler_started_at_ms;
  uint32_t warning_mask;
  uint32_t stalled_mask;
  uint32_t heap_free_bytes;
  uint32_t heap_min_free_bytes;
} App_RtosReport_t;

extern volatile App_RtosReport_t g_app_rtos_report;

void App_RTOS_Run(uint8_t uplink_init_failed);
void App_RTOS_GetReport(App_RtosReport_t *report);

/* Core-owned service entry points shared with the bare-metal scheduler. */
void App_SnapshotEvent_ReportProcess(void);
void App_SystemWatchdog_Process(uint32_t now_ms);

#endif
