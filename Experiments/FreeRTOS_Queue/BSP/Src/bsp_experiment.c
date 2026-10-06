#include "bsp_experiment.h"

static UART_HandleTypeDef s_uart;
volatile uint32_t g_experiment_fault;

HAL_StatusTypeDef BSP_Experiment_Init(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    HAL_Init();
    HAL_NVIC_SetPriorityGrouping(NVIC_PRIORITYGROUP_4);
    /* Reuse the board's verified Y8 25 MHz HSE, without PLL. */
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.PLL.PLLState = RCC_PLL_NONE;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) return HAL_ERROR;
    clk.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                    RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_HSE;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV1;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_0) != HAL_OK) return HAL_ERROR;
    if (SystemCoreClock != 25000000U) return HAL_ERROR;

    s_uart.Instance = USART1;
    s_uart.Init.BaudRate = 115200U;
    s_uart.Init.WordLength = UART_WORDLENGTH_8B;
    s_uart.Init.StopBits = UART_STOPBITS_1;
    s_uart.Init.Parity = UART_PARITY_NONE;
    s_uart.Init.Mode = UART_MODE_TX;
    s_uart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    s_uart.Init.OverSampling = UART_OVERSAMPLING_16;
    return HAL_UART_Init(&s_uart);
}

void HAL_UART_MspInit(UART_HandleTypeDef *uart)
{
    GPIO_InitTypeDef gpio = {0};
    if (uart->Instance != USART1) return;
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();
    gpio.Pin = GPIO_PIN_9;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOA, &gpio);
    /* TX polling only; no UART IRQ and no FromISR calls in this experiment. */
}

HAL_StatusTypeDef BSP_Experiment_Write(const char *text, uint16_t length)
{
    /* B owns UART after scheduler start. This is bounded polling, not an
     * RTOS-blocking UART driver. A can preempt B; no critical section here.
     * <=256 bytes at 115200 8N1 needs <23 ms on the wire; allow 100 ms. */
    return HAL_UART_Transmit(&s_uart, (uint8_t *)text, length, 100U);
}

void Experiment_Panic(uint32_t reason)
{
    __disable_irq();
    g_experiment_fault = reason;
    while (1) { __NOP(); }
}
