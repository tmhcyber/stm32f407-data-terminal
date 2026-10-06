#include "app_sht30_collector.h"

#include "app_data_service.h"
#include "bsp_i2c.h"
#include "sht30.h"

#define APP_SHT30_I2C_TIMEOUT_MS  20U

static SHT30_t s_sht30;
static uint32_t s_state_started_at_ms;
static uint32_t s_state_delay_ms;
static uint32_t s_retries_used;

volatile App_SHT30_CollectorReport_t g_app_sht30_collector_report;

static uint8_t App_SHT30_HasElapsed(uint32_t now_ms,
                                    uint32_t started_at_ms,
                                    uint32_t delay_ms)
{
  return (uint8_t)((uint32_t)(now_ms - started_at_ms) >= delay_ms);
}

static SHT30_BusStatus_t App_SHT30_MapBspStatus(BSP_I2C_Status_t status)
{
  switch (status)
  {
    case BSP_I2C_STATUS_OK:
      return SHT30_BUS_STATUS_OK;

    case BSP_I2C_STATUS_INVALID_ARGUMENT:
      return SHT30_BUS_STATUS_INVALID_ARGUMENT;

    case BSP_I2C_STATUS_NACK:
      return SHT30_BUS_STATUS_NACK;

    case BSP_I2C_STATUS_TIMEOUT:
      return SHT30_BUS_STATUS_TIMEOUT;

    case BSP_I2C_STATUS_BUSY:
      return SHT30_BUS_STATUS_BUSY;

    case BSP_I2C_STATUS_BUS_ERROR:
    default:
      return SHT30_BUS_STATUS_ERROR;
  }
}

static SHT30_BusStatus_t App_SHT30_BusWrite(
  void *context,
  uint8_t address_7bit,
  const uint8_t *data,
  uint16_t length,
  uint32_t timeout_ms)
{
  (void)context;

  return App_SHT30_MapBspStatus(BSP_I2C1_Write(address_7bit,
                                               data,
                                               length,
                                               timeout_ms));
}

static SHT30_BusStatus_t App_SHT30_BusRead(
  void *context,
  uint8_t address_7bit,
  uint8_t *data,
  uint16_t length,
  uint32_t timeout_ms)
{
  (void)context;

  return App_SHT30_MapBspStatus(BSP_I2C1_Read(address_7bit,
                                              data,
                                              length,
                                              timeout_ms));
}

static App_SHT30_CollectorStatus_t App_SHT30_MapStatus(
  SHT30_Status_t status)
{
  switch (status)
  {
    case SHT30_STATUS_OK:
      return APP_SHT30_COLLECTOR_STATUS_OK;

    case SHT30_STATUS_INVALID_ARGUMENT:
      return APP_SHT30_COLLECTOR_STATUS_INVALID_ARGUMENT;

    case SHT30_STATUS_BUS_NACK:
      return APP_SHT30_COLLECTOR_STATUS_NACK;

    case SHT30_STATUS_BUS_TIMEOUT:
      return APP_SHT30_COLLECTOR_STATUS_TIMEOUT;

    case SHT30_STATUS_BUS_BUSY:
      return APP_SHT30_COLLECTOR_STATUS_BUSY;

    case SHT30_STATUS_TEMPERATURE_CRC_ERROR:
      return APP_SHT30_COLLECTOR_STATUS_TEMPERATURE_CRC_ERROR;

    case SHT30_STATUS_HUMIDITY_CRC_ERROR:
      return APP_SHT30_COLLECTOR_STATUS_HUMIDITY_CRC_ERROR;

    case SHT30_STATUS_BUS_ERROR:
    default:
      return APP_SHT30_COLLECTOR_STATUS_BUS_ERROR;
  }
}

