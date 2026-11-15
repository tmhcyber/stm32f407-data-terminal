#ifndef MODBUS_FAKE_HAL_H
#define MODBUS_FAKE_HAL_H
#include <stdint.h>
typedef enum {HAL_OK,HAL_ERROR,HAL_BUSY,HAL_TIMEOUT} HAL_StatusTypeDef;
typedef struct {uint32_t SR;} USART_TypeDef;
typedef struct {uint32_t CNT;} TIM_TypeDef;
typedef struct {uint32_t unused;} GPIO_TypeDef;
typedef struct {uint32_t BaudRate,WordLength,StopBits,Parity,Mode,HwFlowCtl,OverSampling;} UART_InitTypeDef;
typedef struct {USART_TypeDef *Instance; UART_InitTypeDef Init;} UART_HandleTypeDef;
typedef struct {uint32_t Prescaler,CounterMode,Period,ClockDivision,AutoReloadPreload;} TIM_Base_InitTypeDef;
typedef struct {TIM_TypeDef *Instance; TIM_Base_InitTypeDef Init;} TIM_HandleTypeDef;
typedef struct {uint32_t APB1CLKDivider;} RCC_ClkInitTypeDef;
typedef struct {uint32_t Pin,Mode,Pull,Speed,Alternate;} GPIO_InitTypeDef;
extern USART_TypeDef fake_usart;
extern TIM_TypeDef fake_tim;
extern GPIO_TypeDef fake_gpio;
#define USART2 (&fake_usart)
#define TIM2 (&fake_tim)
#define GPIOC (&fake_gpio)
#define GPIO_PIN_0 1U
#define GPIO_PIN_RESET 0U
#define GPIO_PIN_SET 1U
#define GPIO_MODE_OUTPUT_PP 1U
#define GPIO_PULLDOWN 2U
#define GPIO_SPEED_FREQ_HIGH 3U
#define UART_WORDLENGTH_8B 8U
#define UART_WORDLENGTH_9B 9U
#define UART_STOPBITS_1 1U
#define UART_PARITY_NONE 0U
#define UART_PARITY_EVEN 2U
#define UART_MODE_TX_RX 3U
#define UART_HWCONTROL_NONE 0U
#define UART_OVERSAMPLING_16 16U
#define HAL_UART_ERROR_NONE 0U
#define TIM_COUNTERMODE_UP 0U
#define TIM_CLOCKDIVISION_DIV1 0U
#define TIM_AUTORELOAD_PRELOAD_DISABLE 0U
#define RCC_HCLK_DIV1 1U
#define USART_SR_RXNE 0x20U
#define USART_SR_ORE 8U
#define USART_SR_FE 2U
#define USART_SR_NE 4U
#define USART_SR_PE 1U
#define __HAL_RCC_GPIOC_CLK_ENABLE() ((void)0)
#define __HAL_RCC_TIM2_CLK_ENABLE() ((void)0)
#define __HAL_TIM_SET_COUNTER(h,v) ((h)->Instance->CNT=(v))
#define __HAL_TIM_GET_COUNTER(h) ((h)->Instance->CNT)
uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __enable_irq(void);
void HAL_GPIO_WritePin(GPIO_TypeDef *p,uint32_t pin,uint32_t value);
void HAL_GPIO_Init(GPIO_TypeDef *p,GPIO_InitTypeDef *init);
HAL_StatusTypeDef HAL_UART_Init(UART_HandleTypeDef *h);
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *h,uint8_t *data,uint16_t length);
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *h,uint8_t *data,uint16_t length);
HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *h);
uint32_t HAL_UART_GetError(UART_HandleTypeDef *h);
void HAL_UART_IRQHandler(UART_HandleTypeDef *h);
uint32_t HAL_RCC_GetPCLK1Freq(void);
void HAL_RCC_GetClockConfig(RCC_ClkInitTypeDef *clock,uint32_t *latency);
HAL_StatusTypeDef HAL_TIM_Base_Init(TIM_HandleTypeDef *h);
HAL_StatusTypeDef HAL_TIM_Base_Start(TIM_HandleTypeDef *h);
#endif
