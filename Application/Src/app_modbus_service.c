#include "app_modbus_service.h"
#include "app_modbus_registers.h"
#include "app_data_service.h"
#include "bsp_rs485.h"
#include "bsp_modbus_time.h"

#define MODBUS_SLAVE 1U
#define MODBUS_TX_TIMEOUT_US 10000U

volatile App_ModbusReport_t g_app_modbus_report;
/* Framing counters and state are available in Keil Watch. */
MB_RTU_Receiver_t g_app_modbus_receiver;
static uint8_t s_response[MB_RTU_MAX_ADU];
static uint32_t s_tx_started_us;

static MB_Exception_t ReadInput(void *context, uint16_t start,
  uint16_t quantity, uint16_t *registers, uint16_t capacity)
{
  App_DataSnapshot_t snapshot;
  App_ModbusSample_t sample = {0, 0, 0U};
  const App_DataPoint_t *temperature = 0;
  const App_DataPoint_t *humidity = 0;
  uint32_t i;
  (void)context;
  if ((App_DataService_GetSnapshot(&snapshot) != APP_DATA_RESULT_OK) ||
      (snapshot.point_count != APP_DATA_POINT_COUNT)) goto failed;
  for (i = 0U; i < snapshot.point_count; ++i)
  {
    const App_DataPoint_t *point = &snapshot.points[i];
    if (point->source_id != APP_DATA_SOURCE_SHT30) continue;
    if (point->point_id == APP_DATA_POINT_TEMPERATURE) temperature = point;
    if (point->point_id == APP_DATA_POINT_HUMIDITY) humidity = point;
  }
  if ((temperature == 0) || (humidity == 0) ||
      (temperature->scale != APP_DATA_SCALE_MILLI) ||
      (humidity->scale != APP_DATA_SCALE_MILLI) ||
      (temperature->unit != APP_DATA_UNIT_DEGREE_CELSIUS) ||
      (humidity->unit != APP_DATA_UNIT_PERCENT_RH)) goto failed;
  if ((temperature->sequence != 0U) && (humidity->sequence != 0U))
  {
    if ((temperature->sequence != humidity->sequence) ||
        (temperature->quality != humidity->quality)) goto failed;
    switch (temperature->quality)
    {
      case APP_DATA_QUALITY_NOT_AVAILABLE: sample.quality = 0U; break;
      case APP_DATA_QUALITY_VALID: sample.quality = 1U; break;
      case APP_DATA_QUALITY_STALE: sample.quality = 2U; break;
      case APP_DATA_QUALITY_OFFLINE: sample.quality = 3U; break;
      default: goto failed;
    }
    sample.temperature_milli = temperature->raw_value;
    sample.humidity_milli = humidity->raw_value;
  }
  return App_ModbusRegisters_Read(&sample, start, quantity, registers, capacity);
failed:
  ++g_app_modbus_report.snapshot_errors;
  return MB_EX_DEVICE_FAILURE;
}

uint8_t App_ModbusService_Init(void)
{
  App_ModbusReport_t empty = {0};
  MB_RTU_Receiver_t empty_receiver = {{0}, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
  g_app_modbus_report = empty;
  g_app_modbus_receiver = empty_receiver;
  if (BSP_RS485_Init() != BSP_RS485_STATUS_OK) return 0U;
  MB_RTU_RxReset(&g_app_modbus_receiver, BSP_ModbusTime_Micros());
  g_app_modbus_report.initialized = 1U;
  return 1U;
}

void App_ModbusService_Process(void)
{
  BSP_RS485_RxByteEvent_t byte;
  BSP_RS485_RxFaultEvent_t fault;
  BSP_RS485_TxEvent_t tx;
  uint16_t length, response_length;
  uint32_t now_us;
  uint32_t budget;
  if (g_app_modbus_report.initialized == 0U) return;
  fault = BSP_RS485_TakeRxFaultEvent();
  if ((fault.overflow_count != 0U) || (fault.uart_error_count != 0U))
  {
    g_app_modbus_report.rx_faults += fault.overflow_count + fault.uart_error_count;
    (void)BSP_RS485_FlushRx();
    MB_RTU_RxReset(&g_app_modbus_receiver, BSP_ModbusTime_Micros());
  }
  /* Consume actual TC before wall time: a delayed loop does not turn an
   * on-time hardware completion into a transmit timeout. */
  tx = BSP_RS485_TakeTxEvent();
  if (g_app_modbus_report.transmitting != 0U)
  {
    now_us = BSP_ModbusTime_Micros();
    if (tx.type != BSP_RS485_TX_EVENT_NONE)
    {
      if ((tx.type == BSP_RS485_TX_EVENT_COMPLETE) &&
          ((uint32_t)(tx.timestamp_us - s_tx_started_us) < MODBUS_TX_TIMEOUT_US))
        ++g_app_modbus_report.responses_completed;
      else ++g_app_modbus_report.tx_errors;
      g_app_modbus_report.transmitting = 0U;
      /* Preserve remote bytes queued after actual TC. */
      MB_RTU_RxReset(&g_app_modbus_receiver, tx.timestamp_us);
    }
    else if ((uint32_t)(now_us - s_tx_started_us) >= MODBUS_TX_TIMEOUT_US)
    {
      (void)BSP_RS485_AbortTransmit();
      (void)BSP_RS485_FlushRx();
      ++g_app_modbus_report.tx_timeouts;
      g_app_modbus_report.transmitting = 0U;
      MB_RTU_RxReset(&g_app_modbus_receiver, BSP_ModbusTime_Micros());
    }
    if (g_app_modbus_report.transmitting != 0U) return;
  }
  /* A continuous invalid stream cannot monopolize the main loop. */
  for (budget = 0U; budget < BSP_RS485_RX_QUEUE_CAPACITY; ++budget)
  {
    if (BSP_RS485_TakeRxByte(&byte) == 0U) break;
    MB_RTU_RxByte(&g_app_modbus_receiver, byte.byte, byte.timestamp_us);
  }
  if (BSP_RS485_GetQuietTime(&now_us) == 0U) return;
  length = MB_RTU_RxTake(&g_app_modbus_receiver, now_us);
  if (length == 0U) return;
  ++g_app_modbus_report.frames;
  response_length = MB_RTU_Handle(g_app_modbus_receiver.bytes, length,
    MODBUS_SLAVE, ReadInput, 0, s_response, sizeof(s_response));
  if (response_length == 0U)
  {
    ++g_app_modbus_report.silent_frames;
    return;
  }
  if ((s_response[1] & 0x80U) != 0U) ++g_app_modbus_report.exceptions;
  s_tx_started_us = BSP_ModbusTime_Micros();
  if (BSP_RS485_WriteAsync(s_response, response_length) == BSP_RS485_STATUS_OK)
  {
    ++g_app_modbus_report.responses_started;
    g_app_modbus_report.transmitting = 1U;
  }
  else
  {
    ++g_app_modbus_report.tx_errors;
    MB_RTU_RxReset(&g_app_modbus_receiver, BSP_ModbusTime_Micros());
  }
}
