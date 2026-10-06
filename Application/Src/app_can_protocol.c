#include "app_can_protocol.h"

/* User-authored learning logic, syntax corrected during review. */
uint8_t CAN_RequestIsValid(uint32_t id, uint8_t is_extended,
    uint8_t is_remote, uint8_t dlc, const uint8_t *data)
{
  if ((id == 0x123U) && (is_extended == 0U) && (is_remote == 0U) &&
      (dlc == 1U) && (data != 0) && (data[0] == 0x01U))
    return 1U;
  return 0U;
}

uint8_t CAN_EncodeResponse(uint8_t quality, int16_t temperature,
    uint16_t humidity, uint8_t *data, uint32_t data_capacity)
{
  uint16_t temperature_bits;
  if ((data == 0) || (data_capacity < 5U)) return 0U;
  /* Integration guard: the user's exercise assumed these were valid.
   * Unknown status and >100% humidity must not look like valid readings.
   */
  if ((quality > 3U) || ((quality != 0U) && (humidity > 10000U)))
    return 0U;
  data[0] = quality;
  if (quality == 0U) {
    data[1] = 0U; data[2] = 0U; data[3] = 0U; data[4] = 0U;
    return 1U;
  }
  temperature_bits = (uint16_t)temperature;
  data[1] = (uint8_t)(temperature_bits >> 8);
  data[2] = (uint8_t)(temperature_bits % 256U);
  data[3] = (uint8_t)(humidity >> 8);
  data[4] = (uint8_t)(humidity % 256U);
  return 1U;
}
