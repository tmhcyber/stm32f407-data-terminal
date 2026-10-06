#include "sht30.h"

#define SHT30_SINGLE_SHOT_HIGH_NO_STRETCH_MSB  0x24U
#define SHT30_SINGLE_SHOT_HIGH_NO_STRETCH_LSB  0x00U
#define SHT30_COMMAND_LENGTH                   2U
#define SHT30_CRC_INITIAL_VALUE                0xFFU
#define SHT30_CRC_POLYNOMIAL                   0x31U
#define SHT30_RAW_DENOMINATOR                  65535ULL
#define SHT30_ROUNDING_OFFSET                  32767ULL

static uint8_t SHT30_IsValidAddress(uint8_t address_7bit)
{
  return (uint8_t)((address_7bit == SHT30_ADDRESS_DEFAULT_7BIT) ||
                   (address_7bit == SHT30_ADDRESS_ALTERNATE_7BIT));
}

static SHT30_Status_t SHT30_MapBusStatus(SHT30_BusStatus_t status)
{
  switch (status)
  {
    case SHT30_BUS_STATUS_OK:
      return SHT30_STATUS_OK;

    case SHT30_BUS_STATUS_INVALID_ARGUMENT:
      return SHT30_STATUS_INVALID_ARGUMENT;

    case SHT30_BUS_STATUS_NACK:
      return SHT30_STATUS_BUS_NACK;

    case SHT30_BUS_STATUS_TIMEOUT:
      return SHT30_STATUS_BUS_TIMEOUT;

    case SHT30_BUS_STATUS_BUSY:
      return SHT30_STATUS_BUS_BUSY;

    case SHT30_BUS_STATUS_ERROR:
    default:
      return SHT30_STATUS_BUS_ERROR;
  }
}

static uint8_t SHT30_CalculateCrc(const uint8_t *data, uint16_t length)
{
  uint8_t crc;
  uint16_t byte_index;
  uint8_t bit_index;

  crc = SHT30_CRC_INITIAL_VALUE;

  for (byte_index = 0U; byte_index < length; ++byte_index)
  {
    crc ^= data[byte_index];

    for (bit_index = 0U; bit_index < 8U; ++bit_index)
    {
      if ((crc & 0x80U) != 0U)
      {
        crc = (uint8_t)((crc << 1U) ^ SHT30_CRC_POLYNOMIAL);
      }
      else
      {
        crc = (uint8_t)(crc << 1U);
      }
    }
  }

  return crc;
}

SHT30_Status_t SHT30_Init(SHT30_t *device,
                          const SHT30_Bus_t *bus,
                          uint8_t address_7bit)
{
  if ((device == 0) ||
      (bus == 0) ||
      (bus->write == 0) ||
      (bus->read == 0) ||
      (bus->timeout_ms == 0U) ||
      (SHT30_IsValidAddress(address_7bit) == 0U))
  {
    return SHT30_STATUS_INVALID_ARGUMENT;
  }

  device->bus = *bus;
  device->address_7bit = address_7bit;

  return SHT30_STATUS_OK;
}

SHT30_Status_t SHT30_StartHighRepeatabilityMeasurement(SHT30_t *device)
{
  static const uint8_t command[SHT30_COMMAND_LENGTH] =
  {
    SHT30_SINGLE_SHOT_HIGH_NO_STRETCH_MSB,
    SHT30_SINGLE_SHOT_HIGH_NO_STRETCH_LSB
  };
  SHT30_BusStatus_t bus_status;

  if ((device == 0) || (device->bus.write == 0))
  {
    return SHT30_STATUS_INVALID_ARGUMENT;
  }

  bus_status = device->bus.write(device->bus.context,
                                 device->address_7bit,
                                 command,
                                 SHT30_COMMAND_LENGTH,
                                 device->bus.timeout_ms);

  return SHT30_MapBusStatus(bus_status);
}

SHT30_Status_t SHT30_DecodeMeasurement(
  const uint8_t response[SHT30_MEASUREMENT_RESPONSE_LENGTH],
  SHT30_Measurement_t *measurement)
{
  uint16_t temperature_raw;
  uint16_t humidity_raw;
  uint64_t scaled_value;

  if ((response == 0) || (measurement == 0))
  {
    return SHT30_STATUS_INVALID_ARGUMENT;
  }

  if (SHT30_CalculateCrc(&response[0], 2U) != response[2])
  {
    return SHT30_STATUS_TEMPERATURE_CRC_ERROR;
  }

  if (SHT30_CalculateCrc(&response[3], 2U) != response[5])
  {
    return SHT30_STATUS_HUMIDITY_CRC_ERROR;
  }

  temperature_raw = (uint16_t)(((uint16_t)response[0] << 8U) |
                               (uint16_t)response[1]);
  humidity_raw = (uint16_t)(((uint16_t)response[3] << 8U) |
                            (uint16_t)response[4]);

  measurement->temperature_raw = temperature_raw;
  measurement->humidity_raw = humidity_raw;

  scaled_value = ((175000ULL * (uint64_t)temperature_raw) +
                  SHT30_ROUNDING_OFFSET) /
                 SHT30_RAW_DENOMINATOR;
  measurement->temperature_milli_c = (int32_t)scaled_value - 45000;

  scaled_value = ((100000ULL * (uint64_t)humidity_raw) +
                  SHT30_ROUNDING_OFFSET) /
                 SHT30_RAW_DENOMINATOR;
  measurement->humidity_milli_percent = (uint32_t)scaled_value;

  return SHT30_STATUS_OK;
}

SHT30_Status_t SHT30_ReadMeasurement(SHT30_t *device,
                                     SHT30_Measurement_t *measurement)
{
  uint8_t response[SHT30_MEASUREMENT_RESPONSE_LENGTH];
  SHT30_BusStatus_t bus_status;

  if ((device == 0) ||
      (measurement == 0) ||
      (device->bus.read == 0))
  {
    return SHT30_STATUS_INVALID_ARGUMENT;
  }

  bus_status = device->bus.read(device->bus.context,
                                device->address_7bit,
                                response,
                                SHT30_MEASUREMENT_RESPONSE_LENGTH,
                                device->bus.timeout_ms);
  if (bus_status != SHT30_BUS_STATUS_OK)
  {
    return SHT30_MapBusStatus(bus_status);
  }

  return SHT30_DecodeMeasurement(response, measurement);
}
