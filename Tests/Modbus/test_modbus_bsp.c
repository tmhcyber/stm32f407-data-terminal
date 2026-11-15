#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stm32f4xx_hal.h"
#include "bsp_modbus_time.h"
#include "../../BSP/Src/bsp_rs485.c"

USART_TypeDef fake_usart;
TIM_TypeDef fake_tim;
GPIO_TypeDef fake_gpio;
static uint32_t primask,direction,errors,pclk=25000000U,divider=RCC_HCLK_DIV1;
static unsigned checks,receive_calls;
static TIM_Base_InitTypeDef timer_init;
static UART_InitTypeDef uart_init;
static HAL_StatusTypeDef timer_status=HAL_OK,receive_status=HAL_OK,transmit_status=HAL_OK;
#define CHECK(x) do { ++checks; if (!(x)) { printf("FAIL %d: %s\n",__LINE__,#x); exit(1); } } while (0)
uint32_t __get_PRIMASK(void) { return primask; }
void __disable_irq(void) { primask=1; }
void __enable_irq(void) { primask=0; }
void HAL_GPIO_WritePin(GPIO_TypeDef *p,uint32_t pin,uint32_t value) { (void)p;(void)pin;direction=value; }
void HAL_GPIO_Init(GPIO_TypeDef *p,GPIO_InitTypeDef *init) { (void)p;(void)init; }
HAL_StatusTypeDef HAL_UART_Init(UART_HandleTypeDef *h) { uart_init=h->Init; return HAL_OK; }
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *h,uint8_t *data,uint16_t length)
{ (void)h;(void)data;CHECK(length==1);++receive_calls;return receive_status; }
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *h,uint8_t *data,uint16_t length)
{ (void)h;(void)data;CHECK(length>0); return transmit_status; }
HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *h) { (void)h;return HAL_OK; }
uint32_t HAL_UART_GetError(UART_HandleTypeDef *h) { (void)h;return errors; }
void HAL_UART_IRQHandler(UART_HandleTypeDef *h) { (void)h; }
uint32_t HAL_RCC_GetPCLK1Freq(void) { return pclk; }
void HAL_RCC_GetClockConfig(RCC_ClkInitTypeDef *clock,uint32_t *latency)
{ clock->APB1CLKDivider=divider;*latency=0; }
HAL_StatusTypeDef HAL_TIM_Base_Init(TIM_HandleTypeDef *h) { timer_init=h->Init;return timer_status; }
HAL_StatusTypeDef HAL_TIM_Base_Start(TIM_HandleTypeDef *h) { (void)h;return timer_status; }
static void Feed(uint8_t byte,uint32_t us)
{ s_rx_staging_byte=byte;fake_tim.CNT=us;BSP_RS485_HandleRxCompleteIRQ(us/1000); }
int main(void)
{
  BSP_RS485_RxByteEvent_t byte;
  BSP_RS485_RxFaultEvent_t fault;
  BSP_RS485_TxEvent_t event;
  unsigned i,n;
  uint32_t quiet=0;
  uint8_t payload[2]={0x80,0xFF};
  CHECK(BSP_ModbusTime_Init() && timer_init.Prescaler==24 && timer_init.Period==0xFFFFFFFFU);
  fake_tim.CNT=1234; CHECK(BSP_ModbusTime_Micros()==1234);
  pclk=42000000;divider=2;CHECK(BSP_ModbusTime_Init() && timer_init.Prescaler==83);
  pclk=16000000;divider=1;CHECK(BSP_ModbusTime_Init() && timer_init.Prescaler==15);
  pclk=25500000;CHECK(!BSP_ModbusTime_Init());
  pclk=25000000;timer_status=HAL_ERROR;CHECK(!BSP_ModbusTime_Init());timer_status=HAL_OK;
  primask=1; CHECK(BSP_RS485_Init()==BSP_RS485_STATUS_OK && primask==1);
  primask=0;CHECK(BSP_RS485_Init()==BSP_RS485_STATUS_OK && direction==0 && primask==0);
  CHECK(uart_init.BaudRate==115200 && uart_init.StopBits==1);
#if APP_RS485_MODBUS_ENABLED
  CHECK(uart_init.WordLength==9 && uart_init.Parity==UART_PARITY_EVEN);
  CHECK(BSP_RS485_GetQuietTime(&quiet));
#else
  CHECK(uart_init.WordLength==8 && uart_init.Parity==UART_PARITY_NONE);
  CHECK(!BSP_RS485_GetQuietTime(&quiet));
#endif
  Feed(0xFF,3000); CHECK(BSP_RS485_TakeRxByte(&byte) && byte.byte==0xFF && byte.timestamp_ms==3);
#if APP_RS485_MODBUS_ENABLED
  CHECK(byte.timestamp_us==3000);
  fake_usart.SR=USART_SR_RXNE;CHECK(!BSP_RS485_GetQuietTime(&quiet));fake_usart.SR=0;
  Feed(1,3100); CHECK(!BSP_RS485_GetQuietTime(&quiet)); CHECK(BSP_RS485_FlushRx()==1);
#endif
  CHECK(BSP_RS485_WriteAsync(payload,2)==BSP_RS485_STATUS_OK && direction==1);
  CHECK(BSP_RS485_WriteAsync(payload,2)==BSP_RS485_STATUS_BUSY);
#if APP_RS485_MODBUS_ENABLED
  Feed(0x80,3200); CHECK(!BSP_RS485_TakeRxByte(&byte));
#endif
  fake_tim.CNT=4000; BSP_RS485_HandleTxCompleteIRQ(4);
  CHECK(direction==0); event=BSP_RS485_TakeTxEvent(); CHECK(event.type==BSP_RS485_TX_EVENT_COMPLETE && event.timestamp_ms==4);
#if APP_RS485_MODBUS_ENABLED
  CHECK(event.timestamp_us==4000);
#endif
  CHECK(BSP_RS485_TakeTxEvent().type==BSP_RS485_TX_EVENT_NONE);
  transmit_status=HAL_ERROR; CHECK(BSP_RS485_WriteAsync(payload,2)==BSP_RS485_STATUS_ERROR && direction==0);
  transmit_status=HAL_OK; CHECK(BSP_RS485_WriteAsync(payload,2)==BSP_RS485_STATUS_OK);
  CHECK(BSP_RS485_AbortTransmit()==BSP_RS485_STATUS_OK && direction==0);
  BSP_RS485_HandleTxCompleteIRQ(5); CHECK(BSP_RS485_TakeTxEvent().type==BSP_RS485_TX_EVENT_NONE);
  for(i=0;i<65;++i) Feed((uint8_t)i,6000+i*100);
#if APP_RS485_MODBUS_ENABLED
  CHECK(!BSP_RS485_GetQuietTime(&quiet));
#endif
  fault=BSP_RS485_TakeRxFaultEvent(); CHECK(fault.overflow_count==1);
  for(i=0;i<64;++i) CHECK(BSP_RS485_TakeRxByte(&byte) && byte.byte==i);
  CHECK(!BSP_RS485_TakeRxByte(&byte));
  errors=USART_SR_PE;n=receive_calls;Feed(1,20000);CHECK(receive_calls==n); /* preserve flags before rearm */
  errors=0;BSP_RS485_HandleUartErrorIRQ(20,USART_SR_PE);
  fault=BSP_RS485_TakeRxFaultEvent(); CHECK(fault.uart_error_count==1 && fault.last_uart_error_flags==USART_SR_PE);
  receive_status=HAL_ERROR;CHECK(BSP_RS485_Init()==BSP_RS485_STATUS_ERROR && !s_initialized && direction==0);
  printf("RS485 mode %d / TIM2 production C (fake HAL): %u checks passed\n",APP_RS485_MODBUS_ENABLED,checks);
  return 0;
}
