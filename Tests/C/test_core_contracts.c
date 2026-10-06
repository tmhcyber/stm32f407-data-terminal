#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app_data_service.h"
#include "app_rs485_service.h"
#include "app_snapshot_consumer.h"
#include "app_snapshot_text_formatter.h"
#include "app_system_monitor.h"
#include "app_uplink_service.h"
#include "app_uart_bringup.h"
#include "app_watchdog_gate.h"
#include "bsp_system_diagnostics.h"
#include "bsp_rs485.h"
#include "bsp_uart.h"
#include "sht30.h"

static uint32_t g_checks;
static uint32_t g_failures;

static BSP_UART_Status_t g_fake_uart_init_status;
static BSP_UART_Status_t g_fake_uart_write_status;
static uint8_t g_fake_uart_last_data[32];
static uint16_t g_fake_uart_last_length;
static uint32_t g_fake_uart_last_timeout_ms;
static uint32_t g_fake_uart_write_count;
static BSP_UART_Status_t g_fake_uart_async_status;
static BSP_UART_TxEvent_t g_fake_uart_tx_event;
static uint8_t g_fake_uart_async_last_data[APP_UPLINK_TEXT_BUFFER_SIZE];
static const uint8_t *g_fake_uart_async_data_pointer;
static uint16_t g_fake_uart_async_last_length;
static uint32_t g_fake_uart_async_write_count;
static BSP_RS485_Status_t g_fake_rs485_init_status;
static BSP_RS485_Status_t g_fake_rs485_write_status;
static BSP_RS485_Status_t g_fake_rs485_abort_status;
static BSP_RS485_RxByteEvent_t g_fake_rs485_rx_queue[256];
static uint16_t g_fake_rs485_rx_head;
static uint16_t g_fake_rs485_rx_tail;
static BSP_RS485_TxEvent_t g_fake_rs485_tx_event;
static BSP_RS485_RxFaultEvent_t g_fake_rs485_fault_event;
static uint8_t g_fake_rs485_last_tx_data[32];
static uint16_t g_fake_rs485_last_tx_length;
static uint32_t g_fake_rs485_write_count;
static uint32_t g_fake_rs485_abort_count;
static uint32_t g_fake_rs485_flush_count;

#define CHECK(condition)                                                        \
  do                                                                            \
  {                                                                             \
    ++g_checks;                                                                 \
    if (!(condition))                                                           \
    {                                                                           \
      ++g_failures;                                                             \
      (void)printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);         \
    }                                                                           \
  } while (0)

BSP_UART_Status_t BSP_UART1_Init(void)
{
  return g_fake_uart_init_status;
}

BSP_UART_Status_t BSP_UART1_Write(const uint8_t *data,
                                  uint16_t length,
                                  uint32_t timeout_ms)
{
  g_fake_uart_last_length = length;
  g_fake_uart_last_timeout_ms = timeout_ms;
  ++g_fake_uart_write_count;
  if ((data != NULL) && (length <= sizeof(g_fake_uart_last_data)))
  {
    (void)memcpy(g_fake_uart_last_data, data, length);
  }

  return g_fake_uart_write_status;
}

BSP_UART_Status_t BSP_UART1_WriteAsync(const uint8_t *data,
                                       uint16_t length)
{
  ++g_fake_uart_async_write_count;
  g_fake_uart_async_data_pointer = data;
  g_fake_uart_async_last_length = length;
  if ((data != NULL) &&
      (length <= sizeof(g_fake_uart_async_last_data)))
  {
    (void)memcpy(g_fake_uart_async_last_data, data, length);
    if (length < sizeof(g_fake_uart_async_last_data))
    {
      g_fake_uart_async_last_data[length] = '\0';
    }
  }
  return g_fake_uart_async_status;
}

BSP_UART_TxEvent_t BSP_UART1_TakeTxEvent(void)
{
  BSP_UART_TxEvent_t event;

  event = g_fake_uart_tx_event;
  g_fake_uart_tx_event.type = BSP_UART_TX_EVENT_NONE;
  g_fake_uart_tx_event.timestamp_ms = 0U;
  return event;
}

BSP_RS485_Status_t BSP_RS485_Init(void)
{
  return g_fake_rs485_init_status;
}

BSP_RS485_Status_t BSP_RS485_WriteAsync(const uint8_t *data,
                                        uint16_t length)
{
  ++g_fake_rs485_write_count;
  g_fake_rs485_last_tx_length = length;
  if ((data != NULL) && (length <= sizeof(g_fake_rs485_last_tx_data)))
  {
    (void)memcpy(g_fake_rs485_last_tx_data, data, length);
  }
  return g_fake_rs485_write_status;
}

BSP_RS485_Status_t BSP_RS485_AbortTransmit(void)
{
  ++g_fake_rs485_abort_count;
  return g_fake_rs485_abort_status;
}

uint8_t BSP_RS485_TakeRxByte(BSP_RS485_RxByteEvent_t *event)
{
  if ((event == NULL) ||
      (g_fake_rs485_rx_tail == g_fake_rs485_rx_head))
  {
    return 0U;
  }

  *event = g_fake_rs485_rx_queue[g_fake_rs485_rx_tail];
  ++g_fake_rs485_rx_tail;
  return 1U;
}

BSP_RS485_TxEvent_t BSP_RS485_TakeTxEvent(void)
{
  BSP_RS485_TxEvent_t event;

  event = g_fake_rs485_tx_event;
  g_fake_rs485_tx_event.type = BSP_RS485_TX_EVENT_NONE;
  g_fake_rs485_tx_event.timestamp_ms = 0U;
  g_fake_rs485_tx_event.uart_error_flags = 0U;
  return event;
}

BSP_RS485_RxFaultEvent_t BSP_RS485_TakeRxFaultEvent(void)
{
  BSP_RS485_RxFaultEvent_t event;

  event = g_fake_rs485_fault_event;
  (void)memset(&g_fake_rs485_fault_event,
               0,
               sizeof(g_fake_rs485_fault_event));
  return event;
}

uint8_t BSP_RS485_FlushRx(void)
{
  uint16_t queued_count;

  queued_count = g_fake_rs485_rx_head - g_fake_rs485_rx_tail;
  g_fake_rs485_rx_tail = g_fake_rs485_rx_head;
  ++g_fake_rs485_flush_count;
  return (uint8_t)queued_count;
}

typedef struct
{
  SHT30_BusStatus_t write_status;
  SHT30_BusStatus_t read_status;
  uint8_t response[SHT30_MEASUREMENT_RESPONSE_LENGTH];
  uint8_t last_address;
  uint8_t last_write[2];
  uint16_t last_write_length;
  uint16_t last_read_length;
  uint32_t last_timeout_ms;
} FakeBus_t;

static uint8_t CalculateCrc(const uint8_t *data, uint16_t length)
{
  uint8_t crc = 0xFFU;
  uint16_t byte_index;
  uint8_t bit_index;

  for (byte_index = 0U; byte_index < length; ++byte_index)
  {
    crc ^= data[byte_index];
    for (bit_index = 0U; bit_index < 8U; ++bit_index)
    {
      crc = (uint8_t)(((crc & 0x80U) != 0U)
                        ? ((crc << 1U) ^ 0x31U)
                        : (crc << 1U));
    }
  }

  return crc;
}

static SHT30_BusStatus_t FakeWrite(void *context,
                                    uint8_t address_7bit,
                                    const uint8_t *data,
                                    uint16_t length,
                                    uint32_t timeout_ms)
{
  FakeBus_t *fake = (FakeBus_t *)context;

  fake->last_address = address_7bit;
  fake->last_write_length = length;
  fake->last_timeout_ms = timeout_ms;
  if ((data != NULL) && (length == 2U))
  {
    fake->last_write[0] = data[0];
    fake->last_write[1] = data[1];
  }

  return fake->write_status;
}

static SHT30_BusStatus_t FakeRead(void *context,
                                   uint8_t address_7bit,
                                   uint8_t *data,
                                   uint16_t length,
                                   uint32_t timeout_ms)
{
  FakeBus_t *fake = (FakeBus_t *)context;

  fake->last_address = address_7bit;
  fake->last_read_length = length;
  fake->last_timeout_ms = timeout_ms;
  if ((data != NULL) && (length == SHT30_MEASUREMENT_RESPONSE_LENGTH))
  {
    (void)memcpy(data, fake->response, length);
  }

  return fake->read_status;
}

static SHT30_t MakeDevice(FakeBus_t *fake)
{
  SHT30_t device;
  SHT30_Bus_t bus;

  (void)memset(&device, 0, sizeof(device));
  bus.write = FakeWrite;
  bus.read = FakeRead;
  bus.context = fake;
  bus.timeout_ms = 25U;
  CHECK(SHT30_Init(&device, &bus, SHT30_ADDRESS_DEFAULT_7BIT) ==
        SHT30_STATUS_OK);
  return device;
}

static void TestSht30InitAndCommand(void)
{
  FakeBus_t fake;
  SHT30_t device;
  SHT30_Bus_t invalid_bus;

  (void)memset(&fake, 0, sizeof(fake));
  (void)memset(&invalid_bus, 0, sizeof(invalid_bus));
  CHECK(SHT30_Init(NULL, &invalid_bus, SHT30_ADDRESS_DEFAULT_7BIT) ==
        SHT30_STATUS_INVALID_ARGUMENT);
  device = MakeDevice(&fake);

  CHECK(SHT30_StartHighRepeatabilityMeasurement(&device) == SHT30_STATUS_OK);
  CHECK(fake.last_address == SHT30_ADDRESS_DEFAULT_7BIT);
  CHECK(fake.last_write_length == 2U);
  CHECK(fake.last_write[0] == 0x24U);
  CHECK(fake.last_write[1] == 0x00U);
  CHECK(fake.last_timeout_ms == 25U);

  fake.write_status = SHT30_BUS_STATUS_NACK;
  CHECK(SHT30_StartHighRepeatabilityMeasurement(&device) ==
        SHT30_STATUS_BUS_NACK);
  fake.write_status = SHT30_BUS_STATUS_TIMEOUT;
  CHECK(SHT30_StartHighRepeatabilityMeasurement(&device) ==
        SHT30_STATUS_BUS_TIMEOUT);
  fake.write_status = SHT30_BUS_STATUS_BUSY;
  CHECK(SHT30_StartHighRepeatabilityMeasurement(&device) ==
        SHT30_STATUS_BUS_BUSY);
}

static void FillResponse(uint8_t response[6],
                         uint16_t temperature_raw,
                         uint16_t humidity_raw)
{
  response[0] = (uint8_t)(temperature_raw >> 8U);
  response[1] = (uint8_t)temperature_raw;
  response[2] = CalculateCrc(&response[0], 2U);
  response[3] = (uint8_t)(humidity_raw >> 8U);
  response[4] = (uint8_t)humidity_raw;
  response[5] = CalculateCrc(&response[3], 2U);
}

static void TestSht30DecodeAndRead(void)
{
  uint8_t response[6];
  SHT30_Measurement_t measurement;
  FakeBus_t fake;
  SHT30_t device;

  FillResponse(response, 0U, 0U);
  CHECK(SHT30_DecodeMeasurement(response, &measurement) == SHT30_STATUS_OK);
  CHECK(measurement.temperature_milli_c == -45000);
  CHECK(measurement.humidity_milli_percent == 0U);

  FillResponse(response, 65535U, 65535U);
  CHECK(SHT30_DecodeMeasurement(response, &measurement) == SHT30_STATUS_OK);
  CHECK(measurement.temperature_milli_c == 130000);
  CHECK(measurement.humidity_milli_percent == 100000U);

  FillResponse(response, 32768U, 32768U);
  CHECK(SHT30_DecodeMeasurement(response, &measurement) == SHT30_STATUS_OK);
  CHECK(measurement.temperature_milli_c == 42501);
  CHECK(measurement.humidity_milli_percent == 50001U);

  response[2] ^= 1U;
  CHECK(SHT30_DecodeMeasurement(response, &measurement) ==
        SHT30_STATUS_TEMPERATURE_CRC_ERROR);
  FillResponse(response, 0xBEEFU, 0x8000U);
  response[5] ^= 1U;
  CHECK(SHT30_DecodeMeasurement(response, &measurement) ==
        SHT30_STATUS_HUMIDITY_CRC_ERROR);

  (void)memset(&fake, 0, sizeof(fake));
  FillResponse(fake.response, 0xBEEFU, 0x8000U);
  device = MakeDevice(&fake);
  CHECK(SHT30_ReadMeasurement(&device, &measurement) == SHT30_STATUS_OK);
  CHECK(measurement.temperature_raw == 0xBEEFU);
  CHECK(measurement.humidity_raw == 0x8000U);
  CHECK(fake.last_read_length == SHT30_MEASUREMENT_RESPONSE_LENGTH);
  fake.read_status = SHT30_BUS_STATUS_ERROR;
  CHECK(SHT30_ReadMeasurement(&device, &measurement) == SHT30_STATUS_BUS_ERROR);
}

