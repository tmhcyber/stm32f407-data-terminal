#include "bsp_i2c.h"

#include "stm32f4xx_hal.h"

#define BSP_I2C_7BIT_ADDRESS_MIN  0x08U
#define BSP_I2C_7BIT_ADDRESS_MAX  0x77U

static I2C_HandleTypeDef s_i2c1_handle;

static uint8_t BSP_I2C_IsValidAddress(uint8_t address_7bit)
{
  return (uint8_t)((address_7bit >= BSP_I2C_7BIT_ADDRESS_MIN) &&
                   (address_7bit <= BSP_I2C_7BIT_ADDRESS_MAX));
}

static BSP_I2C_Status_t BSP_I2C_MapStatus(HAL_StatusTypeDef hal_status)
{
  uint32_t hal_error;

  if (hal_status == HAL_OK)
  {
    return BSP_I2C_STATUS_OK;
  }

  if (hal_status == HAL_BUSY)
  {
    return BSP_I2C_STATUS_BUSY;
  }

  hal_error = HAL_I2C_GetError(&s_i2c1_handle);

  if ((hal_status == HAL_TIMEOUT) ||
      ((hal_error & HAL_I2C_ERROR_TIMEOUT) != 0U))
  {
    return BSP_I2C_STATUS_TIMEOUT;
  }

  if ((hal_error & HAL_I2C_ERROR_AF) != 0U)
  {
    return BSP_I2C_STATUS_NACK;
  }

  return BSP_I2C_STATUS_BUS_ERROR;
}

static BSP_I2C_Status_t BSP_I2C_MapReadyStatus(
  HAL_StatusTypeDef hal_status)
{
  /*
   * HAL_I2C_IsDeviceReady() clears the AF flag without preserving it in
   * ErrorCode when all address trials are exhausted. For this API only,
   * HAL_ERROR with no recorded HAL error therefore means address NACK.
   */
  if ((hal_status == HAL_ERROR) &&
      (HAL_I2C_GetError(&s_i2c1_handle) == HAL_I2C_ERROR_NONE))
  {
    return BSP_I2C_STATUS_NACK;
  }

  return BSP_I2C_MapStatus(hal_status);
}

BSP_I2C_Status_t BSP_I2C1_Init(void)
{
  s_i2c1_handle.Instance = I2C1;
  s_i2c1_handle.Init.ClockSpeed = 100000U;
  s_i2c1_handle.Init.DutyCycle = I2C_DUTYCYCLE_2;
  s_i2c1_handle.Init.OwnAddress1 = 0U;
  s_i2c1_handle.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  s_i2c1_handle.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  s_i2c1_handle.Init.OwnAddress2 = 0U;
  s_i2c1_handle.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  s_i2c1_handle.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;

  return BSP_I2C_MapStatus(HAL_I2C_Init(&s_i2c1_handle));
}

BSP_I2C_Status_t BSP_I2C1_IsReady(uint8_t address_7bit,
                                  uint32_t timeout_ms)
{
  HAL_StatusTypeDef hal_status;

  if ((BSP_I2C_IsValidAddress(address_7bit) == 0U) ||
      (timeout_ms == 0U))
  {
    return BSP_I2C_STATUS_INVALID_ARGUMENT;
  }

  hal_status = HAL_I2C_IsDeviceReady(&s_i2c1_handle,
                                     (uint16_t)address_7bit << 1U,
                                     1U,
                                     timeout_ms);

  return BSP_I2C_MapReadyStatus(hal_status);
}

BSP_I2C_Status_t BSP_I2C1_Write(uint8_t address_7bit,
                                const uint8_t *data,
                                uint16_t length,
                                uint32_t timeout_ms)
{
  HAL_StatusTypeDef hal_status;

  if ((BSP_I2C_IsValidAddress(address_7bit) == 0U) ||
      (data == 0) ||
      (length == 0U) ||
      (timeout_ms == 0U))
  {
    return BSP_I2C_STATUS_INVALID_ARGUMENT;
  }

  hal_status = HAL_I2C_Master_Transmit(&s_i2c1_handle,
                                       (uint16_t)address_7bit << 1U,
                                       (uint8_t *)data,
                                       length,
                                       timeout_ms);

  return BSP_I2C_MapStatus(hal_status);
}

BSP_I2C_Status_t BSP_I2C1_Read(uint8_t address_7bit,
                               uint8_t *data,
                               uint16_t length,
                               uint32_t timeout_ms)
{
  HAL_StatusTypeDef hal_status;

  if ((BSP_I2C_IsValidAddress(address_7bit) == 0U) ||
      (data == 0) ||
      (length == 0U) ||
      (timeout_ms == 0U))
  {
    return BSP_I2C_STATUS_INVALID_ARGUMENT;
  }

  hal_status = HAL_I2C_Master_Receive(&s_i2c1_handle,
                                      (uint16_t)address_7bit << 1U,
                                      data,
                                      length,
                                      timeout_ms);

  return BSP_I2C_MapStatus(hal_status);
}

void HAL_I2C_MspInit(I2C_HandleTypeDef *i2c_handle)
{
  GPIO_InitTypeDef gpio_init;

  if (i2c_handle->Instance != I2C1)
  {
    return;
  }

  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_I2C1_CLK_ENABLE();

  gpio_init.Pin = GPIO_PIN_8 | GPIO_PIN_9;
  gpio_init.Mode = GPIO_MODE_AF_OD;
  gpio_init.Pull = GPIO_NOPULL;
  gpio_init.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio_init.Alternate = GPIO_AF4_I2C1;
  HAL_GPIO_Init(GPIOB, &gpio_init);
}
