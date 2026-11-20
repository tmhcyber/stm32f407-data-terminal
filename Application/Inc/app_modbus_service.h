#ifndef APP_MODBUS_SERVICE_H
#define APP_MODBUS_SERVICE_H
#include <stdint.h>

typedef struct
{
  uint32_t frames;
  uint32_t silent_frames;
  uint32_t responses_started;
  uint32_t responses_completed;
  uint32_t exceptions;
  uint32_t rx_faults;
  uint32_t tx_errors;
  uint32_t tx_timeouts;
  uint32_t snapshot_errors;
  uint8_t initialized;
  uint8_t transmitting;
} App_ModbusReport_t;

extern volatile App_ModbusReport_t g_app_modbus_report;
uint8_t App_ModbusService_Init(void);
void App_ModbusService_Process(void);
uint32_t App_ModbusService_ProcessBudget(uint32_t byte_limit);
#endif
