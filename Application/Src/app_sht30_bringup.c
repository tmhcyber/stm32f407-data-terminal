#include "app_sht30_bringup.h"

#include "bsp_i2c.h"
#include "sht30.h"

#define APP_SHT30_I2C_TIMEOUT_MS  20U

static SHT30_t s_sht30;

volatile App_SHT30_BringupReport_t g_app_sht30_bringup_report =
{
  APP_SHT30_BRINGUP_STATE_IDLE,
  APP_SHT30_STATUS_NOT_RUN,
  0U,
  0U,
  0U,
  0U,
  0,
  0U,
  0U
};

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

static App_SHT30_Status_t App_SHT30_MapStatus(SHT30_Status_t status)
{
  switch (status)
  {
    case SHT30_STATUS_OK:
      return APP_SHT30_STATUS_OK;

    case SHT30_STATUS_INVALID_ARGUMENT:
      return APP_SHT30_STATUS_INVALID_ARGUMENT;

    case SHT30_STATUS_BUS_NACK:
      return APP_SHT30_STATUS_NACK;

    case SHT30_STATUS_BUS_TIMEOUT:
      return APP_SHT30_STATUS_TIMEOUT;

    case SHT30_STATUS_BUS_BUSY:
      return APP_SHT30_STATUS_BUSY;

    case SHT30_STATUS_TEMPERATURE_CRC_ERROR:
      return APP_SHT30_STATUS_TEMPERATURE_CRC_ERROR;

    case SHT30_STATUS_HUMIDITY_CRC_ERROR:
      return APP_SHT30_STATUS_HUMIDITY_CRC_ERROR;

    case SHT30_STATUS_BUS_ERROR:
    default:
      return APP_SHT30_STATUS_BUS_ERROR;
  }
}

static void App_SHT30_CompleteWithError(SHT30_Status_t status)
{
  g_app_sht30_bringup_report.status = App_SHT30_MapStatus(status);
  g_app_sht30_bringup_report.state = APP_SHT30_BRINGUP_STATE_ERROR;
  g_app_sht30_bringup_report.completed = 1U;
}

void App_SHT30_Bringup_Init(void)
{
  SHT30_Bus_t bus;
  SHT30_Status_t status;

  g_app_sht30_bringup_report.state = APP_SHT30_BRINGUP_STATE_IDLE;
  g_app_sht30_bringup_report.status = APP_SHT30_STATUS_NOT_RUN;
  g_app_sht30_bringup_report.started_at_ms = 0U;
  g_app_sht30_bringup_report.ready_at_ms = 0U;
  g_app_sht30_bringup_report.temperature_raw = 0U;
  g_app_sht30_bringup_report.humidity_raw = 0U;
  g_app_sht30_bringup_report.temperature_milli_c = 0;
  g_app_sht30_bringup_report.humidity_milli_percent = 0U;
  g_app_sht30_bringup_report.completed = 0U;

  bus.write = App_SHT30_BusWrite;
  bus.read = App_SHT30_BusRead;
  bus.context = 0;
  bus.timeout_ms = APP_SHT30_I2C_TIMEOUT_MS;

  status = SHT30_Init(&s_sht30, &bus, SHT30_ADDRESS_DEFAULT_7BIT);
  if (status != SHT30_STATUS_OK)
  {
    App_SHT30_CompleteWithError(status);
  }
}

void App_SHT30_Bringup_Process(uint32_t now_ms)
{
  SHT30_Status_t status;
  SHT30_Measurement_t measurement;

  if (g_app_sht30_bringup_report.completed != 0U)
  {
    return;
  }

  switch (g_app_sht30_bringup_report.state)
  {
    case APP_SHT30_BRINGUP_STATE_IDLE:
      status = SHT30_StartHighRepeatabilityMeasurement(&s_sht30);
      if (status != SHT30_STATUS_OK)
      {
        App_SHT30_CompleteWithError(status);
        return;
      }

      g_app_sht30_bringup_report.state =
        APP_SHT30_BRINGUP_STATE_MEASUREMENT_STARTED;
      return;

    case APP_SHT30_BRINGUP_STATE_MEASUREMENT_STARTED:
      g_app_sht30_bringup_report.started_at_ms = now_ms;
      g_app_sht30_bringup_report.ready_at_ms =
        now_ms + SHT30_HIGH_REPEATABILITY_WAIT_MS;
      g_app_sht30_bringup_report.state =
        APP_SHT30_BRINGUP_STATE_WAITING;
      return;

    case APP_SHT30_BRINGUP_STATE_WAITING:
      if ((uint32_t)(now_ms -
                     g_app_sht30_bringup_report.started_at_ms) <
          SHT30_HIGH_REPEATABILITY_WAIT_MS)
      {
        return;
      }

      status = SHT30_ReadMeasurement(&s_sht30, &measurement);
      if (status != SHT30_STATUS_OK)
      {
        App_SHT30_CompleteWithError(status);
        return;
      }

      g_app_sht30_bringup_report.temperature_raw =
        measurement.temperature_raw;
      g_app_sht30_bringup_report.humidity_raw =
        measurement.humidity_raw;
      g_app_sht30_bringup_report.temperature_milli_c =
        measurement.temperature_milli_c;
      g_app_sht30_bringup_report.humidity_milli_percent =
        measurement.humidity_milli_percent;
      g_app_sht30_bringup_report.status = APP_SHT30_STATUS_OK;
      g_app_sht30_bringup_report.state =
        APP_SHT30_BRINGUP_STATE_COMPLETE;
      g_app_sht30_bringup_report.completed = 1U;
      return;

    case APP_SHT30_BRINGUP_STATE_COMPLETE:
    case APP_SHT30_BRINGUP_STATE_ERROR:
      return;

    default:
      App_SHT30_CompleteWithError(SHT30_STATUS_INVALID_ARGUMENT);
      return;
  }
}
