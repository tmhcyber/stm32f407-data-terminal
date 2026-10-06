#ifndef TEST_CAN_HAL_H
#define TEST_CAN_HAL_H
#include <stdint.h>
/* Minimal HAL boundary double. Does not emulate physical CAN or W1C registers. */
typedef struct { uint32_t MCR, TSR, ESR, MSR, IER, RF0R; } CAN_TypeDef;
#define CAN2_RX0_IRQn 64
#define CAN_IT_RX_FIFO0_MSG_PENDING 2U
#define CAN_IT_RX_FIFO0_OVERRUN 8U
#define CAN_FLAG_FOV0 16U
#define __HAL_CAN_GET_FLAG(H, F) ((H)->Instance->RF0R & (F))
#define __HAL_CAN_CLEAR_FLAG(H, F) ((H)->Instance->RF0R &= ~(F))
uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t value);
void __DMB(void);
void __DSB(void);
void __ISB(void);
void HAL_NVIC_DisableIRQ(int irq);
void HAL_NVIC_EnableIRQ(int irq);
void HAL_NVIC_ClearPendingIRQ(int irq);
void HAL_NVIC_SetPriority(int irq, uint32_t priority, uint32_t subpriority);
#define CAN_MSR_INAK 1U
#define CAN_MSR_SLAK 2U
#define CLEAR_BIT(REG, BIT) ((REG) &= ~(BIT))
void test_can_force_reset(void);
#define __HAL_RCC_CAN2_FORCE_RESET() test_can_force_reset()
#define __HAL_RCC_CAN2_RELEASE_RESET() ((void)0)
#define CAN_MCR_INRQ 1U
#define CAN_MCR_SLEEP 2U
#define SET_BIT(REG, BIT) ((REG) |= (BIT))
extern CAN_TypeDef test_can2;
#define CAN2 (&test_can2)
extern uint32_t test_clocks;
#define __HAL_RCC_CAN1_CLK_ENABLE() (test_clocks |= 1U)
#define __HAL_RCC_CAN2_CLK_ENABLE() (test_clocks |= 2U)
#define __HAL_RCC_GPIOB_CLK_ENABLE() (test_clocks |= 4U)
#define GPIOB ((void *)0)
#define GPIO_PIN_12 0x1000U
#define GPIO_PIN_13 0x2000U
#define GPIO_SPEED_FREQ_HIGH 2U
#define CAN_MODE_NORMAL 0U
#define CAN_BS1_7TQ 0x00060000U
#define GPIO_MODE_AF_PP 2U
#define GPIO_PULLUP 1U
#define GPIO_SPEED_FREQ_LOW 0U
#define GPIO_AF9_CAN2 9U
typedef struct { uint32_t Pin, Mode, Pull, Speed, Alternate; } GPIO_InitTypeDef;
void HAL_GPIO_Init(void *port, GPIO_InitTypeDef *gpio);
#define CAN_MODE_SILENT_LOOPBACK 0xC0000000U
#define CAN_SJW_1TQ 0U
#define CAN_BS1_13TQ 0x000C0000U
#define CAN_BS2_2TQ 0x00100000U
#define DISABLE 0U
#define ENABLE 1U
#define CAN_FILTERMODE_IDMASK 0U
#define CAN_FILTERSCALE_32BIT 1U
#define CAN_FILTER_FIFO0 0U
#define CAN_RX_FIFO0 0U
#define CAN_ID_STD 0U
#define CAN_RTR_DATA 0U
#define CAN_TX_MAILBOX0 1U
#define CAN_TX_MAILBOX1 2U
#define CAN_TSR_RQCP0 1U
#define CAN_TSR_RQCP1 0x100U
#define CAN_TSR_RQCP2 0x10000U
#define CAN_TSR_TXOK0 2U
typedef enum { HAL_OK, HAL_ERROR } HAL_StatusTypeDef;
#define RCC_OSCILLATORTYPE_HSE 1U
#define RCC_HSE_ON 1U
#define RCC_PLL_NONE 0U
#define RCC_CLOCKTYPE_SYSCLK 1U
#define RCC_CLOCKTYPE_HCLK 2U
#define RCC_CLOCKTYPE_PCLK1 4U
#define RCC_CLOCKTYPE_PCLK2 8U
#define RCC_SYSCLKSOURCE_HSE 1U
#define RCC_SYSCLK_DIV1 0U
#define RCC_HCLK_DIV1 0U
#define FLASH_LATENCY_0 0U
typedef struct { uint32_t OscillatorType, HSEState;
  struct { uint32_t PLLState; } PLL;
} RCC_OscInitTypeDef;
typedef struct { uint32_t ClockType, SYSCLKSource, AHBCLKDivider,
  APB1CLKDivider, APB2CLKDivider;
} RCC_ClkInitTypeDef;
HAL_StatusTypeDef HAL_RCC_OscConfig(RCC_OscInitTypeDef *osc);
HAL_StatusTypeDef HAL_RCC_ClockConfig(RCC_ClkInitTypeDef *clk, uint32_t latency);
typedef struct {
  uint32_t Prescaler, Mode, SyncJumpWidth, TimeSeg1, TimeSeg2;
  uint32_t TimeTriggeredMode, AutoBusOff, AutoWakeUp, AutoRetransmission;
  uint32_t ReceiveFifoLocked, TransmitFifoPriority;
} CAN_InitTypeDef;
typedef struct { CAN_TypeDef *Instance; CAN_InitTypeDef Init; uint32_t ErrorCode; } CAN_HandleTypeDef;
typedef struct {
  uint32_t SlaveStartFilterBank, FilterBank, FilterMode, FilterScale;
  uint32_t FilterIdHigh, FilterIdLow, FilterMaskIdHigh, FilterMaskIdLow;
  uint32_t FilterFIFOAssignment, FilterActivation;
} CAN_FilterTypeDef;
typedef struct { uint32_t StdId, ExtId, IDE, RTR, DLC, TransmitGlobalTime; } CAN_TxHeaderTypeDef;
typedef struct { uint32_t StdId, ExtId, IDE, RTR, DLC; } CAN_RxHeaderTypeDef;
uint32_t HAL_RCC_GetPCLK1Freq(void);
HAL_StatusTypeDef HAL_CAN_Init(CAN_HandleTypeDef *h);
HAL_StatusTypeDef HAL_CAN_ConfigFilter(CAN_HandleTypeDef *h, CAN_FilterTypeDef *f);
HAL_StatusTypeDef HAL_CAN_Start(CAN_HandleTypeDef *h);
HAL_StatusTypeDef HAL_CAN_AbortTxRequest(CAN_HandleTypeDef *h, uint32_t mailbox);
HAL_StatusTypeDef HAL_CAN_AddTxMessage(CAN_HandleTypeDef *h, CAN_TxHeaderTypeDef *tx, uint8_t data[], uint32_t *mailbox);
uint32_t HAL_CAN_GetRxFifoFillLevel(CAN_HandleTypeDef *h, uint32_t fifo);
HAL_StatusTypeDef HAL_CAN_GetRxMessage(CAN_HandleTypeDef *h, uint32_t fifo, CAN_RxHeaderTypeDef *rx, uint8_t data[]);
#endif