static void FillUpdates(App_DataValueUpdate_t updates[2],
                        int32_t temperature,
                        int32_t humidity)
{
  updates[0].source_id = APP_DATA_SOURCE_SHT30;
  updates[0].point_id = APP_DATA_POINT_TEMPERATURE;
  updates[0].raw_value = temperature;
  updates[0].scale = APP_DATA_SCALE_MILLI;
  updates[0].unit = APP_DATA_UNIT_DEGREE_CELSIUS;
  updates[1].source_id = APP_DATA_SOURCE_SHT30;
  updates[1].point_id = APP_DATA_POINT_HUMIDITY;
  updates[1].raw_value = humidity;
  updates[1].scale = APP_DATA_SCALE_MILLI;
  updates[1].unit = APP_DATA_UNIT_PERCENT_RH;
}

static void TestDataServiceContract(void)
{
  App_DataSnapshot_t snapshot;
  App_DataValueUpdate_t updates[2];

  CHECK(App_DataService_GetSnapshot(&snapshot) ==
        APP_DATA_RESULT_NOT_INITIALIZED);
  App_DataService_Init();
  CHECK(App_DataService_GetSnapshot(&snapshot) == APP_DATA_RESULT_OK);
  CHECK(snapshot.snapshot_version == 2U);
  CHECK(snapshot.point_count == APP_DATA_POINT_COUNT);
  CHECK(snapshot.points[0].quality == APP_DATA_QUALITY_NOT_AVAILABLE);

  FillUpdates(updates, 23567, 48321);
  CHECK(App_DataService_PublishValues(updates, 2U, 1000U) ==
        APP_DATA_RESULT_OK);
  CHECK(App_DataService_GetSnapshot(&snapshot) == APP_DATA_RESULT_OK);
  CHECK(snapshot.snapshot_version == 4U);
  CHECK(snapshot.points[0].raw_value == 23567);
  CHECK(snapshot.points[1].raw_value == 48321);
  CHECK(snapshot.points[0].sequence == 1U);
  CHECK(snapshot.points[1].sequence == 1U);
  CHECK(snapshot.points[0].timestamp_ms == 1000U);
  CHECK(snapshot.points[0].quality == APP_DATA_QUALITY_VALID);

  CHECK(App_DataService_UpdateSourceQuality(APP_DATA_SOURCE_SHT30,
                                             APP_DATA_QUALITY_STALE,
                                             APP_DATA_STATUS_NACK,
                                             1100U) == APP_DATA_RESULT_OK);
  CHECK(App_DataService_GetSnapshot(&snapshot) == APP_DATA_RESULT_OK);
  CHECK(snapshot.snapshot_version == 6U);
  CHECK(snapshot.points[0].raw_value == 23567);
  CHECK(snapshot.points[0].timestamp_ms == 1000U);
  CHECK(snapshot.points[0].sequence == 1U);
  CHECK(snapshot.points[0].status_timestamp_ms == 1100U);
  CHECK(snapshot.points[0].quality == APP_DATA_QUALITY_STALE);
}

static void TestDataServiceRejectsInvalidBatches(void)
{
  App_DataValueUpdate_t updates[2];

  App_DataService_Init();
  FillUpdates(updates, 1, 2);
  updates[1] = updates[0];
  CHECK(App_DataService_PublishValues(updates, 2U, 1U) ==
        APP_DATA_RESULT_INVALID_ARGUMENT);
  FillUpdates(updates, 1, 2);
  updates[0].unit = APP_DATA_UNIT_NONE;
  CHECK(App_DataService_PublishValues(updates, 2U, 1U) ==
        APP_DATA_RESULT_INVALID_ARGUMENT);
  CHECK(App_DataService_UpdateSourceQuality(APP_DATA_SOURCE_SHT30,
                                             APP_DATA_QUALITY_VALID,
                                             APP_DATA_STATUS_NACK,
                                             1U) ==
        APP_DATA_RESULT_INVALID_ARGUMENT);
}

static void TestSnapshotConsumerTransitions(void)
{
  App_SnapshotConsumer_t consumer;
  App_SnapshotEvent_t event;
  App_DataValueUpdate_t updates[2];
  uint8_t ready;

  App_DataService_Init();
  App_SnapshotConsumer_Init(&consumer);
  CHECK(App_SnapshotConsumer_Process(&consumer, &event, &ready) ==
        APP_DATA_RESULT_OK);
  CHECK(ready == 1U);
  CHECK(event.flags == APP_SNAPSHOT_EVENT_INITIAL_SNAPSHOT);
  CHECK(App_SnapshotConsumer_Process(&consumer, &event, &ready) ==
        APP_DATA_RESULT_OK);
  CHECK(ready == 0U);

  FillUpdates(updates, 30000, 70000);
  CHECK(App_DataService_PublishValues(updates, 2U, 1000U) ==
        APP_DATA_RESULT_OK);
  CHECK(App_SnapshotConsumer_Process(&consumer, &event, &ready) ==
        APP_DATA_RESULT_OK);
  CHECK(ready == 1U);
  CHECK((event.flags & APP_SNAPSHOT_EVENT_NEW_VALID_DATA) != 0U);
  CHECK((event.flags & APP_SNAPSHOT_EVENT_QUALITY_CHANGED) != 0U);
  CHECK(event.skipped_count == 0U);

  CHECK(App_DataService_UpdateSourceQuality(APP_DATA_SOURCE_SHT30,
                                             APP_DATA_QUALITY_STALE,
                                             APP_DATA_STATUS_NACK,
                                             2000U) == APP_DATA_RESULT_OK);
  CHECK(App_DataService_UpdateSourceQuality(APP_DATA_SOURCE_SHT30,
                                             APP_DATA_QUALITY_OFFLINE,
                                             APP_DATA_STATUS_NACK,
                                             3000U) == APP_DATA_RESULT_OK);
  CHECK(App_SnapshotConsumer_Process(&consumer, &event, &ready) ==
        APP_DATA_RESULT_OK);
  CHECK((event.flags & APP_SNAPSHOT_EVENT_SOURCE_OFFLINE) != 0U);
  CHECK((event.flags & APP_SNAPSHOT_EVENT_VERSION_GAP) != 0U);
  CHECK((event.flags & APP_SNAPSHOT_EVENT_NEW_VALID_DATA) == 0U);
  CHECK(event.skipped_count == 1U);
  CHECK(event.snapshot.points[0].raw_value == 30000);
  CHECK(event.snapshot.points[0].sequence == 1U);
  CHECK(event.snapshot.points[0].timestamp_ms == 1000U);

  FillUpdates(updates, 30100, 70100);
  CHECK(App_DataService_PublishValues(updates, 2U, 4000U) ==
        APP_DATA_RESULT_OK);
  CHECK(App_SnapshotConsumer_Process(&consumer, &event, &ready) ==
        APP_DATA_RESULT_OK);
  CHECK((event.flags & APP_SNAPSHOT_EVENT_SOURCE_RECOVERED) != 0U);
  CHECK((event.flags & APP_SNAPSHOT_EVENT_NEW_VALID_DATA) != 0U);
  CHECK(event.skipped_count == 0U);
  CHECK(consumer.total_skipped_count == 1U);
}

static void ResetFakeUart(void)
{
  g_fake_uart_init_status = BSP_UART_STATUS_OK;
  g_fake_uart_write_status = BSP_UART_STATUS_OK;
  (void)memset(g_fake_uart_last_data, 0, sizeof(g_fake_uart_last_data));
  g_fake_uart_last_length = 0U;
  g_fake_uart_last_timeout_ms = 0U;
  g_fake_uart_write_count = 0U;
  g_fake_uart_async_status = BSP_UART_STATUS_OK;
  g_fake_uart_tx_event.type = BSP_UART_TX_EVENT_NONE;
  g_fake_uart_tx_event.timestamp_ms = 0U;
  (void)memset(g_fake_uart_async_last_data,
               0,
               sizeof(g_fake_uart_async_last_data));
  g_fake_uart_async_last_length = 0U;
  g_fake_uart_async_data_pointer = NULL;
  g_fake_uart_async_write_count = 0U;
}

static void ResetFakeRs485(void)
{
  g_fake_rs485_init_status = BSP_RS485_STATUS_OK;
  g_fake_rs485_write_status = BSP_RS485_STATUS_OK;
  g_fake_rs485_abort_status = BSP_RS485_STATUS_OK;
  (void)memset(g_fake_rs485_rx_queue,
               0,
               sizeof(g_fake_rs485_rx_queue));
  g_fake_rs485_rx_head = 0U;
  g_fake_rs485_rx_tail = 0U;
  g_fake_rs485_tx_event.type = BSP_RS485_TX_EVENT_NONE;
  g_fake_rs485_tx_event.timestamp_ms = 0U;
  g_fake_rs485_tx_event.uart_error_flags = 0U;
  (void)memset(&g_fake_rs485_fault_event,
               0,
               sizeof(g_fake_rs485_fault_event));
  (void)memset(g_fake_rs485_last_tx_data,
               0,
               sizeof(g_fake_rs485_last_tx_data));
  g_fake_rs485_last_tx_length = 0U;
  g_fake_rs485_write_count = 0U;
  g_fake_rs485_abort_count = 0U;
  g_fake_rs485_flush_count = 0U;
}

static void QueueFakeRs485Bytes(const uint8_t *data,
                                uint16_t length,
                                uint32_t first_timestamp_ms,
                                uint32_t byte_gap_ms)
{
  uint16_t index;

  if ((data == NULL) ||
      ((uint32_t)g_fake_rs485_rx_head + length >
       (uint32_t)(sizeof(g_fake_rs485_rx_queue) /
                  sizeof(g_fake_rs485_rx_queue[0]))))
  {
    CHECK(0);
    return;
  }

  for (index = 0U; index < length; ++index)
  {
    g_fake_rs485_rx_queue[g_fake_rs485_rx_head].byte = data[index];
    g_fake_rs485_rx_queue[g_fake_rs485_rx_head].timestamp_ms =
      first_timestamp_ms + ((uint32_t)index * byte_gap_ms);
    ++g_fake_rs485_rx_head;
  }
}

static void InitFakeRs485(void)
{
  ResetFakeRs485();
  CHECK(App_RS485Service_Init() == APP_RS485_SERVICE_RESULT_OK);
  CHECK(g_app_rs485_report.state == APP_RS485_STATE_RX_WAIT);
}

