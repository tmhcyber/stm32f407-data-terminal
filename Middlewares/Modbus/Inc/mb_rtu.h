#ifndef MB_RTU_H
#define MB_RTU_H

#include <stdint.h>

#define MB_RTU_MAX_ADU 256U
#define MB_RTU_MAX_READ_REGISTERS 125U
#define MB_RTU_T15_US 750U
#define MB_RTU_T35_US 1750U
/* 11 UART bits / 115200 baud, rounded up to the 1 us clock resolution.
 * RX events mark character completion. Subtract one character to estimate
 * wire silence; wait an extra character before finalizing to allow an
 * in-flight character to complete. See docs/MODBUS_RTU.md for limits.
 */
#define MB_RTU_CHARACTER_US 96U
#define MB_RTU_RX_END_US (MB_RTU_T35_US + MB_RTU_CHARACTER_US)

typedef enum
{
  MB_EX_NONE = 0,
  MB_EX_ILLEGAL_FUNCTION = 1,
  MB_EX_ILLEGAL_ADDRESS = 2,
  MB_EX_ILLEGAL_VALUE = 3,
  MB_EX_DEVICE_FAILURE = 4
} MB_Exception_t;

typedef MB_Exception_t (*MB_ReadInput_t)(void *context, uint16_t start,
  uint16_t quantity, uint16_t *registers, uint16_t capacity);

uint16_t MB_Crc16(const uint8_t *data, uint16_t length);
/* Entire, gap-delimited ADU in; response bytes out. 0 = silent discard or
 * insufficient response capacity. Callback receives at most 125 registers.
 * Caller supplies distinct request/response storage and valid capacities.
 */
uint16_t MB_RTU_Handle(const uint8_t *request, uint16_t length,
  uint8_t slave, MB_ReadInput_t read_input, void *context,
  uint8_t *response, uint16_t capacity);

typedef struct
{
  uint8_t bytes[MB_RTU_MAX_ADU];
  uint16_t length;
  uint32_t last_byte_us;
  uint8_t active;
  uint8_t discard;
  uint32_t gap_errors;
  uint32_t overlengths;
  uint32_t superseded;
} MB_RTU_Receiver_t;

/* Initialize/drop an untrusted stream; require a quiet gap to resynchronize.
 * All calls belong to the same main-loop context, not concurrently to IRQ.
 */
void MB_RTU_RxReset(MB_RTU_Receiver_t *rx, uint32_t now_us);
void MB_RTU_RxByte(MB_RTU_Receiver_t *rx, uint8_t byte, uint32_t completed_us);
/* Call only after draining a bounded snapshot of the receive queue, with a
 * current (not loop-start) timestamp. Returned storage valid until next feed.
 * A newer queued frame supersedes an unanswered old request; never answer
 * a historical request while a new one is being received.
 */
uint16_t MB_RTU_RxTake(MB_RTU_Receiver_t *rx, uint32_t now_us);

#endif
