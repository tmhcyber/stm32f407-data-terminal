#ifndef APP_RS485_CONFIG_H
#define APP_RS485_CONFIG_H

/* 0: the established 115200 8N1 PING/PONG experiment.
 * 1: 115200 8E1 Modbus RTU slave. Exactly one USART2 owner runs.
 * Keep the existing demo default until the new hardware path has been
 * validated. Override in the compiler; private builds cover both modes.
 */
#ifndef APP_RS485_MODBUS_ENABLED
#define APP_RS485_MODBUS_ENABLED 0
#endif
#if (APP_RS485_MODBUS_ENABLED != 0) && (APP_RS485_MODBUS_ENABLED != 1)
#error "APP_RS485_MODBUS_ENABLED must be 0 or 1"
#endif

#endif
