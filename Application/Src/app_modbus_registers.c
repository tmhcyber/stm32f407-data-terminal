#include "app_modbus_registers.h"

MB_Exception_t App_ModbusRegisters_Read(const App_ModbusSample_t *sample,
  uint16_t start, uint16_t quantity, uint16_t *output, uint16_t capacity)
{
  uint16_t values[APP_MODBUS_REGISTER_COUNT];
  uint16_t i;
  if ((quantity == 0U) || (quantity > MB_RTU_MAX_READ_REGISTERS))
    return MB_EX_ILLEGAL_VALUE;
  /* Subtract only after validating start: no 16-bit start+quantity wrap. */
  if ((start >= APP_MODBUS_REGISTER_COUNT) ||
      (quantity > (APP_MODBUS_REGISTER_COUNT - start)))
    return MB_EX_ILLEGAL_ADDRESS;
  if ((sample == 0) || (output == 0) || (capacity < quantity) ||
      (sample->quality > 3U)) return MB_EX_DEVICE_FAILURE;
  values[0] = 0U;
  values[1] = 0U;
  values[2] = sample->quality;
  if (sample->quality != 0U)
  {
    if ((sample->temperature_milli < -327680) ||
        (sample->temperature_milli > 327670) ||
        (sample->humidity_milli < 0) || (sample->humidity_milli > 100000))
      return MB_EX_DEVICE_FAILURE;
    values[0] = (uint16_t)(int16_t)(sample->temperature_milli / 10);
    values[1] = (uint16_t)(sample->humidity_milli / 10);
  }
  for (i = 0U; i < quantity; ++i) output[i] = values[start + i];
  return MB_EX_NONE;
}
