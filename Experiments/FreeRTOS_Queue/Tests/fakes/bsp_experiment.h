#include <stdint.h>
typedef enum { HAL_OK, HAL_ERROR } HAL_StatusTypeDef;
HAL_StatusTypeDef BSP_Experiment_Write(const char *text, uint16_t length);
void Experiment_Panic(uint32_t reason);