static App_DataStatus_t App_SHT30_MapDataStatus(
  App_SHT30_CollectorStatus_t status)
{
  switch (status)
  {
    case APP_SHT30_COLLECTOR_STATUS_NOT_RUN:
      return APP_DATA_STATUS_NOT_RUN;

    case APP_SHT30_COLLECTOR_STATUS_OK:
      return APP_DATA_STATUS_OK;

    case APP_SHT30_COLLECTOR_STATUS_INVALID_ARGUMENT:
      return APP_DATA_STATUS_INVALID_ARGUMENT;

    case APP_SHT30_COLLECTOR_STATUS_NACK:
      return APP_DATA_STATUS_NACK;

    case APP_SHT30_COLLECTOR_STATUS_TIMEOUT:
      return APP_DATA_STATUS_TIMEOUT;

    case APP_SHT30_COLLECTOR_STATUS_BUSY:
      return APP_DATA_STATUS_BUSY;

    case APP_SHT30_COLLECTOR_STATUS_TEMPERATURE_CRC_ERROR:
      return APP_DATA_STATUS_TEMPERATURE_CRC_ERROR;

    case APP_SHT30_COLLECTOR_STATUS_HUMIDITY_CRC_ERROR:
      return APP_DATA_STATUS_HUMIDITY_CRC_ERROR;

    case APP_SHT30_COLLECTOR_STATUS_DATA_SERVICE_ERROR:
    case APP_SHT30_COLLECTOR_STATUS_BUS_ERROR:
    default:
      return APP_DATA_STATUS_BUS_ERROR;
  }
}

static App_DataQuality_t App_SHT30_MapDataQuality(
  App_SHT30_Quality_t quality)
{
  switch (quality)
  {
    case APP_SHT30_QUALITY_VALID:
      return APP_DATA_QUALITY_VALID;

    case APP_SHT30_QUALITY_STALE:
      return APP_DATA_QUALITY_STALE;

    case APP_SHT30_QUALITY_OFFLINE:
      return APP_DATA_QUALITY_OFFLINE;

    case APP_SHT30_QUALITY_NOT_AVAILABLE:
    default:
      return APP_DATA_QUALITY_NOT_AVAILABLE;
  }
}

static void App_SHT30_RecordDataServiceError(uint32_t now_ms)
{
  g_app_sht30_collector_report.last_status =
    APP_SHT30_COLLECTOR_STATUS_DATA_SERVICE_ERROR;
  g_app_sht30_collector_report.last_error_at_ms = now_ms;
  ++g_app_sht30_collector_report.data_service_error_count;
  g_app_sht30_collector_report.state = APP_SHT30_COLLECTOR_STATE_FAULT;
}

static uint8_t App_SHT30_PublishQuality(uint32_t now_ms)
{
  App_DataResult_t result;

  result = App_DataService_UpdateSourceQuality(
    APP_DATA_SOURCE_SHT30,
    App_SHT30_MapDataQuality(g_app_sht30_collector_report.quality),
    App_SHT30_MapDataStatus(g_app_sht30_collector_report.last_status),
    now_ms);
  if (result != APP_DATA_RESULT_OK)
  {
    App_SHT30_RecordDataServiceError(now_ms);
    return 0U;
  }

  return 1U;
}

static void App_SHT30_SetWaitState(App_SHT30_CollectorState_t state,
                                   uint32_t now_ms,
                                   uint32_t delay_ms)
{
  g_app_sht30_collector_report.state = state;
  s_state_started_at_ms = now_ms;
  s_state_delay_ms = delay_ms;
}

static void App_SHT30_RecordAttemptError(SHT30_Status_t status,
                                         uint32_t now_ms)
{
  g_app_sht30_collector_report.last_status = App_SHT30_MapStatus(status);
  g_app_sht30_collector_report.last_error_at_ms = now_ms;

  switch (status)
  {
    case SHT30_STATUS_INVALID_ARGUMENT:
      ++g_app_sht30_collector_report.invalid_argument_count;
      break;

    case SHT30_STATUS_BUS_NACK:
      ++g_app_sht30_collector_report.nack_count;
      break;

    case SHT30_STATUS_BUS_TIMEOUT:
      ++g_app_sht30_collector_report.timeout_count;
      break;

    case SHT30_STATUS_BUS_BUSY:
      ++g_app_sht30_collector_report.busy_count;
      break;

    case SHT30_STATUS_TEMPERATURE_CRC_ERROR:
      ++g_app_sht30_collector_report.temperature_crc_error_count;
      break;

    case SHT30_STATUS_HUMIDITY_CRC_ERROR:
      ++g_app_sht30_collector_report.humidity_crc_error_count;
      break;

    case SHT30_STATUS_BUS_ERROR:
    default:
      ++g_app_sht30_collector_report.bus_error_count;
      break;
  }
}

