#ifndef APP_MODBUS_REGISTERS_H
#define APP_MODBUS_REGISTERS_H

#include <stdint.h>
#include "mb_rtu.h"

#define APP_MODBUS_REGISTER_COUNT 3U
/* One consistent business snapshot, in the collector's original milli units.
 * quality: 0 unavailable (no successful sample), 1 valid, 2 stale, 3 offline.
 * Capturing/validating DataService belongs to the service adapter.
 */
typedef struct
{
  int32_t temperature_milli;
  int32_t humidity_milli;
  uint16_t quality;
} App_ModbusSample_t;

/* Pure register mapping; implemented by Codex at the user's request.
 * Protocol addresses 0/1/2 = temperature/humidity/quality.
 * Success: output quantity host-order uint16_t registers; return MB_EX_NONE.
 * Temperature: signed centi-C encoded in uint16_t two's complement.
 * Humidity: unsigned centi-%RH. milli / 10 truncates toward zero (C99).
 * quantity == 0 or > 125 -> MB_EX_ILLEGAL_VALUE;
 * any address outside [0, 3) -> MB_EX_ILLEGAL_ADDRESS (no wrapping sum);
 * null pointer, insufficient output, quality > 3, invalid measurement
 * range -> MB_EX_DEVICE_FAILURE. Validate before writing any output.
 * For quality 0 return zero temperature/humidity; otherwise preserve the
 * last sample, including qualities 2/3. Valid milli ranges match CAN:
 * temperature [-327680, 327670], humidity [0, 100000].
 * Modbus byte order and CRC are handled by the middleware, not here.
 */
MB_Exception_t App_ModbusRegisters_Read(const App_ModbusSample_t *sample,
  uint16_t start, uint16_t quantity, uint16_t *output, uint16_t capacity);

#endif
