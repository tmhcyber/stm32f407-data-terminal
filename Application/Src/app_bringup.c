#include "app_bringup.h"

#include "bsp_i2c.h"

#define APP_BRINGUP_BOARD_EEPROM_ADDRESS_7BIT  0x50U
#define APP_BRINGUP_SHT30_ADDRESS_7BIT         0x44U
#define APP_BRINGUP_I2C_TIMEOUT_MS             20U

volatile App_BringupReport_t g_app_bringup_report =
{
  APP_BRINGUP_PROBE_NOT_RUN,
  APP_BRINGUP_PROBE_NOT_RUN,
  APP_BRINGUP_PROBE_NOT_RUN,
  0U
};

static App_BringupProbeStatus_t App_Bringup_MapI2CStatus(
  BSP_I2C_Status_t status)
{
  switch (status)
  {
    case BSP_I2C_STATUS_OK:
      return APP_BRINGUP_PROBE_OK;

    case BSP_I2C_STATUS_NACK:
      return APP_BRINGUP_PROBE_NACK;

    case BSP_I2C_STATUS_TIMEOUT:
      return APP_BRINGUP_PROBE_TIMEOUT;

    case BSP_I2C_STATUS_BUSY:
      return APP_BRINGUP_PROBE_BUSY;

    case BSP_I2C_STATUS_BUS_ERROR:
      return APP_BRINGUP_PROBE_BUS_ERROR;

    case BSP_I2C_STATUS_INVALID_ARGUMENT:
    default:
      return APP_BRINGUP_PROBE_INVALID_ARGUMENT;
  }
}

void App_Bringup_RunOnce(void)
{
  BSP_I2C_Status_t status;

  g_app_bringup_report.i2c1_init = APP_BRINGUP_PROBE_NOT_RUN;
  g_app_bringup_report.board_eeprom_0x50 = APP_BRINGUP_PROBE_NOT_RUN;
  g_app_bringup_report.sht30_0x44 = APP_BRINGUP_PROBE_NOT_RUN;
  g_app_bringup_report.completed = 0U;

  status = BSP_I2C1_Init();
  g_app_bringup_report.i2c1_init = App_Bringup_MapI2CStatus(status);

  if (status != BSP_I2C_STATUS_OK)
  {
    g_app_bringup_report.completed = 1U;
    return;
  }

  status = BSP_I2C1_IsReady(APP_BRINGUP_BOARD_EEPROM_ADDRESS_7BIT,
                            APP_BRINGUP_I2C_TIMEOUT_MS);
  g_app_bringup_report.board_eeprom_0x50 =
    App_Bringup_MapI2CStatus(status);

  status = BSP_I2C1_IsReady(APP_BRINGUP_SHT30_ADDRESS_7BIT,
                            APP_BRINGUP_I2C_TIMEOUT_MS);
  g_app_bringup_report.sht30_0x44 =
    App_Bringup_MapI2CStatus(status);

  g_app_bringup_report.completed = 1U;
}
