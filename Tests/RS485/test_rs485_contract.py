from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


class Rs485ContractTests(unittest.TestCase):
    def test_confirmed_stage_one_parameters_are_bounded(self):
        header = read("Application/Inc/app_rs485_service.h")
        source = read("Application/Src/app_rs485_service.c")

        self.assertIn("APP_RS485_MAX_FRAME_BYTES          32U", header)
        self.assertIn("APP_RS485_INTERBYTE_TIMEOUT_MS    100U", header)
        self.assertIn("APP_RS485_TX_COMPLETE_TIMEOUT_MS   10U", header)
        self.assertIn('"V1|PING\\r\\n"', source)
        self.assertIn('"V1|PONG\\r\\n"', source)

    def test_application_is_nonblocking_and_has_no_hal_dependency(self):
        source = read("Application/Src/app_rs485_service.c")

        self.assertNotIn("stm32f4xx_hal", source)
        self.assertNotIn("HAL_", source)
        self.assertNotIn("HAL_Delay", source)
        self.assertNotIn("while (1", source)

    def test_bsp_uses_usart2_and_releases_direction_on_tc_callback(self):
        source = read("BSP/Src/bsp_rs485.c")
        callback_source = read("BSP/Src/bsp_uart.c")

        for token in (
            "USART2",
            "GPIO_PIN_0",
            "HAL_UART_Transmit_IT",
            "HAL_UART_Receive_IT",
            "BSP_RS485_HandleTxCompleteIRQ",
        ):
            self.assertIn(token, source)
        self.assertIn("GPIO_PIN_2 | GPIO_PIN_3", callback_source)
        self.assertIn("GPIO_AF7_USART2", callback_source)
        self.assertNotIn("HAL_UART_Transmit(", source)
        self.assertNotIn("HAL_Delay", source)
        callback = source.index("void BSP_RS485_HandleTxCompleteIRQ")
        release = source.index("BSP_RS485_SetReceiveDirection();", callback)
        complete = source.index("BSP_RS485_TX_EVENT_COMPLETE", callback)
        self.assertLess(release, complete)

    def test_rx_callback_preserves_hal_error_before_rearming(self):
        source = read("BSP/Src/bsp_rs485.c")

        callback = source.index("void BSP_RS485_HandleRxCompleteIRQ")
        error_check = source.index(
            "HAL_UART_GetError(&s_uart2_handle) != HAL_UART_ERROR_NONE",
            callback,
        )
        rearm = source.index("HAL_UART_Receive_IT", error_check)
        self.assertLess(error_check, rearm)
        self.assertIn("error callback preserve the flags", source)

    def test_bsp_publishes_initialized_atomically_with_first_rx_arm(self):
        source = read("BSP/Src/bsp_rs485.c")

        self.assertIn("static volatile uint8_t s_initialized;", source)
        init_start = source.index("BSP_RS485_Status_t BSP_RS485_Init(void)")
        write_start = source.index("BSP_RS485_Status_t BSP_RS485_WriteAsync", init_start)
        init_source = source[init_start:write_start]
        irq_disable = init_source.index("__disable_irq();")
        first_rx_arm = init_source.index("HAL_UART_Receive_IT")
        initialized = init_source.index("s_initialized = 1U;", first_rx_arm)
        irq_restore = init_source.index("if (primask == 0U)", initialized)

        self.assertLess(irq_disable, first_rx_arm)
        self.assertLess(first_rx_arm, initialized)
        self.assertLess(initialized, irq_restore)
        self.assertIn("Publish the initialized flag atomically", init_source)

    def test_overflow_flushes_untrusted_bytes_before_discard_resync(self):
        source = read("Application/Src/app_rs485_service.c")

        handler = source.index("static void App_RS485Service_HandleRxFault")
        flush = source.index("BSP_RS485_FlushRx()", handler)
        discard = source.index("App_RS485Service_EnterDiscard", flush)
        self.assertLess(flush, discard)
        native_test = read("Tests/C/test_core_contracts.c")
        self.assertIn("TestRs485OverflowDiscardsUntrustedQueuedPing", native_test)

    def test_usart1_uplink_remains_and_shared_callbacks_dispatch_usart2(self):
        source = read("BSP/Src/bsp_uart.c")

        self.assertIn("uart_handle == &s_uart1_handle", source)
        self.assertIn("uart_handle->Instance == USART2", source)
        self.assertIn("BSP_RS485_HandleRxCompleteIRQ", source)
        self.assertIn("BSP_RS485_HandleTxCompleteIRQ", source)
        self.assertIn("BSP_RS485_HandleUartErrorIRQ", source)

    def test_core_keil_and_native_runner_include_rs485_sources(self):
        main = read("Core/Src/main.c")
        project = read("MDK-ARM/IndustrialTerminal.uvprojx")
        runner = read("Tools/run_c_tests.ps1")

        self.assertIn("App_RS485Service_Init", main)
        self.assertIn("App_RS485Service_Process", main)
        self.assertIn("bsp_rs485.c", project)
        self.assertIn("app_rs485_service.c", project)
        self.assertIn("app_rs485_service.c", runner)


if __name__ == "__main__":
    unittest.main()