static void TestUartBringupSchedule(void)
{
  static const uint8_t expected_message[] = "UART1_TX_OK\r\n";

  ResetFakeUart();
  App_UART_Bringup_Init();
  CHECK(g_app_uart_bringup_report.init_status ==
        APP_UART_BRINGUP_STATUS_OK);
  CHECK(g_app_uart_bringup_report.last_send_status ==
        APP_UART_BRINGUP_STATUS_NOT_RUN);

  App_UART_Bringup_Process(100U);
  CHECK(g_fake_uart_write_count == 1U);
  CHECK(g_fake_uart_last_length == (sizeof(expected_message) - 1U));
  CHECK(memcmp(g_fake_uart_last_data,
               expected_message,
               sizeof(expected_message) - 1U) == 0);
  CHECK(g_fake_uart_last_timeout_ms == 20U);
  CHECK(g_app_uart_bringup_report.last_attempt_at_ms == 100U);
  CHECK(g_app_uart_bringup_report.successful_send_count == 1U);
  CHECK(g_app_uart_bringup_report.failed_send_count == 0U);

  App_UART_Bringup_Process(1099U);
  CHECK(g_fake_uart_write_count == 1U);
  App_UART_Bringup_Process(1100U);
  CHECK(g_fake_uart_write_count == 2U);
  CHECK(g_app_uart_bringup_report.successful_send_count == 2U);

  g_fake_uart_write_status = BSP_UART_STATUS_BUSY;
  App_UART_Bringup_Process(2100U);
  CHECK(g_fake_uart_write_count == 3U);
  CHECK(g_app_uart_bringup_report.last_send_status ==
        APP_UART_BRINGUP_STATUS_BUSY);
  CHECK(g_app_uart_bringup_report.successful_send_count == 2U);
  CHECK(g_app_uart_bringup_report.failed_send_count == 1U);
}

static void TestUartBringupInitFailureAndTickWrap(void)
{
  ResetFakeUart();
  g_fake_uart_init_status = BSP_UART_STATUS_ERROR;
  App_UART_Bringup_Init();
  App_UART_Bringup_Process(1U);
  CHECK(g_app_uart_bringup_report.init_status ==
        APP_UART_BRINGUP_STATUS_ERROR);
  CHECK(g_fake_uart_write_count == 0U);

  ResetFakeUart();
  App_UART_Bringup_Init();
  App_UART_Bringup_Process(0xFFFFFF00U);
  CHECK(g_fake_uart_write_count == 1U);
  App_UART_Bringup_Process(0x000002E7U);
  CHECK(g_fake_uart_write_count == 1U);
  App_UART_Bringup_Process(0x000002E8U);
  CHECK(g_fake_uart_write_count == 2U);
}

static App_UplinkMessage_t MakeUplinkMessage(void)
{
  App_UplinkMessage_t message;
  App_DataPoint_t *temperature;
  App_DataPoint_t *humidity;

  (void)memset(&message, 0, sizeof(message));
  message.reason = APP_UPLINK_REASON_EVENT;
  message.flags = APP_SNAPSHOT_EVENT_SNAPSHOT_CHANGED |
                  APP_SNAPSHOT_EVENT_NEW_VALID_DATA |
                  APP_SNAPSHOT_EVENT_STATUS_CHANGED;
  message.coalesced_count = 3U;
  message.snapshot.snapshot_version = 94U;
  message.snapshot.point_count = APP_DATA_POINT_COUNT;

  temperature = &message.snapshot.points[0];
  temperature->source_id = APP_DATA_SOURCE_SHT30;
  temperature->point_id = APP_DATA_POINT_TEMPERATURE;
  temperature->raw_value = 31350;
  temperature->scale = APP_DATA_SCALE_MILLI;
  temperature->unit = APP_DATA_UNIT_DEGREE_CELSIUS;
  temperature->quality = APP_DATA_QUALITY_VALID;
  temperature->last_status = APP_DATA_STATUS_OK;
  temperature->timestamp_ms = 45015U;
  temperature->status_timestamp_ms = 45015U;
  temperature->sequence = 46U;

  humidity = &message.snapshot.points[1];
  *humidity = *temperature;
  humidity->point_id = APP_DATA_POINT_HUMIDITY;
  humidity->raw_value = 73800;
  humidity->unit = APP_DATA_UNIT_PERCENT_RH;

  return message;
}

static void TestSnapshotTextEvent(void)
{
  static const char expected[] =
    "v=1;reason=EVENT;flags=0x00000016;gap=0;coal=3;snap=94;n=2;"
    "p0.src=SHT30;p0.id=TEMPERATURE;p0.value=31350;p0.scale=-3;"
    "p0.unit=DEG_C;p0.quality=VALID;p0.status=OK;p0.value_ms=45015;"
    "p0.status_ms=45015;p0.seq=46;"
    "p1.src=SHT30;p1.id=HUMIDITY;p1.value=73800;p1.scale=-3;"
    "p1.unit=PERCENT_RH;p1.quality=VALID;p1.status=OK;"
    "p1.value_ms=45015;p1.status_ms=45015;p1.seq=46;\r\n";
  App_UplinkMessage_t message;
  char buffer[APP_UPLINK_TEXT_BUFFER_SIZE];
  uint32_t length;

  message = MakeUplinkMessage();
  CHECK(App_SnapshotTextFormatter_Format(&message,
                                          buffer,
                                          sizeof(buffer),
                                          &length) ==
        APP_UPLINK_TEXT_RESULT_OK);
  CHECK(length == (sizeof(expected) - 1U));
  CHECK(strlen(buffer) == length);
  CHECK(strcmp(buffer, expected) == 0);
  CHECK(buffer[length - 2U] == '\r');
  CHECK(buffer[length - 1U] == '\n');
}

static void TestSnapshotTextHeartbeatAndOffline(void)
{
  App_UplinkMessage_t message;
  char buffer[APP_UPLINK_TEXT_BUFFER_SIZE];
  uint32_t length;

  message = MakeUplinkMessage();
  message.reason = APP_UPLINK_REASON_HEARTBEAT;
  message.flags = APP_SNAPSHOT_EVENT_NONE;
  message.skipped_count = 0U;
  CHECK(App_SnapshotTextFormatter_Format(&message,
                                          buffer,
                                          sizeof(buffer),
                                          &length) ==
        APP_UPLINK_TEXT_RESULT_OK);
  CHECK(strstr(buffer, "reason=HEARTBEAT") != NULL);
  CHECK(strstr(buffer, "flags=0x00000000;gap=0") != NULL);
  CHECK(strstr(buffer, "p0.quality=VALID;p0.status=OK") != NULL);

  message.reason = APP_UPLINK_REASON_EVENT;
  message.flags = APP_SNAPSHOT_EVENT_SNAPSHOT_CHANGED |
                  APP_SNAPSHOT_EVENT_STATUS_CHANGED |
                  APP_SNAPSHOT_EVENT_SOURCE_OFFLINE;
  message.snapshot.snapshot_version = 96U;
  message.snapshot.points[0].quality = APP_DATA_QUALITY_OFFLINE;
  message.snapshot.points[0].last_status =
    APP_DATA_STATUS_HUMIDITY_CRC_ERROR;
  message.snapshot.points[0].status_timestamp_ms = 76200U;
  message.snapshot.points[1].quality = APP_DATA_QUALITY_OFFLINE;
  message.snapshot.points[1].last_status =
    APP_DATA_STATUS_HUMIDITY_CRC_ERROR;
  message.snapshot.points[1].status_timestamp_ms = 76200U;
  CHECK(App_SnapshotTextFormatter_Format(&message,
                                          buffer,
                                          sizeof(buffer),
                                          &length) ==
        APP_UPLINK_TEXT_RESULT_OK);
  CHECK(strstr(buffer, "p0.value=31350") != NULL);
  CHECK(strstr(buffer,
               "p0.quality=OFFLINE;"
               "p0.status=HUMIDITY_CRC_ERROR") != NULL);
  CHECK(strstr(buffer, "p0.value_ms=45015;p0.status_ms=76200") != NULL);
}

static void TestSnapshotTextRejectsInvalidMessages(void)
{
  App_UplinkMessage_t message;
  char buffer[APP_UPLINK_TEXT_BUFFER_SIZE];
  uint32_t length;

  message = MakeUplinkMessage();
  CHECK(App_SnapshotTextFormatter_Format(NULL,
                                          buffer,
                                          sizeof(buffer),
                                          &length) ==
        APP_UPLINK_TEXT_RESULT_INVALID_ARGUMENT);
  CHECK(App_SnapshotTextFormatter_Format(&message,
                                          buffer,
                                          16U,
                                          &length) ==
        APP_UPLINK_TEXT_RESULT_BUFFER_TOO_SMALL);
  CHECK(length == 0U);
  CHECK(buffer[0] == '\0');

  message.reason = APP_UPLINK_REASON_HEARTBEAT;
  CHECK(App_SnapshotTextFormatter_Format(&message,
                                          buffer,
                                          sizeof(buffer),
                                          &length) ==
        APP_UPLINK_TEXT_RESULT_INVALID_MESSAGE);
  message.flags = APP_SNAPSHOT_EVENT_NONE;
  message.skipped_count = 1U;
  CHECK(App_SnapshotTextFormatter_Format(&message,
                                          buffer,
                                          sizeof(buffer),
                                          &length) ==
        APP_UPLINK_TEXT_RESULT_INVALID_MESSAGE);

  message = MakeUplinkMessage();
  message.snapshot.snapshot_version = 95U;
  CHECK(App_SnapshotTextFormatter_Format(&message,
                                          buffer,
                                          sizeof(buffer),
                                          &length) ==
        APP_UPLINK_TEXT_RESULT_INVALID_MESSAGE);
  message.snapshot.snapshot_version = 94U;
  message.snapshot.point_count = 1U;
  CHECK(App_SnapshotTextFormatter_Format(&message,
                                          buffer,
                                          sizeof(buffer),
                                          &length) ==
        APP_UPLINK_TEXT_RESULT_INVALID_MESSAGE);

  message = MakeUplinkMessage();
  message.snapshot.points[0].quality = (App_DataQuality_t)99;
  CHECK(App_SnapshotTextFormatter_Format(&message,
                                          buffer,
                                          sizeof(buffer),
                                          &length) ==
        APP_UPLINK_TEXT_RESULT_UNSUPPORTED_VALUE);
  message = MakeUplinkMessage();
  message.flags |= (1UL << 31U);
  CHECK(App_SnapshotTextFormatter_Format(&message,
                                          buffer,
                                          sizeof(buffer),
                                          &length) ==
        APP_UPLINK_TEXT_RESULT_UNSUPPORTED_VALUE);
}

static void TestSnapshotTextWorstCaseFits(void)
{
  App_UplinkMessage_t message;
  char buffer[APP_UPLINK_TEXT_BUFFER_SIZE];
  uint32_t index;
  uint32_t length;

  message = MakeUplinkMessage();
  message.flags = 0xFFU;
  message.skipped_count = UINT32_MAX;
  message.coalesced_count = UINT32_MAX;
  message.snapshot.snapshot_version = UINT32_MAX - 1U;
  for (index = 0U; index < APP_DATA_POINT_COUNT; ++index)
  {
    message.snapshot.points[index].raw_value = INT32_MIN;
    message.snapshot.points[index].scale = INT8_MIN;
    message.snapshot.points[index].quality =
      APP_DATA_QUALITY_NOT_AVAILABLE;
    message.snapshot.points[index].last_status =
      APP_DATA_STATUS_HUMIDITY_CRC_ERROR;
    message.snapshot.points[index].timestamp_ms = UINT32_MAX;
    message.snapshot.points[index].status_timestamp_ms = UINT32_MAX;
    message.snapshot.points[index].sequence = UINT32_MAX;
  }

  CHECK(App_SnapshotTextFormatter_Format(&message,
                                          buffer,
                                          sizeof(buffer),
                                          &length) ==
        APP_UPLINK_TEXT_RESULT_OK);
  CHECK(length < APP_UPLINK_TEXT_BUFFER_SIZE);
  CHECK(buffer[length] == '\0');
}

static App_SnapshotEvent_t MakeUplinkEvent(uint32_t snapshot_version,
                                           uint32_t sequence)
{
  App_UplinkMessage_t message;
  App_SnapshotEvent_t event;

  message = MakeUplinkMessage();
  message.snapshot.snapshot_version = snapshot_version;
  message.snapshot.points[0].sequence = sequence;
  message.snapshot.points[1].sequence = sequence;
  event.flags = message.flags;
  event.skipped_count = message.skipped_count;
  event.snapshot = message.snapshot;
  return event;
}