static void App_SHT30_CompleteRoundFailure(uint32_t now_ms)
{
  ++g_app_sht30_collector_report.total_round_count;
  ++g_app_sht30_collector_report.failed_round_count;
  ++g_app_sht30_collector_report.consecutive_failed_rounds;

  if (g_app_sht30_collector_report.last_status ==
      APP_SHT30_COLLECTOR_STATUS_INVALID_ARGUMENT)
  {
    if (g_app_sht30_collector_report.has_valid_measurement != 0U)
    {
      g_app_sht30_collector_report.quality = APP_SHT30_QUALITY_STALE;
    }
    else
    {
      g_app_sht30_collector_report.quality =
        APP_SHT30_QUALITY_NOT_AVAILABLE;
    }

    g_app_sht30_collector_report.state = APP_SHT30_COLLECTOR_STATE_FAULT;
    (void)App_SHT30_PublishQuality(now_ms);
    return;
  }

  if ((g_app_sht30_collector_report.device_state ==
       APP_SHT30_DEVICE_OFFLINE) ||
      (g_app_sht30_collector_report.consecutive_failed_rounds >=
       APP_SHT30_OFFLINE_THRESHOLD_ROUNDS))
  {
    g_app_sht30_collector_report.device_state = APP_SHT30_DEVICE_OFFLINE;
    g_app_sht30_collector_report.quality = APP_SHT30_QUALITY_OFFLINE;
    if (App_SHT30_PublishQuality(now_ms) == 0U)
    {
      return;
    }

    App_SHT30_SetWaitState(APP_SHT30_COLLECTOR_STATE_WAIT_OFFLINE_PROBE,
                           now_ms,
                           APP_SHT30_OFFLINE_PROBE_PERIOD_MS);
    return;
  }

  if (g_app_sht30_collector_report.has_valid_measurement != 0U)
  {
    g_app_sht30_collector_report.quality = APP_SHT30_QUALITY_STALE;
  }

  if (App_SHT30_PublishQuality(now_ms) == 0U)
  {
    return;
  }

  App_SHT30_SetWaitState(APP_SHT30_COLLECTOR_STATE_WAIT_SAMPLE,
                         g_app_sht30_collector_report.round_started_at_ms,
                         APP_SHT30_SAMPLE_PERIOD_MS);
}

