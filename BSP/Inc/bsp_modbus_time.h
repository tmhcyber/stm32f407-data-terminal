#ifndef BSP_MODBUS_TIME_H
#define BSP_MODBUS_TIME_H
#include <stdint.h>

/* TIM2: free-running 32-bit 1 MHz counter, no timer IRQ or GPIO.
 * Initialize after final system/APB clock setup, before USART2 RX.
 * Differences use unsigned subtraction; wraps about every 71 min.
 */
uint8_t BSP_ModbusTime_Init(void);
uint32_t BSP_ModbusTime_Micros(void);
#endif