static void TestUplinkServiceCompletionAndHeartbeat(void)
{
  App_SnapshotEvent_t event;

  ResetFakeUart();
  CHECK(App_UplinkService_Init() == APP_UPLINK_SERVICE_RESULT_OK);
  event = MakeUplinkEvent(94U, 46U);
  CHECK(App_UplinkService_SubmitEvent(&event) ==
        APP_UPLINK_SERVICE_RESULT_OK);
  CHECK(g_app_uplink_service_report.pending_valid == 1U);

  App_UplinkService_Process(1000U);
  CHECK(g_fake_uart_async_write_count == 1U);
  CHECK(g_app_uplink_service_report.state ==
        APP_UPLINK_SERVICE_STATE_SENDING);
  CHECK(g_app_uplink_service_report.tx_start_count == 1U);
  CHECK(strstr((const char *)g_fake_uart_async_last_data,
               "reason=EVENT") != NULL);

  App_UplinkService_Process(1044U);
  CHECK(g_app_uplink_service_report.completed_count == 0U);
  g_fake_uart_tx_event.type = BSP_UART_TX_EVENT_COMPLETE;
  g_fake_uart_tx_event.timestamp_ms = 1045U;
  App_UplinkService_Process(1044U);
  CHECK(g_app_uplink_service_report.state ==
        APP_UPLINK_SERVICE_STATE_IDLE);
  CHECK(g_app_uplink_service_report.last_completed_at_ms == 1045U);
  CHECK(g_app_uplink_service_report.completed_count == 1U);
  CHECK(g_app_uplink_service_report.event_completed_count == 1U);
  CHECK(g_fake_uart_async_write_count == 1U);
  CHECK(g_app_uplink_service_report.pending_valid == 0U);

  App_UplinkService_Process(6044U);
  CHECK(g_fake_uart_async_write_count == 1U);
  App_UplinkService_Process(6045U);
  CHECK(g_fake_uart_async_write_count == 2U);
  CHECK(g_app_uplink_service_report.active_reason ==
        APP_UPLINK_REASON_HEARTBEAT);
  CHECK(strstr((const char *)g_fake_uart_async_last_data,
               "reason=HEARTBEAT") != NULL);
  CHECK(strstr((const char *)g_fake_uart_async_last_data,
               "flags=0x00000000;gap=0") != NULL);
  CHECK(strstr((const char *)g_fake_uart_async_last_data,
               "snap=94") != NULL);
}

static void TestUplinkServiceCoalescesLatestPending(void)
{
  App_SnapshotEvent_t event_a;
  App_SnapshotEvent_t event_b;
  App_SnapshotEvent_t event_c;

  ResetFakeUart();
  CHECK(App_UplinkService_Init() == APP_UPLINK_SERVICE_RESULT_OK);
  event_a = MakeUplinkEvent(94U, 46U);
  event_b = MakeUplinkEvent(96U, 47U);
  event_c = MakeUplinkEvent(98U, 48U);

  CHECK(App_UplinkService_SubmitEvent(&event_a) ==
        APP_UPLINK_SERVICE_RESULT_OK);
  App_UplinkService_Process(100U);
  CHECK(strstr((const char *)g_fake_uart_async_last_data,
               "snap=94") != NULL);
  CHECK(App_UplinkService_SubmitEvent(&event_b) ==
        APP_UPLINK_SERVICE_RESULT_OK);
  CHECK(g_app_uplink_service_report.coalesced_count == 0U);
  CHECK(App_UplinkService_SubmitEvent(&event_c) ==
        APP_UPLINK_SERVICE_RESULT_OK);
  CHECK(g_app_uplink_service_report.coalesced_count == 1U);
  CHECK(g_app_uplink_service_report.pending_valid == 1U);
  CHECK(g_fake_uart_async_data_pointer != NULL);
  CHECK(strstr((const char *)g_fake_uart_async_data_pointer,
               "snap=94") != NULL);
  CHECK(strstr((const char *)g_fake_uart_async_last_data,
               "snap=94") != NULL);

  g_fake_uart_tx_event.type = BSP_UART_TX_EVENT_COMPLETE;
  g_fake_uart_tx_event.timestamp_ms = 145U;
  App_UplinkService_Process(145U);
  CHECK(g_fake_uart_async_write_count == 2U);
  CHECK(strstr((const char *)g_fake_uart_async_last_data,
               "snap=98") != NULL);
  CHECK(strstr((const char *)g_fake_uart_async_last_data,
               "coal=1") != NULL);
  CHECK(g_app_uplink_service_report.format_error_count == 0U);
}

static void TestUplinkServiceBusyAndFormatFailure(void)
{
  App_SnapshotEvent_t event;

  ResetFakeUart();
  CHECK(App_UplinkService_Init() == APP_UPLINK_SERVICE_RESULT_OK);
  event = MakeUplinkEvent(94U, 46U);
  CHECK(App_UplinkService_SubmitEvent(&event) ==
        APP_UPLINK_SERVICE_RESULT_OK);
  g_fake_uart_async_status = BSP_UART_STATUS_BUSY;
  App_UplinkService_Process(100U);
  CHECK(g_app_uplink_service_report.tx_busy_count == 1U);
  CHECK(g_app_uplink_service_report.pending_valid == 1U);
  CHECK(g_app_uplink_service_report.format_error_count == 0U);
  App_UplinkService_Process(199U);
  CHECK(g_fake_uart_async_write_count == 1U);
  g_fake_uart_async_status = BSP_UART_STATUS_OK;
  App_UplinkService_Process(200U);
  CHECK(g_fake_uart_async_write_count == 2U);
  CHECK(g_app_uplink_service_report.state ==
        APP_UPLINK_SERVICE_STATE_SENDING);

  ResetFakeUart();
  CHECK(App_UplinkService_Init() == APP_UPLINK_SERVICE_RESULT_OK);
  event = MakeUplinkEvent(95U, 46U);
  CHECK(App_UplinkService_SubmitEvent(&event) ==
        APP_UPLINK_SERVICE_RESULT_OK);
  App_UplinkService_Process(300U);
  CHECK(g_app_uplink_service_report.format_error_count == 1U);
  CHECK(g_app_uplink_service_report.has_latest_snapshot == 0U);
  CHECK(g_app_uplink_service_report.pending_valid == 0U);
  CHECK(g_fake_uart_async_write_count == 0U);
}

static void TestUplinkServiceStartErrorRetriesAndRecovers(void)
{
  App_SnapshotEvent_t event;

  ResetFakeUart();
  CHECK(App_UplinkService_Init() == APP_UPLINK_SERVICE_RESULT_OK);
  event = MakeUplinkEvent(94U, 46U);
  CHECK(App_UplinkService_SubmitEvent(&event) ==
        APP_UPLINK_SERVICE_RESULT_OK);

  g_fake_uart_async_status = BSP_UART_STATUS_ERROR;
  App_UplinkService_Process(100U);
  CHECK(g_fake_uart_async_write_count == 1U);
  CHECK(g_app_uplink_service_report.state ==
        APP_UPLINK_SERVICE_STATE_IDLE);
  CHECK(g_app_uplink_service_report.pending_valid == 1U);
  CHECK(g_app_uplink_service_report.tx_error_count == 1U);

  App_UplinkService_Process(199U);
  CHECK(g_fake_uart_async_write_count == 1U);
  g_fake_uart_async_status = BSP_UART_STATUS_OK;
  App_UplinkService_Process(200U);
  CHECK(g_fake_uart_async_write_count == 2U);
  CHECK(g_app_uplink_service_report.state ==
        APP_UPLINK_SERVICE_STATE_SENDING);
  CHECK(g_app_uplink_service_report.tx_error_count == 1U);

  g_fake_uart_tx_event.type = BSP_UART_TX_EVENT_COMPLETE;
  g_fake_uart_tx_event.timestamp_ms = 245U;
  App_UplinkService_Process(245U);
  CHECK(g_app_uplink_service_report.state ==
        APP_UPLINK_SERVICE_STATE_IDLE);
  CHECK(g_app_uplink_service_report.completed_count == 1U);
  CHECK(g_app_uplink_service_report.last_completed_at_ms == 245U);
  CHECK(g_app_uplink_service_report.tx_error_count == 1U);
}

static void TestUplinkServiceTransportErrorAndInitFailure(void)
{
  App_SnapshotEvent_t event;

  ResetFakeUart();
  CHECK(App_UplinkService_Init() == APP_UPLINK_SERVICE_RESULT_OK);
  event = MakeUplinkEvent(94U, 46U);
  CHECK(App_UplinkService_SubmitEvent(&event) ==
        APP_UPLINK_SERVICE_RESULT_OK);
  App_UplinkService_Process(100U);
  g_fake_uart_tx_event.type = BSP_UART_TX_EVENT_ERROR;
  g_fake_uart_tx_event.timestamp_ms = 145U;
  App_UplinkService_Process(145U);
  CHECK(g_app_uplink_service_report.state ==
        APP_UPLINK_SERVICE_STATE_IDLE);
  CHECK(g_app_uplink_service_report.tx_error_count == 1U);
  CHECK(g_app_uplink_service_report.completed_count == 0U);
  CHECK(g_app_uplink_service_report.pending_reason ==
        APP_UPLINK_REASON_HEARTBEAT);
  App_UplinkService_Process(244U);
  CHECK(g_fake_uart_async_write_count == 1U);
  App_UplinkService_Process(245U);
  CHECK(g_fake_uart_async_write_count == 2U);
  CHECK(g_app_uplink_service_report.active_reason ==
        APP_UPLINK_REASON_HEARTBEAT);

  ResetFakeUart();
  g_fake_uart_init_status = BSP_UART_STATUS_ERROR;
  CHECK(App_UplinkService_Init() ==
        APP_UPLINK_SERVICE_RESULT_TRANSPORT_ERROR);
  CHECK(g_app_uplink_service_report.state ==
        APP_UPLINK_SERVICE_STATE_NOT_INITIALIZED);
  CHECK(g_app_uplink_service_report.tx_error_count == 1U);
  CHECK(App_UplinkService_SubmitEvent(&event) ==
        APP_UPLINK_SERVICE_RESULT_NOT_INITIALIZED);
  App_UplinkService_Process(500U);
  CHECK(g_fake_uart_async_write_count == 0U);
}

static BSP_SystemFaultRecord_t MakeValidFaultRecord(void)
{
  BSP_SystemFaultRecord_t record;

  (void)memset(&record, 0, sizeof(record));
  record.version = BSP_SYSTEM_DIAGNOSTICS_FAULT_RECORD_VERSION;
  record.size = (uint32_t)sizeof(record);
  record.fault_type = (uint32_t)BSP_SYSTEM_FAULT_TYPE_BUSFAULT;
  record.exc_return = 0xFFFFFFF9UL;
  record.original_sp = 0x20001000UL;
  record.core_frame_sp = record.original_sp;
  record.frame_valid = 1U;
  record.pc = 0x08001234UL;
  record.xpsr = 0x21000000UL;
  record.cfsr = 0x00008200UL;
  record.bfar = 0x20030000UL;
  record.checksum = BSP_SystemDiagnostics_CalculateFaultChecksum(&record);
  record.commit = BSP_SYSTEM_DIAGNOSTICS_FAULT_RECORD_COMMIT;
  return record;
}

static void TestSystemDiagnosticsResetDecode(void)
{
  uint32_t raw;
  uint32_t expected;

  raw = BSP_SYSTEM_DIAGNOSTICS_RAW_RESET_PIN |
        BSP_SYSTEM_DIAGNOSTICS_RAW_RESET_SOFTWARE;
  expected = BSP_SYSTEM_DIAGNOSTICS_RESET_REASON_PIN |
             BSP_SYSTEM_DIAGNOSTICS_RESET_REASON_SOFTWARE;
  CHECK(BSP_SystemDiagnostics_DecodeResetReasons(raw) == expected);

  raw = BSP_SYSTEM_DIAGNOSTICS_RAW_RESET_BOR |
        BSP_SYSTEM_DIAGNOSTICS_RAW_RESET_PIN |
        BSP_SYSTEM_DIAGNOSTICS_RAW_RESET_POR |
        BSP_SYSTEM_DIAGNOSTICS_RAW_RESET_SOFTWARE |
        BSP_SYSTEM_DIAGNOSTICS_RAW_RESET_IWDG |
        BSP_SYSTEM_DIAGNOSTICS_RAW_RESET_WWDG |
        BSP_SYSTEM_DIAGNOSTICS_RAW_RESET_LOW_POWER;
  expected = BSP_SYSTEM_DIAGNOSTICS_RESET_REASON_BOR |
             BSP_SYSTEM_DIAGNOSTICS_RESET_REASON_PIN |
             BSP_SYSTEM_DIAGNOSTICS_RESET_REASON_POR |
             BSP_SYSTEM_DIAGNOSTICS_RESET_REASON_SOFTWARE |
             BSP_SYSTEM_DIAGNOSTICS_RESET_REASON_IWDG |
             BSP_SYSTEM_DIAGNOSTICS_RESET_REASON_WWDG |
             BSP_SYSTEM_DIAGNOSTICS_RESET_REASON_LOW_POWER;
  CHECK(BSP_SystemDiagnostics_DecodeResetReasons(raw) == expected);
  CHECK(BSP_SystemDiagnostics_DecodeResetReasons(0x00000003UL) == 0U);
}