static void App_SHT30_CompleteRoundSuccess(
  const SHT30_Measurement_t *measurement,
  uint32_t now_ms)
{
  App_DataResult_t data_result;
  App_DataValueUpdate_t updates[APP_DATA_POINT_COUNT];
  uint8_t recovered;

  recovered = (uint8_t)(g_app_sht30_collector_report.device_state ==
                        APP_SHT30_DEVICE_OFFLINE);

  updates[0].source_id = APP_DATA_SOURCE_SHT30;
  updates[0].point_id = APP_DATA_POINT_TEMPERATURE;
  updates[0].raw_value = measurement->temperature_milli_c;
  updates[0].scale = APP_DATA_SCALE_MILLI;
  updates[0].unit = APP_DATA_UNIT_DEGREE_CELSIUS;

  updates[1].source_id = APP_DATA_SOURCE_SHT30;
  updates[1].point_id = APP_DATA_POINT_HUMIDITY;
  updates[1].raw_value = (int32_t)measurement->humidity_milli_percent;
  updates[1].scale = APP_DATA_SCALE_MILLI;
  updates[1].unit = APP_DATA_UNIT_PERCENT_RH;

  data_result = App_DataService_PublishValues(updates,
                                               APP_DATA_POINT_COUNT,
                                               now_ms);
  if (data_result != APP_DATA_RESULT_OK)
  {
    App_SHT30_RecordDataServiceError(now_ms);
    return;
  }

  g_app_sht30_collector_report.temperature_raw =
    measurement->temperature_raw;
  g_app_sht30_collector_report.humidity_raw = measurement->humidity_raw;
  g_app_sht30_collector_report.temperature_milli_c =
    measurement->temperature_milli_c;
  g_app_sht30_collector_report.humidity_milli_percent =
    measurement->humidity_milli_percent;
  g_app_sht30_collector_report.last_success_at_ms = now_ms;
  g_app_sht30_collector_report.last_status =
    APP_SHT30_COLLECTOR_STATUS_OK;
  g_app_sht30_collector_report.device_state = APP_SHT30_DEVICE_ONLINE;
  g_app_sht30_collector_report.quality = APP_SHT30_QUALITY_VALID;
  g_app_sht30_collector_report.has_valid_measurement = 1U;
  g_app_sht30_collector_report.consecutive_failed_rounds = 0U;
  ++g_app_sht30_collector_report.total_round_count;
  ++g_app_sht30_collector_report.success_count;
  ++g_app_sht30_collector_report.sequence;

  if (recovered != 0U)
  {
    ++g_app_sht30_collector_report.recovery_count;
  }

  App_SHT30_SetWaitState(APP_SHT30_COLLECTOR_STATE_WAIT_SAMPLE,
                         g_app_sht30_collector_report.round_started_at_ms,
                         APP_SHT30_SAMPLE_PERIOD_MS);
}

static void App_SHT30_HandleAttemptFailure(SHT30_Status_t status,
                                           uint32_t now_ms)
{
  App_SHT30_RecordAttemptError(status, now_ms);

  if ((status != SHT30_STATUS_INVALID_ARGUMENT) &&
      (s_retries_used < APP_SHT30_MAX_RETRIES_PER_ROUND))
  {
    ++s_retries_used;
    ++g_app_sht30_collector_report.retry_count;
    App_SHT30_SetWaitState(APP_SHT30_COLLECTOR_STATE_WAIT_RETRY,
                           now_ms,
                           APP_SHT30_RETRY_DELAY_MS);
    return;
  }

  App_SHT30_CompleteRoundFailure(now_ms);
}

static void App_SHT30_StartRound(uint32_t now_ms)
{
  g_app_sht30_collector_report.round_started_at_ms = now_ms;
  s_retries_used = 0U;
  g_app_sht30_collector_report.current_attempt = 1U;
}

static void App_SHT30_StartAttempt(uint32_t now_ms)
{
  SHT30_Status_t status;

  g_app_sht30_collector_report.attempt_started_at_ms = now_ms;
  status = SHT30_StartHighRepeatabilityMeasurement(&s_sht30);
  if (status != SHT30_STATUS_OK)
  {
    App_SHT30_HandleAttemptFailure(status, now_ms);
    return;
  }

  App_SHT30_SetWaitState(APP_SHT30_COLLECTOR_STATE_WAIT_MEASUREMENT,
                         now_ms,
                         SHT30_HIGH_REPEATABILITY_WAIT_MS);
}

