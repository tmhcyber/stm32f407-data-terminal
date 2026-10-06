from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


class SystemDiagnosticsContractTests(unittest.TestCase):
    def test_reset_report_keeps_raw_value_and_decoded_bitmask(self):
        header = read("BSP/Inc/bsp_system_diagnostics.h")
        record_source = read("BSP/Src/bsp_system_diagnostics_record.c")

        self.assertIn("raw_rcc_csr", header)
        self.assertIn("reasons", header)
        self.assertIn("BSP_SYSTEM_DIAGNOSTICS_RESET_REASON_PIN", header)
        self.assertIn("BSP_SYSTEM_DIAGNOSTICS_RESET_REASON_SOFTWARE", header)
        self.assertIn("BSP_SystemDiagnostics_DecodeResetReasons", record_source)

    def test_fault_record_has_commit_validation_and_frame_boundary(self):
        header = read("BSP/Inc/bsp_system_diagnostics.h")
        record_source = read("BSP/Src/bsp_system_diagnostics_record.c")

        for field in (
            "commit",
            "checksum",
            "version",
            "size",
            "fault_type",
            "exc_return",
            "original_sp",
            "frame_valid",
            "frame_reject_reasons",
            "cfsr",
            "hfsr",
            "bfar",
            "pc",
        ):
            self.assertIn(field, header)

        self.assertIn("BSP_SystemDiagnostics_ValidateFaultRecord", record_source)
        self.assertIn("BSP_SystemDiagnostics_ConsumeFaultRecord", record_source)
        self.assertIn("BSP_SYSTEM_DIAGNOSTICS_CFSR_STACK_ERROR_MASK", record_source)
        self.assertIn("BSP_SystemDiagnostics_FrameRangeIsInsideRam", record_source)

    def test_main_captures_before_hal_and_then_enables_faults(self):
        main = read("Core/Src/main.c")

        capture = main.index("BSP_SystemDiagnostics_CaptureBootReport")
        enable = main.index("BSP_SystemDiagnostics_EnableConfigurableFaults")
        hal_init = main.index("HAL_Init();")
        self.assertLess(capture, enable)
        self.assertLess(enable, hal_init)
        self.assertIn("g_bsp_system_boot_report", main)

    def test_exception_entry_selects_original_stack_and_switches_to_emergency_stack(self):
        handlers = read("Core/Src/stm32f4xx_it.c")

        self.assertRegex(handlers, r"CPSID\s+I")
        self.assertRegex(handlers, r"TST\s+R1, #4")
        self.assertRegex(handlers, r"MRSEQ\s+R2, MSP")
        self.assertRegex(handlers, r"MRSNE\s+R2, PSP")
        self.assertIn("g_bsp_system_diagnostics_fault_stack_top", handlers)
        self.assertIn("MSR     MSP, R3", handlers)
        self.assertIn("BSP_SystemDiagnostics_CaptureFault", handlers)

    def test_fault_capture_has_no_peripheral_or_dynamic_memory_dependency(self):
        source = read("BSP/Src/bsp_system_diagnostics.c")

        for forbidden in (
            "HAL_",
            "BSP_UART",
            "BSP_I2C",
            "malloc",
            "printf",
            "HAL_GetTick",
        ):
            self.assertNotIn(forbidden, source)
        self.assertIn("SCB->CFSR", source)
        self.assertIn("SCB->HFSR", source)
        self.assertIn("while (1)", source)

    def test_keil_project_uses_explicit_uninit_scatter_region(self):
        project = read("MDK-ARM/IndustrialTerminal.uvprojx")
        scatter = read("MDK-ARM/IndustrialTerminal.sct")

        self.assertIn("bsp_system_diagnostics_record.c", project)
        self.assertIn("bsp_system_diagnostics.c", project)
        self.assertIn(".\\IndustrialTerminal.sct", project)
        self.assertIn("RW_FAULT_STACK", scatter)
        self.assertIn("RW_FAULT_NOINIT", scatter)
        self.assertRegex(scatter, r"RW_FAULT_NOINIT[^\n]*UNINIT")
        self.assertIn("*(NoInit)", scatter)


if __name__ == "__main__":
    unittest.main()