static void TestSystemDiagnosticsRecordLifecycle(void)
{
  BSP_SystemFaultRecord_t retained;
  BSP_SystemFaultRecord_t boot_copy;

  retained = MakeValidFaultRecord();
  CHECK(BSP_SystemDiagnostics_ValidateFaultRecord(&retained) ==
        BSP_SYSTEM_FAULT_RECORD_VALID);
  CHECK(BSP_SystemDiagnostics_ConsumeFaultRecord(&retained, &boot_copy) ==
        BSP_SYSTEM_FAULT_RECORD_VALID);
  CHECK(retained.commit == BSP_SYSTEM_DIAGNOSTICS_FAULT_RECORD_INVALID);
  CHECK(boot_copy.pc == 0x08001234UL);
  CHECK(boot_copy.bfar == 0x20030000UL);

  retained = MakeValidFaultRecord();
  retained.commit = BSP_SYSTEM_DIAGNOSTICS_FAULT_RECORD_INVALID;
  (void)memset(&boot_copy, 0xA5, sizeof(boot_copy));
  CHECK(BSP_SystemDiagnostics_ConsumeFaultRecord(&retained, &boot_copy) ==
        BSP_SYSTEM_FAULT_RECORD_NO_COMMITTED_RECORD);
  CHECK(boot_copy.commit == 0U);
  CHECK(boot_copy.pc == 0U);

  retained = MakeValidFaultRecord();
  retained.version += 1U;
  CHECK(BSP_SystemDiagnostics_ValidateFaultRecord(&retained) ==
        BSP_SYSTEM_FAULT_RECORD_INVALID_VERSION);

  retained = MakeValidFaultRecord();
  retained.size -= 4U;
  CHECK(BSP_SystemDiagnostics_ValidateFaultRecord(&retained) ==
        BSP_SYSTEM_FAULT_RECORD_INVALID_SIZE);

  retained = MakeValidFaultRecord();
  retained.pc ^= 1U;
  CHECK(BSP_SystemDiagnostics_ValidateFaultRecord(&retained) ==
        BSP_SYSTEM_FAULT_RECORD_INVALID_CHECKSUM);

  retained = MakeValidFaultRecord();
  retained.fault_type = 99U;
  retained.checksum = BSP_SystemDiagnostics_CalculateFaultChecksum(&retained);
  CHECK(BSP_SystemDiagnostics_ValidateFaultRecord(&retained) ==
        BSP_SYSTEM_FAULT_RECORD_INVALID_CONTENT);

  retained = MakeValidFaultRecord();
  retained.frame_valid = 0U;
  retained.checksum = BSP_SystemDiagnostics_CalculateFaultChecksum(&retained);
  CHECK(BSP_SystemDiagnostics_ValidateFaultRecord(&retained) ==
        BSP_SYSTEM_FAULT_RECORD_INVALID_CONTENT);
}

static void TestSystemDiagnosticsFrameValidation(void)
{
  uint32_t core_frame_sp;
  uint32_t reasons;

  reasons = BSP_SystemDiagnostics_EvaluateFrame(0x20001000UL,
                                                 0xFFFFFFF9UL,
                                                 0U,
                                                 &core_frame_sp);
  CHECK(reasons == BSP_SYSTEM_DIAGNOSTICS_FRAME_REJECT_NONE);
  CHECK(core_frame_sp == 0x20001000UL);

  reasons = BSP_SystemDiagnostics_EvaluateFrame(0x10001000UL,
                                                 0xFFFFFFE9UL,
                                                 0U,
                                                 &core_frame_sp);
  CHECK(reasons == BSP_SYSTEM_DIAGNOSTICS_FRAME_REJECT_NONE);
  CHECK(core_frame_sp == 0x10001048UL);

  reasons = BSP_SystemDiagnostics_EvaluateFrame(0x20001002UL,
                                                 0xFFFFFFF9UL,
                                                 0U,
                                                 &core_frame_sp);
  CHECK((reasons & BSP_SYSTEM_DIAGNOSTICS_FRAME_REJECT_MISALIGNED) != 0U);
  CHECK(core_frame_sp == 0U);

  reasons = BSP_SystemDiagnostics_EvaluateFrame(0x2001FFF0UL,
                                                 0xFFFFFFF9UL,
                                                 0U,
                                                 &core_frame_sp);
  CHECK((reasons & BSP_SYSTEM_DIAGNOSTICS_FRAME_REJECT_OUT_OF_RANGE) != 0U);
  CHECK(core_frame_sp == 0U);

  reasons = BSP_SystemDiagnostics_EvaluateFrame(
    0xDEADBEEFUL,
    0xFFFFFFF9UL,
    BSP_SYSTEM_DIAGNOSTICS_CFSR_STACK_ERROR_MASK,
    &core_frame_sp);
  CHECK((reasons & BSP_SYSTEM_DIAGNOSTICS_FRAME_REJECT_STACK_ERROR) != 0U);
  CHECK((reasons & BSP_SYSTEM_DIAGNOSTICS_FRAME_REJECT_MISALIGNED) != 0U);
  CHECK((reasons & BSP_SYSTEM_DIAGNOSTICS_FRAME_REJECT_OUT_OF_RANGE) != 0U);
  CHECK(core_frame_sp == 0U);
}

static App_SystemTimingSample_t MakeSystemTimingSample(uint32_t gap_ms)
{
  App_SystemTimingSample_t timing;

  (void)memset(&timing, 0, sizeof(timing));
  timing.loop_gap_valid = 1U;
  timing.loop_gap_cycles = gap_ms * 16000U;
  timing.loop_work_cycles = 1600U;
  timing.collector_cycles = 800U;
  timing.snapshot_cycles = 400U;
  timing.uplink_cycles = 300U;
  return timing;
}

static App_SystemHealthInputs_t MakeHealthySystemInputs(uint32_t now_ms)
{
  App_SystemHealthInputs_t health;

  (void)memset(&health, 0, sizeof(health));
  health.has_valid_data = 1U;
  health.last_success_at_ms = now_ms;
  health.has_completed_tx = 1U;
  health.last_completed_at_ms = now_ms;
  health.uplink_completed_count = 1U;
  return health;
}

static void TestSystemMonitorReportContract(void)
{
  App_SystemMonitorReport_t report;

  CHECK(App_SystemMonitor_GetReport(&report) ==
        APP_SYSTEM_MONITOR_RESULT_NOT_INITIALIZED);
  CHECK(App_SystemMonitor_Init(16000000U, 2U, 100U) ==
        APP_SYSTEM_MONITOR_RESULT_OK);
  CHECK(App_SystemMonitor_GetReport(&report) ==
        APP_SYSTEM_MONITOR_RESULT_OK);
  CHECK(report.monitor_valid == 1U);
  CHECK(report.health_state == APP_SYSTEM_HEALTH_STARTING);
  CHECK(App_SystemMonitor_GetReport(0) ==
        APP_SYSTEM_MONITOR_RESULT_INVALID_ARGUMENT);
}

static void TestSystemMonitorUserDesignedLifecycle(void)
{
  App_SystemTimingSample_t timing;
  App_SystemHealthInputs_t health;

  CHECK(App_SystemMonitor_Init(16000000U, 2U, 0U) ==
        APP_SYSTEM_MONITOR_RESULT_OK);
  CHECK(g_app_system_monitor_report.health_state ==
        APP_SYSTEM_HEALTH_STARTING);
  CHECK(g_app_system_monitor_report.loop_warning_cycles == 80000U);
  CHECK(g_app_system_monitor_report.loop_hard_cycles == 400000U);

  timing = MakeSystemTimingSample(3U);
  health = MakeHealthySystemInputs(20U);
  CHECK(App_SystemMonitor_Process(&timing, &health, 20U) ==
        APP_SYSTEM_MONITOR_RESULT_OK);
  CHECK(g_app_system_monitor_report.health_state ==
        APP_SYSTEM_HEALTH_HEALTHY);
  CHECK(g_app_system_monitor_report.active_reasons ==
        APP_SYSTEM_HEALTH_REASON_NONE);
  CHECK(g_app_system_monitor_report.observed_reasons ==
        APP_SYSTEM_HEALTH_REASON_NONE);
  CHECK(g_app_system_monitor_report.hard_overrun_count == 0U);
  CHECK(g_app_system_monitor_report.transition_count == 1U);

  timing = MakeSystemTimingSample(27U);
  health = MakeHealthySystemInputs(10000U);
  CHECK(App_SystemMonitor_Process(&timing, &health, 10000U) ==
        APP_SYSTEM_MONITOR_RESULT_OK);
  CHECK(g_app_system_monitor_report.health_state ==
        APP_SYSTEM_HEALTH_UNHEALTHY);
  CHECK(g_app_system_monitor_report.active_reasons ==
        APP_SYSTEM_HEALTH_REASON_LOOP_HARD_OVERRUN);
  CHECK(g_app_system_monitor_report.observed_reasons ==
        APP_SYSTEM_HEALTH_REASON_LOOP_HARD_OVERRUN);
  CHECK(g_app_system_monitor_report.hard_overrun_count == 1U);
  CHECK(g_app_system_monitor_report.transition_count == 2U);

  timing = MakeSystemTimingSample(3U);
  health = MakeHealthySystemInputs(10500U);
  (void)App_SystemMonitor_Process(&timing, &health, 10500U);
  CHECK(g_app_system_monitor_report.health_state ==
        APP_SYSTEM_HEALTH_UNHEALTHY);
  CHECK(g_app_system_monitor_report.active_reasons ==
        APP_SYSTEM_HEALTH_REASON_LOOP_HARD_OVERRUN);
  CHECK(g_app_system_monitor_report.hard_overrun_count == 1U);
  CHECK(g_app_system_monitor_report.transition_count == 2U);

  timing = MakeSystemTimingSample(3U);
  health = MakeHealthySystemInputs(11100U);
  (void)App_SystemMonitor_Process(&timing, &health, 11100U);
  CHECK(g_app_system_monitor_report.health_state ==
        APP_SYSTEM_HEALTH_HEALTHY);
  CHECK(g_app_system_monitor_report.active_reasons ==
        APP_SYSTEM_HEALTH_REASON_NONE);
  CHECK(g_app_system_monitor_report.observed_reasons ==
        APP_SYSTEM_HEALTH_REASON_LOOP_HARD_OVERRUN);
  CHECK(g_app_system_monitor_report.hard_overrun_count == 1U);
  CHECK(g_app_system_monitor_report.transition_count == 3U);

  timing = MakeSystemTimingSample(3U);
  health = MakeHealthySystemInputs(12000U);
  health.source_offline = 1U;
  (void)App_SystemMonitor_Process(&timing, &health, 12000U);
  CHECK(g_app_system_monitor_report.health_state ==
        APP_SYSTEM_HEALTH_DEGRADED);
  CHECK(g_app_system_monitor_report.active_reasons ==
        APP_SYSTEM_HEALTH_REASON_SOURCE_OFFLINE);
  CHECK(g_app_system_monitor_report.observed_reasons ==
        (APP_SYSTEM_HEALTH_REASON_LOOP_HARD_OVERRUN |
         APP_SYSTEM_HEALTH_REASON_SOURCE_OFFLINE));
  CHECK(g_app_system_monitor_report.hard_overrun_count == 1U);
  CHECK(g_app_system_monitor_report.transition_count == 4U);

  timing = MakeSystemTimingSample(3U);
  health = MakeHealthySystemInputs(14015U);
  (void)App_SystemMonitor_Process(&timing, &health, 14015U);
  CHECK(g_app_system_monitor_report.health_state ==
        APP_SYSTEM_HEALTH_HEALTHY);
  CHECK(g_app_system_monitor_report.active_reasons ==
        APP_SYSTEM_HEALTH_REASON_NONE);
  CHECK(g_app_system_monitor_report.observed_reasons ==
        (APP_SYSTEM_HEALTH_REASON_LOOP_HARD_OVERRUN |
         APP_SYSTEM_HEALTH_REASON_SOURCE_OFFLINE));
  CHECK(g_app_system_monitor_report.hard_overrun_count == 1U);
  CHECK(g_app_system_monitor_report.transition_count == 5U);
  CHECK(g_app_system_monitor_report.loop_gap_max_cycles == 432000U);
}

