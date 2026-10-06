#ifndef TEST_CAN_QUEUE_HAL_H
#define TEST_CAN_QUEUE_HAL_H
#include <stdint.h>
uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t value);
void __DMB(void);
#endif
