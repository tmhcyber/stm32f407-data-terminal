#include "mb_rtu.h"

uint16_t MB_Crc16(const uint8_t *data, uint16_t length)
{
  uint16_t crc = 0xFFFFU;
  uint16_t i;
  uint8_t bit;
  for (i = 0U; i < length; ++i)
  {
    crc ^= data[i];
    for (bit = 0U; bit < 8U; ++bit)
      crc = (uint16_t)((crc & 1U) ? ((crc >> 1U) ^ 0xA001U) : (crc >> 1U));
  }
  return crc;
}

static uint16_t Finish(uint8_t *response, uint16_t length)
{
  uint16_t crc = MB_Crc16(response, length);
  response[length] = (uint8_t)crc;
  response[length + 1U] = (uint8_t)(crc >> 8U);
  return (uint16_t)(length + 2U);
}

uint16_t MB_RTU_Handle(const uint8_t *request, uint16_t length,
  uint8_t slave, MB_ReadInput_t read_input, void *context,
  uint8_t *response, uint16_t capacity)
{
  uint16_t registers[MB_RTU_MAX_READ_REGISTERS];
  uint16_t start, quantity, crc, i;
  MB_Exception_t exception = MB_EX_NONE;
  if ((request == 0) || (response == 0) || (length < 4U) ||
      (length > MB_RTU_MAX_ADU) || (capacity < 5U) ||
      (slave == 0U) || (slave > 247U) || (request[0] != slave)) return 0U;
  crc = MB_Crc16(request, (uint16_t)(length - 2U));
  if ((request[length - 2U] != (uint8_t)crc) ||
      (request[length - 1U] != (uint8_t)(crc >> 8U))) return 0U;

  if (request[1] != 4U) exception = MB_EX_ILLEGAL_FUNCTION;
  else if (length != 8U) return 0U; /* malformed PDU, not a semantic exception */
  else
  {
    start = (uint16_t)(((uint16_t)request[2] << 8U) | request[3]);
    quantity = (uint16_t)(((uint16_t)request[4] << 8U) | request[5]);
    if ((quantity == 0U) || (quantity > MB_RTU_MAX_READ_REGISTERS))
      exception = MB_EX_ILLEGAL_VALUE;
    else if (read_input == 0) exception = MB_EX_DEVICE_FAILURE;
    else
    {
      /* Initialize to avoid leaking stack bytes if an adapter is incomplete.
       * Success still requires the adapter to implement its full contract.
       */
      for (i = 0U; i < quantity; ++i) registers[i] = 0U;
      exception = read_input(context, start, quantity, registers,
                            MB_RTU_MAX_READ_REGISTERS);
      if ((unsigned)exception > MB_EX_DEVICE_FAILURE)
        exception = MB_EX_DEVICE_FAILURE;
      if (exception == MB_EX_NONE)
      {
        if (capacity < (uint16_t)(5U + 2U * quantity)) return 0U;
        response[0] = slave;
        response[1] = 4U;
        response[2] = (uint8_t)(2U * quantity);
        for (i = 0U; i < quantity; ++i)
        {
          response[3U + 2U * i] = (uint8_t)(registers[i] >> 8U);
          response[4U + 2U * i] = (uint8_t)registers[i];
        }
        return Finish(response, (uint16_t)(3U + 2U * quantity));
      }
    }
  }
  response[0] = slave;
  response[1] = (uint8_t)(request[1] | 0x80U);
  response[2] = (uint8_t)exception;
  return Finish(response, 3U);
}

void MB_RTU_RxReset(MB_RTU_Receiver_t *rx, uint32_t now_us)
{
  rx->length = 0U;
  rx->active = 1U;
  rx->discard = 1U;
  rx->last_byte_us = now_us;
}

void MB_RTU_RxByte(MB_RTU_Receiver_t *rx, uint8_t byte, uint32_t completed_us)
{
  uint32_t elapsed = (uint32_t)(completed_us - rx->last_byte_us);
  if ((rx->active != 0U) && (elapsed >= MB_RTU_RX_END_US))
  {
    if ((rx->length != 0U) && (rx->discard == 0U)) ++rx->superseded;
    rx->length = 0U;
    rx->discard = 0U;
  }
  else if ((rx->active != 0U) && (rx->length != 0U) &&
           (elapsed > (MB_RTU_T15_US + MB_RTU_CHARACTER_US)))
  {
    ++rx->gap_errors;
    rx->discard = 1U;
    rx->length = 0U;
  }
  rx->active = 1U;
  rx->last_byte_us = completed_us;
  if (rx->discard != 0U) return;
  if (rx->length == MB_RTU_MAX_ADU)
  {
    ++rx->overlengths;
    rx->length = 0U;
    rx->discard = 1U;
    return;
  }
  rx->bytes[rx->length++] = byte;
}

uint16_t MB_RTU_RxTake(MB_RTU_Receiver_t *rx, uint32_t now_us)
{
  uint16_t length;
  uint32_t elapsed = (uint32_t)(now_us - rx->last_byte_us);
  /* A producer timestamp newer than this poll must never look like silence. */
  if ((rx->active == 0U) || (elapsed >= 0x80000000UL) ||
      (elapsed < MB_RTU_RX_END_US)) return 0U;
  length = rx->discard ? 0U : rx->length;
  rx->active = 0U;
  rx->discard = 0U;
  rx->length = 0U;
  return length;
}