static void TestSystemMonitorWarningRecovery(void)
{
  App_SystemTimingSample_t timing;
  App_SystemHealthInputs_t health;

  CHECK(App_SystemMonitor_Init(16000000U, 2U, 0U) ==
        APP_SYSTEM_MONITOR_RESULT_OK);
  timing = MakeSystemTimingSample(3U);
  health = MakeHealthySystemInputs(20U);
  (void)App_SystemMonitor_Process(&timing, &health, 20U);

  timing = MakeSystemTimingSample(5U);
  health = MakeHealthySystemInputs(1000U);
  (void)App_SystemMonitor_Process(&timing, &health, 1000U);
  CHECK(g_app_system_monitor_report.health_state ==
        APP_SYSTEM_HEALTH_DEGRADED);
  CHECK(g_app_system_monitor_report.warning_count == 1U);
  CHECK(g_app_system_monitor_report.hard_overrun_count == 0U);
  CHECK(g_app_system_monitor_report.active_reasons ==
        APP_SYSTEM_HEALTH_REASON_LOOP_WARNING);

  timing = MakeSystemTimingSample(3U);
  health = MakeHealthySystemInputs(1999U);
  (void)App_SystemMonitor_Process(&timing, &health, 1999U);
  CHECK(g_app_system_monitor_report.active_reasons ==
        APP_SYSTEM_HEALTH_REASON_LOOP_WARNING);

  health = MakeHealthySystemInputs(2000U);
  (void)App_SystemMonitor_Process(&timing, &health, 2000U);
  CHECK(g_app_system_monitor_report.health_state ==
        APP_SYSTEM_HEALTH_HEALTHY);
  CHECK(g_app_system_monitor_report.active_reasons ==
        APP_SYSTEM_HEALTH_REASON_NONE);
  CHECK(g_app_system_monitor_report.observed_reasons ==
        APP_SYSTEM_HEALTH_REASON_LOOP_WARNING);
}

static void TestSystemMonitorStartupInvalidAndTickWrap(void)
{
  App_SystemTimingSample_t timing;
  App_SystemHealthInputs_t health;

  CHECK(App_SystemMonitor_Init(0U, 0U, 0U) ==
        APP_SYSTEM_MONITOR_RESULT_INVALID_CONFIGURATION);
  CHECK(g_app_system_monitor_report.health_state ==
        APP_SYSTEM_HEALTH_UNHEALTHY);
  CHECK(g_app_system_monitor_report.active_reasons ==
        APP_SYSTEM_HEALTH_REASON_MONITOR_INVALID);
  CHECK(g_app_system_monitor_report.observed_reasons ==
        APP_SYSTEM_HEALTH_REASON_MONITOR_INVALID);

  CHECK(App_SystemMonitor_Init(16000000U, 2U, 0xFFFFFF00UL) ==
        APP_SYSTEM_MONITOR_RESULT_OK);
  timing = MakeSystemTimingSample(3U);
  (void)memset(&health, 0, sizeof(health));
  (void)App_SystemMonitor_Process(&timing, &health, 0x00000050UL);
  CHECK(g_app_system_monitor_report.health_state ==
        APP_SYSTEM_HEALTH_STARTING);

  (void)App_SystemMonitor_Process(&timing, &health, 0x00001670UL);
  CHECK(g_app_system_monitor_report.startup_completed == 1U);
  CHECK(g_app_system_monitor_report.health_state ==
        APP_SYSTEM_HEALTH_DEGRADED);
  CHECK((g_app_system_monitor_report.active_reasons &
         APP_SYSTEM_HEALTH_REASON_DATA_STALE) != 0U);
  CHECK((g_app_system_monitor_report.active_reasons &
         APP_SYSTEM_HEALTH_REASON_UPLINK_STALLED) != 0U);

  CHECK(App_SystemMonitor_Process(0, &health, 0U) ==
        APP_SYSTEM_MONITOR_RESULT_INVALID_ARGUMENT);
  CHECK(App_SystemMonitor_Process(&timing, 0, 0U) ==
        APP_SYSTEM_MONITOR_RESULT_INVALID_ARGUMENT);
}

static void TestSystemMonitorUplinkErrorNeedsCompletionToRecover(void)
{
  App_SystemTimingSample_t timing;
  App_SystemHealthInputs_t health;

  CHECK(App_SystemMonitor_Init(16000000U, 2U, 0U) ==
        APP_SYSTEM_MONITOR_RESULT_OK);
  timing = MakeSystemTimingSample(3U);
  health = MakeHealthySystemInputs(20U);
  (void)App_SystemMonitor_Process(&timing, &health, 20U);

  health = MakeHealthySystemInputs(1000U);
  health.uplink_error_count = 1U;
  (void)App_SystemMonitor_Process(&timing, &health, 1000U);
  CHECK(g_app_system_monitor_report.health_state ==
        APP_SYSTEM_HEALTH_DEGRADED);
  CHECK((g_app_system_monitor_report.active_reasons &
         APP_SYSTEM_HEALTH_REASON_UPLINK_ERROR) != 0U);

  health = MakeHealthySystemInputs(1500U);
  health.uplink_error_count = 1U;
  (void)App_SystemMonitor_Process(&timing, &health, 1500U);
  CHECK(g_app_system_monitor_report.health_state ==
        APP_SYSTEM_HEALTH_DEGRADED);

  health = MakeHealthySystemInputs(2000U);
  health.uplink_error_count = 1U;
  health.uplink_completed_count = 2U;
  (void)App_SystemMonitor_Process(&timing, &health, 2000U);
  CHECK(g_app_system_monitor_report.health_state ==
        APP_SYSTEM_HEALTH_HEALTHY);
  CHECK((g_app_system_monitor_report.active_reasons &
         APP_SYSTEM_HEALTH_REASON_UPLINK_ERROR) == 0U);
  CHECK((g_app_system_monitor_report.observed_reasons &
         APP_SYSTEM_HEALTH_REASON_UPLINK_ERROR) != 0U);
}

static void TestSystemMonitorFreshnessAndTxSilenceBoundaries(void)
{
  App_SystemTimingSample_t timing;
  App_SystemHealthInputs_t health;

  CHECK(App_SystemMonitor_Init(16000000U, 2U, 0U) ==
        APP_SYSTEM_MONITOR_RESULT_OK);
  CHECK((g_app_system_monitor_report_version & 1U) == 0U);
  timing = MakeSystemTimingSample(3U);

  health = MakeHealthySystemInputs(20U);
  (void)App_SystemMonitor_Process(&timing, &health, 20U);
  CHECK((g_app_system_monitor_report_version & 1U) == 0U);

  health = MakeHealthySystemInputs(1520U);
  health.last_success_at_ms = 20U;
  (void)App_SystemMonitor_Process(&timing, &health, 1520U);
  CHECK(g_app_system_monitor_report.data_age_ms == 1500U);
  CHECK(g_app_system_monitor_report.health_state ==
        APP_SYSTEM_HEALTH_HEALTHY);

  health = MakeHealthySystemInputs(1521U);
  health.last_success_at_ms = 20U;
  (void)App_SystemMonitor_Process(&timing, &health, 1521U);
  CHECK(g_app_system_monitor_report.data_age_ms == 1501U);
  CHECK(g_app_system_monitor_report.health_state ==
        APP_SYSTEM_HEALTH_DEGRADED);
  CHECK((g_app_system_monitor_report.active_reasons &
         APP_SYSTEM_HEALTH_REASON_DATA_STALE) != 0U);

  health = MakeHealthySystemInputs(2000U);
  (void)App_SystemMonitor_Process(&timing, &health, 2000U);
  CHECK(g_app_system_monitor_report.health_state ==
        APP_SYSTEM_HEALTH_HEALTHY);

  health = MakeHealthySystemInputs(8000U);
  health.last_completed_at_ms = 2000U;
  (void)App_SystemMonitor_Process(&timing, &health, 8000U);
  CHECK(g_app_system_monitor_report.tx_silence_ms == 6000U);
  CHECK(g_app_system_monitor_report.health_state ==
        APP_SYSTEM_HEALTH_HEALTHY);

  health = MakeHealthySystemInputs(8001U);
  health.last_completed_at_ms = 2000U;
  (void)App_SystemMonitor_Process(&timing, &health, 8001U);
  CHECK(g_app_system_monitor_report.tx_silence_ms == 6001U);
  CHECK(g_app_system_monitor_report.health_state ==
        APP_SYSTEM_HEALTH_DEGRADED);
  CHECK((g_app_system_monitor_report.active_reasons &
         APP_SYSTEM_HEALTH_REASON_UPLINK_STALLED) != 0U);

  health = MakeHealthySystemInputs(8100U);
  health.uplink_completed_count = 2U;
  (void)App_SystemMonitor_Process(&timing, &health, 8100U);
  CHECK(g_app_system_monitor_report.health_state ==
        APP_SYSTEM_HEALTH_HEALTHY);
  CHECK((g_app_system_monitor_report.observed_reasons &
         APP_SYSTEM_HEALTH_REASON_DATA_STALE) != 0U);
  CHECK((g_app_system_monitor_report.observed_reasons &
         APP_SYSTEM_HEALTH_REASON_UPLINK_STALLED) != 0U);
}

static App_WatchdogGateInputs_t MakeWatchdogGateInputs(uint32_t reasons)
{
  App_WatchdogGateInputs_t inputs;

  inputs.monitor_evaluation_valid = 1U;
  inputs.active_reasons = reasons;
  return inputs;
}

static void TestWatchdogGateRefreshPeriodAndAllowedReasons(void)
{
  App_WatchdogGateDecision_t decision;
  App_WatchdogGateInputs_t inputs;

  CHECK(App_WatchdogGate_Init(1000U) == APP_WATCHDOG_GATE_RESULT_OK);
  inputs = MakeWatchdogGateInputs(APP_SYSTEM_HEALTH_REASON_NONE);
  CHECK(App_WatchdogGate_Evaluate(&inputs, 1249U, &decision) ==
        APP_WATCHDOG_GATE_RESULT_OK);
  CHECK(decision == APP_WATCHDOG_GATE_DECISION_WAIT_PERIOD);
  CHECK(App_WatchdogGate_Evaluate(&inputs, 1250U, &decision) ==
        APP_WATCHDOG_GATE_RESULT_OK);
  CHECK(decision == APP_WATCHDOG_GATE_DECISION_REFRESH_DUE);
  CHECK(App_WatchdogGate_RecordRefreshResult(
          APP_WATCHDOG_GATE_REFRESH_SUCCESS,
          1250U) == APP_WATCHDOG_GATE_RESULT_OK);
  CHECK(g_app_watchdog_gate_report.last_successful_refresh_at_ms == 1250U);
  CHECK(g_app_watchdog_gate_report.refresh_completed_count == 1U);
  CHECK(g_app_watchdog_gate_report.decision ==
        APP_WATCHDOG_GATE_DECISION_WAIT_PERIOD);

  inputs = MakeWatchdogGateInputs(APP_WATCHDOG_GATE_ALLOW_REASONS);
  (void)App_WatchdogGate_Evaluate(&inputs, 1500U, &decision);
  CHECK(decision == APP_WATCHDOG_GATE_DECISION_REFRESH_DUE);
  CHECK(g_app_watchdog_gate_report.latched_reasons == 0U);
  CHECK(g_app_watchdog_gate_report.active_hold_reasons == 0U);
}