void App_SHT30_Collector_Init(void)
{
  SHT30_Bus_t bus;
  SHT30_Status_t status;

  g_app_sht30_collector_report.state =
    APP_SHT30_COLLECTOR_STATE_WAIT_SAMPLE;
  g_app_sht30_collector_report.device_state = APP_SHT30_DEVICE_UNKNOWN;
  g_app_sht30_collector_report.quality = APP_SHT30_QUALITY_NOT_AVAILABLE;
  g_app_sht30_collector_report.last_status =
    APP_SHT30_COLLECTOR_STATUS_NOT_RUN;
  g_app_sht30_collector_report.round_started_at_ms = 0U;
  g_app_sht30_collector_report.attempt_started_at_ms = 0U;
  g_app_sht30_collector_report.last_success_at_ms = 0U;
  g_app_sht30_collector_report.last_error_at_ms = 0U;
  g_app_sht30_collector_report.temperature_raw = 0U;
  g_app_sht30_collector_report.humidity_raw = 0U;
  g_app_sht30_collector_report.temperature_milli_c = 0;
  g_app_sht30_collector_report.humidity_milli_percent = 0U;
  g_app_sht30_collector_report.current_attempt = 0U;
  g_app_sht30_collector_report.total_round_count = 0U;
  g_app_sht30_collector_report.success_count = 0U;
  g_app_sht30_collector_report.failed_round_count = 0U;
  g_app_sht30_collector_report.retry_count = 0U;
  g_app_sht30_collector_report.consecutive_failed_rounds = 0U;
  g_app_sht30_collector_report.recovery_count = 0U;
  g_app_sht30_collector_report.invalid_argument_count = 0U;
  g_app_sht30_collector_report.nack_count = 0U;
  g_app_sht30_collector_report.timeout_count = 0U;
  g_app_sht30_collector_report.busy_count = 0U;
  g_app_sht30_collector_report.bus_error_count = 0U;
  g_app_sht30_collector_report.temperature_crc_error_count = 0U;
  g_app_sht30_collector_report.humidity_crc_error_count = 0U;
  g_app_sht30_collector_report.data_service_error_count = 0U;
  g_app_sht30_collector_report.sequence = 0U;
  g_app_sht30_collector_report.has_valid_measurement = 0U;

  s_state_started_at_ms = 0U;
  s_state_delay_ms = 0U;
  s_retries_used = 0U;

  bus.write = App_SHT30_BusWrite;
  bus.read = App_SHT30_BusRead;
  bus.context = 0;
  bus.timeout_ms = APP_SHT30_I2C_TIMEOUT_MS;

  status = SHT30_Init(&s_sht30, &bus, SHT30_ADDRESS_DEFAULT_7BIT);
  if (status != SHT30_STATUS_OK)
  {
    App_SHT30_RecordAttemptError(status, 0U);
    g_app_sht30_collector_report.state = APP_SHT30_COLLECTOR_STATE_FAULT;
    (void)App_SHT30_PublishQuality(0U);
  }
}

void App_SHT30_Collector_Process(uint32_t now_ms)
{
  SHT30_Status_t status;
  SHT30_Measurement_t measurement;

  switch (g_app_sht30_collector_report.state)
  {
    case APP_SHT30_COLLECTOR_STATE_WAIT_SAMPLE:
      if (App_SHT30_HasElapsed(now_ms,
                               s_state_started_at_ms,
                               s_state_delay_ms) == 0U)
      {
        return;
      }

      App_SHT30_StartRound(now_ms);
      App_SHT30_StartAttempt(now_ms);
      return;

    case APP_SHT30_COLLECTOR_STATE_WAIT_MEASUREMENT:
      if (App_SHT30_HasElapsed(now_ms,
                               s_state_started_at_ms,
                               s_state_delay_ms) == 0U)
      {
        return;
      }

      status = SHT30_ReadMeasurement(&s_sht30, &measurement);
      if (status != SHT30_STATUS_OK)
      {
        App_SHT30_HandleAttemptFailure(status, now_ms);
        return;
      }

      App_SHT30_CompleteRoundSuccess(&measurement, now_ms);
      return;

    case APP_SHT30_COLLECTOR_STATE_WAIT_RETRY:
      if (App_SHT30_HasElapsed(now_ms,
                               s_state_started_at_ms,
                               s_state_delay_ms) == 0U)
      {
        return;
      }

      g_app_sht30_collector_report.current_attempt = s_retries_used + 1U;
      App_SHT30_StartAttempt(now_ms);
      return;

    case APP_SHT30_COLLECTOR_STATE_WAIT_OFFLINE_PROBE:
      if (App_SHT30_HasElapsed(now_ms,
                               s_state_started_at_ms,
                               s_state_delay_ms) == 0U)
      {
        return;
      }

      App_SHT30_StartRound(now_ms);
      App_SHT30_StartAttempt(now_ms);
      return;

    case APP_SHT30_COLLECTOR_STATE_FAULT:
    default:
      return;
  }
}