static void TestCollectorFaultMonitorToWatchdogLatch(void)
{
  App_SystemTimingSample_t timing;
  App_SystemHealthInputs_t health;
  App_WatchdogGateInputs_t gate_inputs;
  App_WatchdogGateDecision_t decision;

  CHECK(App_SystemMonitor_Init(16000000U, 2U, 0U) ==
        APP_SYSTEM_MONITOR_RESULT_OK);
  CHECK(App_WatchdogGate_Init(0U) == APP_WATCHDOG_GATE_RESULT_OK);
  timing = MakeSystemTimingSample(3U);
  health = MakeHealthySystemInputs(20U);
  CHECK(App_SystemMonitor_Process(&timing, &health, 20U) ==
        APP_SYSTEM_MONITOR_RESULT_OK);

  health = MakeHealthySystemInputs(100U);
  health.collector_fault = 1U;
  CHECK(App_SystemMonitor_Process(&timing, &health, 100U) ==
        APP_SYSTEM_MONITOR_RESULT_OK);
  CHECK(g_app_system_monitor_report.health_state ==
        APP_SYSTEM_HEALTH_UNHEALTHY);
  CHECK(g_app_system_monitor_report.active_reasons ==
        APP_SYSTEM_HEALTH_REASON_COLLECTOR_FAULT);
  CHECK(g_app_system_monitor_report.observed_reasons ==
        APP_SYSTEM_HEALTH_REASON_COLLECTOR_FAULT);

  gate_inputs = MakeWatchdogGateInputs(
    g_app_system_monitor_report.active_reasons);
  CHECK(App_WatchdogGate_Evaluate(&gate_inputs, 100U, &decision) ==
        APP_WATCHDOG_GATE_RESULT_OK);
  CHECK(decision == APP_WATCHDOG_GATE_DECISION_LATCHED);
  CHECK(g_app_watchdog_gate_report.latched_reasons ==
        APP_SYSTEM_HEALTH_REASON_COLLECTOR_FAULT);

  health = MakeHealthySystemInputs(101U);
  CHECK(App_SystemMonitor_Process(&timing, &health, 101U) ==
        APP_SYSTEM_MONITOR_RESULT_OK);
  CHECK(g_app_system_monitor_report.health_state ==
        APP_SYSTEM_HEALTH_HEALTHY);
  CHECK(g_app_system_monitor_report.active_reasons ==
        APP_SYSTEM_HEALTH_REASON_NONE);
  CHECK(g_app_system_monitor_report.observed_reasons ==
        APP_SYSTEM_HEALTH_REASON_COLLECTOR_FAULT);

  gate_inputs = MakeWatchdogGateInputs(
    g_app_system_monitor_report.active_reasons);
  CHECK(App_WatchdogGate_Evaluate(&gate_inputs, 101U, &decision) ==
        APP_WATCHDOG_GATE_RESULT_OK);
  CHECK(decision == APP_WATCHDOG_GATE_DECISION_LATCHED);
  CHECK(g_app_watchdog_gate_report.latched_reasons ==
        APP_SYSTEM_HEALTH_REASON_COLLECTOR_FAULT);
  CHECK(g_app_watchdog_gate_report.refresh_completed_count == 0U);
}

static void TestWatchdogGateHoldRecoveryAndPriority(void)
{
  App_WatchdogGateDecision_t decision;
  App_WatchdogGateInputs_t inputs;

  (void)App_WatchdogGate_Init(0U);
  inputs = MakeWatchdogGateInputs(
    APP_SYSTEM_HEALTH_REASON_LOOP_HARD_OVERRUN |
    APP_SYSTEM_HEALTH_REASON_SOURCE_OFFLINE);
  (void)App_WatchdogGate_Evaluate(&inputs, 250U, &decision);
  CHECK(decision == APP_WATCHDOG_GATE_DECISION_HOLD);
  CHECK(g_app_watchdog_gate_report.active_hold_reasons ==
        APP_SYSTEM_HEALTH_REASON_LOOP_HARD_OVERRUN);
  CHECK(g_app_watchdog_gate_report.refresh_completed_count == 0U);

  inputs = MakeWatchdogGateInputs(APP_SYSTEM_HEALTH_REASON_SOURCE_OFFLINE);
  (void)App_WatchdogGate_Evaluate(&inputs, 1250U, &decision);
  CHECK(decision == APP_WATCHDOG_GATE_DECISION_REFRESH_DUE);
  CHECK(App_WatchdogGate_RecordRefreshResult(
          APP_WATCHDOG_GATE_REFRESH_SUCCESS,
          1250U) == APP_WATCHDOG_GATE_RESULT_OK);
  CHECK(g_app_watchdog_gate_report.refresh_completed_count == 1U);

  inputs = MakeWatchdogGateInputs(
    APP_SYSTEM_HEALTH_REASON_UPLINK_STALLED |
    APP_SYSTEM_HEALTH_REASON_COLLECTOR_FAULT);
  (void)App_WatchdogGate_Evaluate(&inputs, 1500U, &decision);
  CHECK(decision == APP_WATCHDOG_GATE_DECISION_LATCHED);
  CHECK((g_app_watchdog_gate_report.latched_reasons &
         APP_SYSTEM_HEALTH_REASON_COLLECTOR_FAULT) != 0U);

  inputs = MakeWatchdogGateInputs(APP_SYSTEM_HEALTH_REASON_NONE);
  (void)App_WatchdogGate_Evaluate(&inputs, 5000U, &decision);
  CHECK(decision == APP_WATCHDOG_GATE_DECISION_LATCHED);
}

static void TestWatchdogGateInvalidInputAndUnknownReasons(void)
{
  App_WatchdogGateDecision_t decision;
  App_WatchdogGateInputs_t inputs;

  (void)App_WatchdogGate_Init(0U);
  inputs = MakeWatchdogGateInputs(1UL << 31U);
  CHECK(App_WatchdogGate_Evaluate(&inputs, 250U, &decision) ==
        APP_WATCHDOG_GATE_RESULT_INVALID_INPUT);
  CHECK(decision == APP_WATCHDOG_GATE_DECISION_LATCHED);
  CHECK((g_app_watchdog_gate_report.latched_reasons &
         APP_WATCHDOG_GATE_INTERNAL_UNKNOWN_REASON) != 0U);
  CHECK(g_app_watchdog_gate_report.unknown_active_reasons == (1UL << 31U));

  (void)App_WatchdogGate_Init(0U);
  inputs = MakeWatchdogGateInputs(APP_SYSTEM_HEALTH_REASON_NONE);
  inputs.monitor_evaluation_valid = 0U;
  CHECK(App_WatchdogGate_Evaluate(&inputs, 250U, &decision) ==
        APP_WATCHDOG_GATE_RESULT_INVALID_INPUT);
  CHECK(decision == APP_WATCHDOG_GATE_DECISION_LATCHED);
  CHECK((g_app_watchdog_gate_report.latched_reasons &
         APP_WATCHDOG_GATE_INTERNAL_INPUT_INVALID) != 0U);
}

static void TestWatchdogGateRefreshFailureAndTickWrap(void)
{
  App_WatchdogGateDecision_t decision;
  App_WatchdogGateInputs_t inputs;

  (void)App_WatchdogGate_Init(0xFFFFFF00UL);
  inputs = MakeWatchdogGateInputs(APP_SYSTEM_HEALTH_REASON_NONE);
  (void)App_WatchdogGate_Evaluate(&inputs, 0xFFFFFFF9UL, &decision);
  CHECK(decision == APP_WATCHDOG_GATE_DECISION_WAIT_PERIOD);
  (void)App_WatchdogGate_Evaluate(&inputs, 0x00000000UL, &decision);
  CHECK(decision == APP_WATCHDOG_GATE_DECISION_REFRESH_DUE);
  CHECK(App_WatchdogGate_RecordRefreshResult(
          APP_WATCHDOG_GATE_REFRESH_FAILURE,
          0x00000000UL) == APP_WATCHDOG_GATE_RESULT_OK);
  CHECK(g_app_watchdog_gate_report.refresh_completed_count == 0U);
  CHECK(g_app_watchdog_gate_report.refresh_failed_count == 1U);
  CHECK(g_app_watchdog_gate_report.last_successful_refresh_at_ms ==
        0xFFFFFF00UL);
  CHECK(g_app_watchdog_gate_report.decision ==
        APP_WATCHDOG_GATE_DECISION_LATCHED);
  CHECK((g_app_watchdog_gate_report.latched_reasons &
         APP_WATCHDOG_GATE_INTERNAL_REFRESH_FAILED) != 0U);

  (void)App_WatchdogGate_Evaluate(&inputs, 1000U, &decision);
  CHECK(decision == APP_WATCHDOG_GATE_DECISION_LATCHED);
}

static void TestRs485NormalPingPongAndDelayedHandling(void)
{
  static const uint8_t ping[] = "V1|PING\r\n";
  static const uint8_t pong[] = "V1|PONG\r\n";

  InitFakeRs485();
  QueueFakeRs485Bytes(ping, (uint16_t)(sizeof(ping) - 1U), 100U, 0U);
  App_RS485Service_Process(100U);
  CHECK(g_app_rs485_report.state == APP_RS485_STATE_RESPONSE_PENDING);
  CHECK(g_app_rs485_report.rx_byte_count == (sizeof(ping) - 1U));
  CHECK(g_app_rs485_report.complete_frame_count == 1U);
  CHECK(g_app_rs485_report.valid_ping_count == 1U);
  CHECK(g_app_rs485_report.semantic_invalid_count == 0U);

  App_RS485Service_Process(100U);
  CHECK(g_app_rs485_report.state == APP_RS485_STATE_TX_WAIT_COMPLETE);
  CHECK(g_app_rs485_report.tx_start_count == 1U);
  CHECK(g_fake_rs485_write_count == 1U);
  CHECK(g_fake_rs485_last_tx_length == (sizeof(pong) - 1U));
  CHECK(memcmp(g_fake_rs485_last_tx_data,
               pong,
               sizeof(pong) - 1U) == 0);

  g_fake_rs485_tx_event.type = BSP_RS485_TX_EVENT_COMPLETE;
  g_fake_rs485_tx_event.timestamp_ms = 101U;
  App_RS485Service_Process(120U);
  CHECK(g_app_rs485_report.state == APP_RS485_STATE_RX_WAIT);
  CHECK(g_app_rs485_report.tx_success_count == 1U);
  CHECK(g_app_rs485_report.tx_timeout_count == 0U);
  CHECK(g_app_rs485_report.last_tx_completed_at_ms == 101U);
}

static void TestRs485SemanticAndLengthBoundaries(void)
{
  static const uint8_t pang[] = "V1|PANG\r\n";
  uint8_t exactly_32_bytes[APP_RS485_MAX_FRAME_BYTES];

  InitFakeRs485();
  QueueFakeRs485Bytes(pang, (uint16_t)(sizeof(pang) - 1U), 10U, 0U);
  App_RS485Service_Process(10U);
  CHECK(g_app_rs485_report.state == APP_RS485_STATE_RX_WAIT);
  CHECK(g_app_rs485_report.complete_frame_count == 1U);
  CHECK(g_app_rs485_report.semantic_invalid_count == 1U);
  CHECK(g_app_rs485_report.valid_ping_count == 0U);
  CHECK(g_fake_rs485_write_count == 0U);

  InitFakeRs485();
  (void)memset(exactly_32_bytes, 'X', sizeof(exactly_32_bytes));
  exactly_32_bytes[APP_RS485_MAX_FRAME_BYTES - 2U] = '\r';
  exactly_32_bytes[APP_RS485_MAX_FRAME_BYTES - 1U] = '\n';
  QueueFakeRs485Bytes(exactly_32_bytes,
                      (uint16_t)sizeof(exactly_32_bytes),
                      20U,
                      0U);
  App_RS485Service_Process(20U);
  CHECK(g_app_rs485_report.complete_frame_count == 1U);
  CHECK(g_app_rs485_report.semantic_invalid_count == 1U);
  CHECK(g_app_rs485_report.overlength_count == 0U);
  CHECK(g_app_rs485_report.current_frame_length == 0U);
}

static void TestRs485OverlengthAndResynchronization(void)
{
  static const uint8_t delimiter[] = "\r\n";
  static const uint8_t ping[] = "V1|PING\r\n";
  uint8_t overlength[APP_RS485_MAX_FRAME_BYTES + 1U];

  InitFakeRs485();
  (void)memset(overlength, 'X', sizeof(overlength));
  QueueFakeRs485Bytes(overlength,
                      (uint16_t)sizeof(overlength),
                      0U,
                      0U);
  QueueFakeRs485Bytes(delimiter,
                      (uint16_t)(sizeof(delimiter) - 1U),
                      1U,
                      0U);
  QueueFakeRs485Bytes(ping,
                      (uint16_t)(sizeof(ping) - 1U),
                      2U,
                      0U);
  App_RS485Service_Process(2U);
  CHECK(g_app_rs485_report.state == APP_RS485_STATE_RESPONSE_PENDING);
  CHECK(g_app_rs485_report.overlength_count == 1U);
  CHECK(g_app_rs485_report.complete_frame_count == 1U);
  CHECK(g_app_rs485_report.valid_ping_count == 1U);
  CHECK(g_app_rs485_report.semantic_invalid_count == 0U);
}

static void TestRs485InterbyteTimeoutScenarios(void)
{
  static const uint8_t first_part[] = "V1|PI";
  static const uint8_t second_part[] = "NG\r\n";
  static const uint8_t ping[] = "V1|PING\r\n";

  InitFakeRs485();
  QueueFakeRs485Bytes(first_part,
                      (uint16_t)(sizeof(first_part) - 1U),
                      0U,
                      1U);
  QueueFakeRs485Bytes(second_part,
                      (uint16_t)(sizeof(second_part) - 1U),
                      54U,
                      1U);
  App_RS485Service_Process(60U);
  CHECK(g_app_rs485_report.valid_ping_count == 1U);
  CHECK(g_app_rs485_report.interbyte_timeout_count == 0U);
  CHECK(g_app_rs485_report.state == APP_RS485_STATE_RESPONSE_PENDING);

  InitFakeRs485();
  QueueFakeRs485Bytes(first_part,
                      (uint16_t)(sizeof(first_part) - 1U),
                      0U,
                      1U);
  QueueFakeRs485Bytes(second_part,
                      (uint16_t)(sizeof(second_part) - 1U),
                      154U,
                      1U);
  QueueFakeRs485Bytes(ping,
                      (uint16_t)(sizeof(ping) - 1U),
                      200U,
                      1U);
  App_RS485Service_Process(210U);
  CHECK(g_app_rs485_report.interbyte_timeout_count == 1U);
  CHECK(g_app_rs485_report.complete_frame_count == 2U);
  CHECK(g_app_rs485_report.semantic_invalid_count == 1U);
  CHECK(g_app_rs485_report.valid_ping_count == 1U);
  CHECK(g_app_rs485_report.state == APP_RS485_STATE_RESPONSE_PENDING);
}

static void TestRs485RejectsStaleNowBeforeRxEvent(void)
{
  static const uint8_t first_byte[] = "V";
  static const uint8_t remaining_bytes[] = "1|PING\r\n";

  InitFakeRs485();
  QueueFakeRs485Bytes(first_byte,
                      (uint16_t)(sizeof(first_byte) - 1U),
                      101U,
                      0U);
  App_RS485Service_Process(100U);
  CHECK(g_app_rs485_report.state == APP_RS485_STATE_RX_WAIT);
  CHECK(g_app_rs485_report.current_frame_length == 1U);
  CHECK(g_app_rs485_report.interbyte_timeout_count == 0U);

  QueueFakeRs485Bytes(remaining_bytes,
                      (uint16_t)(sizeof(remaining_bytes) - 1U),
                      102U,
                      1U);
  App_RS485Service_Process(109U);
  CHECK(g_app_rs485_report.state == APP_RS485_STATE_RESPONSE_PENDING);
  CHECK(g_app_rs485_report.rx_byte_count == 9U);
  CHECK(g_app_rs485_report.complete_frame_count == 1U);
  CHECK(g_app_rs485_report.valid_ping_count == 1U);
  CHECK(g_app_rs485_report.semantic_invalid_count == 0U);
  CHECK(g_app_rs485_report.interbyte_timeout_count == 0U);
}

static void TestRs485RxSilenceTimeoutAcrossTickWrap(void)
{
  static const uint8_t first_byte[] = "V";

  InitFakeRs485();
  QueueFakeRs485Bytes(first_byte,
                      (uint16_t)(sizeof(first_byte) - 1U),
                      UINT32_MAX - 50U,
                      0U);
  App_RS485Service_Process(UINT32_MAX - 50U);
  CHECK(g_app_rs485_report.current_frame_length == 1U);
  CHECK(g_app_rs485_report.interbyte_timeout_count == 0U);

  App_RS485Service_Process(49U);
  CHECK(g_app_rs485_report.state == APP_RS485_STATE_RX_WAIT);
  CHECK(g_app_rs485_report.current_frame_length == 0U);
  CHECK(g_app_rs485_report.interbyte_timeout_count == 1U);
}

static void TestRs485OverflowDiscardsUntrustedQueuedPing(void)
{
  static const uint8_t ping[] = "V1|PING\r\n";

  InitFakeRs485();
  QueueFakeRs485Bytes(ping, (uint16_t)(sizeof(ping) - 1U), 10U, 0U);
  g_fake_rs485_fault_event.overflow_count = 1U;
  g_fake_rs485_fault_event.last_overflow_at_ms = 10U;
  App_RS485Service_Process(20U);
  CHECK(g_app_rs485_report.state == APP_RS485_STATE_RX_DISCARD);
  CHECK(g_app_rs485_report.rx_queue_overflow_count == 1U);
  CHECK(g_app_rs485_report.rx_byte_count == sizeof(ping));
  CHECK(g_app_rs485_report.complete_frame_count == 0U);
  CHECK(g_app_rs485_report.valid_ping_count == 0U);
  CHECK(g_fake_rs485_flush_count == 1U);

  QueueFakeRs485Bytes(ping, (uint16_t)(sizeof(ping) - 1U), 30U, 0U);
  App_RS485Service_Process(30U);
  CHECK(g_app_rs485_report.state == APP_RS485_STATE_RX_WAIT);
  CHECK(g_app_rs485_report.complete_frame_count == 0U);
  CHECK(g_app_rs485_report.valid_ping_count == 0U);

  QueueFakeRs485Bytes(ping, (uint16_t)(sizeof(ping) - 1U), 40U, 0U);
  App_RS485Service_Process(40U);
  CHECK(g_app_rs485_report.state == APP_RS485_STATE_RESPONSE_PENDING);
  CHECK(g_app_rs485_report.complete_frame_count == 1U);
  CHECK(g_app_rs485_report.valid_ping_count == 1U);
}

static void TestRs485TransmitFailuresAndDeadlines(void)
{
  static const uint8_t ping[] = "V1|PING\r\n";

  InitFakeRs485();
  QueueFakeRs485Bytes(ping, (uint16_t)(sizeof(ping) - 1U), 0U, 0U);
  App_RS485Service_Process(0U);
  g_fake_rs485_write_status = BSP_RS485_STATUS_ERROR;
  App_RS485Service_Process(0U);
  CHECK(g_app_rs485_report.state == APP_RS485_STATE_RX_WAIT);
  CHECK(g_app_rs485_report.tx_start_count == 0U);
  CHECK(g_app_rs485_report.tx_start_failure_count == 1U);
  CHECK(g_app_rs485_report.tx_error_count == 1U);

  InitFakeRs485();
  QueueFakeRs485Bytes(ping, (uint16_t)(sizeof(ping) - 1U), 100U, 0U);
  App_RS485Service_Process(100U);
  App_RS485Service_Process(100U);
  App_RS485Service_Process(110U);
  CHECK(g_app_rs485_report.state == APP_RS485_STATE_RX_WAIT);
  CHECK(g_app_rs485_report.tx_timeout_count == 1U);
  CHECK(g_fake_rs485_abort_count == 1U);

  InitFakeRs485();
  QueueFakeRs485Bytes(ping, (uint16_t)(sizeof(ping) - 1U), 100U, 0U);
  App_RS485Service_Process(100U);
  App_RS485Service_Process(100U);
  g_fake_rs485_tx_event.type = BSP_RS485_TX_EVENT_COMPLETE;
  g_fake_rs485_tx_event.timestamp_ms = 111U;
  App_RS485Service_Process(130U);
  CHECK(g_app_rs485_report.state == APP_RS485_STATE_RX_WAIT);
  CHECK(g_app_rs485_report.tx_success_count == 0U);
  CHECK(g_app_rs485_report.tx_timeout_count == 1U);
  CHECK(g_app_rs485_report.tx_late_complete_count == 1U);
  CHECK(g_fake_rs485_abort_count == 0U);
}

static void TestRs485InitFailure(void)
{
  ResetFakeRs485();
  g_fake_rs485_init_status = BSP_RS485_STATUS_ERROR;
  CHECK(App_RS485Service_Init() ==
        APP_RS485_SERVICE_RESULT_TRANSPORT_ERROR);
  CHECK(g_app_rs485_report.state == APP_RS485_STATE_NOT_INITIALIZED);
  CHECK(g_app_rs485_report.uart_error_count == 1U);
  App_RS485Service_Process(100U);
  CHECK(g_fake_rs485_write_count == 0U);
}

int main(void)
{
  TestSht30InitAndCommand();
  TestSht30DecodeAndRead();
  TestDataServiceContract();
  TestDataServiceRejectsInvalidBatches();
  TestSnapshotConsumerTransitions();
  TestUartBringupSchedule();
  TestUartBringupInitFailureAndTickWrap();
  TestSnapshotTextEvent();
  TestSnapshotTextHeartbeatAndOffline();
  TestSnapshotTextRejectsInvalidMessages();
  TestSnapshotTextWorstCaseFits();
  TestUplinkServiceCompletionAndHeartbeat();
  TestUplinkServiceCoalescesLatestPending();
  TestUplinkServiceBusyAndFormatFailure();
  TestUplinkServiceStartErrorRetriesAndRecovers();
  TestUplinkServiceTransportErrorAndInitFailure();
  TestSystemDiagnosticsResetDecode();
  TestSystemDiagnosticsRecordLifecycle();
  TestSystemDiagnosticsFrameValidation();
  TestSystemMonitorReportContract();
  TestSystemMonitorUserDesignedLifecycle();
  TestSystemMonitorWarningRecovery();
  TestSystemMonitorStartupInvalidAndTickWrap();
  TestSystemMonitorUplinkErrorNeedsCompletionToRecover();
  TestSystemMonitorFreshnessAndTxSilenceBoundaries();
  TestWatchdogGateRefreshPeriodAndAllowedReasons();
  TestCollectorFaultMonitorToWatchdogLatch();
  TestWatchdogGateHoldRecoveryAndPriority();
  TestWatchdogGateInvalidInputAndUnknownReasons();
  TestWatchdogGateRefreshFailureAndTickWrap();
  TestRs485NormalPingPongAndDelayedHandling();
  TestRs485SemanticAndLengthBoundaries();
  TestRs485OverlengthAndResynchronization();
  TestRs485InterbyteTimeoutScenarios();
  TestRs485RejectsStaleNowBeforeRxEvent();
  TestRs485RxSilenceTimeoutAcrossTickWrap();
  TestRs485OverflowDiscardsUntrustedQueuedPing();
  TestRs485TransmitFailuresAndDeadlines();
  TestRs485InitFailure();

  if (g_failures == 0U)
  {
    (void)printf("PASS: %lu checks executed against production C sources.\n",
                 (unsigned long)g_checks);
    return 0;
  }

  (void)printf("FAIL: %lu of %lu checks failed.\n",
               (unsigned long)g_failures,
               (unsigned long)g_checks);
  return 1;
}
