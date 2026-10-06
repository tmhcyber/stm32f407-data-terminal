from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


class WatchdogGateContractTests(unittest.TestCase):
    def test_policy_masks_match_user_designed_gate(self):
        header = read("Application/Inc/app_watchdog_gate.h")

        self.assertIn("APP_WATCHDOG_GATE_REFRESH_PERIOD_MS  250U", header)
        for name in (
            "APP_SYSTEM_HEALTH_REASON_LOOP_WARNING",
            "APP_SYSTEM_HEALTH_REASON_DATA_STALE",
            "APP_SYSTEM_HEALTH_REASON_SOURCE_OFFLINE",
            "APP_SYSTEM_HEALTH_REASON_LOOP_HARD_OVERRUN",
            "APP_SYSTEM_HEALTH_REASON_UPLINK_STALLED",
            "APP_SYSTEM_HEALTH_REASON_UPLINK_ERROR",
            "APP_SYSTEM_HEALTH_REASON_COLLECTOR_FAULT",
            "APP_SYSTEM_HEALTH_REASON_MONITOR_INVALID",
        ):
            self.assertIn(name, header)

    def test_gate_exposes_wait_due_hold_and_latched_decisions(self):
        header = read("Application/Inc/app_watchdog_gate.h")

        for name in (
            "APP_WATCHDOG_GATE_DECISION_WAIT_PERIOD",
            "APP_WATCHDOG_GATE_DECISION_REFRESH_DUE",
            "APP_WATCHDOG_GATE_DECISION_HOLD",
            "APP_WATCHDOG_GATE_DECISION_LATCHED",
        ):
            self.assertIn(name, header)

    def test_gate_latches_invalid_unknown_and_refresh_failure_inputs(self):
        header = read("Application/Inc/app_watchdog_gate.h")
        source = read("Application/Src/app_watchdog_gate.c")

        for name in (
            "APP_WATCHDOG_GATE_INTERNAL_INPUT_INVALID",
            "APP_WATCHDOG_GATE_INTERNAL_UNKNOWN_REASON",
            "APP_WATCHDOG_GATE_INTERNAL_REFRESH_FAILED",
        ):
            self.assertIn(name, header)
            self.assertIn(name, source)

        self.assertIn("s_report.latched_reasons |= new_latch_reasons", source)
        self.assertIn("s_report.latched_reasons != 0U", source)

    def test_application_gate_has_no_hardware_or_blocking_dependency(self):
        source = read("Application/Src/app_watchdog_gate.c")

        for forbidden in (
            "HAL_",
            "BSP_",
            "IWDG->",
            "HAL_Delay",
            "malloc",
            "free(",
        ):
            self.assertNotIn(forbidden, source)

    def test_keil_and_native_tests_compile_the_integrated_gate(self):
        project = read("MDK-ARM/IndustrialTerminal.uvprojx")
        runner = read("Tools/run_c_tests.ps1")
        native_test = read("Tests/C/test_core_contracts.c")
        main = read("Core/Src/main.c")

        self.assertIn("app_watchdog_gate.c", project)
        self.assertIn("app_watchdog_gate.c", runner)
        self.assertIn("TestWatchdogGateRefreshPeriodAndAllowedReasons", native_test)
        self.assertNotIn("HAL_IWDG_Refresh", main)

    def test_bsp_owns_the_iwdg_configuration_and_hal_calls(self):
        header = read("BSP/Inc/bsp_system_watchdog.h")
        source = read("BSP/Src/bsp_system_watchdog.c")

        self.assertIn("BSP_SYSTEM_WATCHDOG_PRESCALER_DIVIDER  32U", header)
        self.assertIn("BSP_SYSTEM_WATCHDOG_RELOAD_VALUE      1999U", header)
        self.assertIn("IWDG_PRESCALER_32", source)
        self.assertIn("HAL_IWDG_Init", source)
        self.assertIn("HAL_IWDG_Refresh", source)
        self.assertIn("__HAL_DBGMCU_FREEZE_IWDG", source)

    def test_keil_builds_the_bsp_and_hal_iwdg_sources(self):
        project = read("MDK-ARM/IndustrialTerminal.uvprojx")

        self.assertIn("bsp_system_watchdog.c", project)
        self.assertIn("stm32f4xx_hal_iwdg.c", project)

    def test_core_initializes_gate_after_the_first_hardware_reload(self):
        main = read("Core/Src/main.c")

        monitor_init = main.index("App_SystemMonitor_Init(")
        bsp_init = main.index("BSP_SystemWatchdog_Init()")
        gate_init = main.index("App_WatchdogGate_Init(HAL_GetTick())")
        loop = main.index("while (1)")
        self.assertLess(monitor_init, bsp_init)
        self.assertLess(bsp_init, gate_init)
        self.assertLess(gate_init, loop)

    def test_core_is_the_only_production_c_refresh_requester(self):
        main = read("Core/Src/main.c")

        self.assertEqual(main.count("BSP_SystemWatchdog_Refresh()"), 1)
        self.assertNotIn("HAL_IWDG_Refresh", main)
        for relative_path in (
            "Application/Src/app_system_monitor.c",
            "Application/Src/app_watchdog_gate.c",
            "Application/Src/app_sht30_collector.c",
            "Application/Src/app_uplink_service.c",
        ):
            self.assertNotIn("BSP_SystemWatchdog_Refresh", read(relative_path))

    def test_core_feeds_back_the_real_bsp_refresh_result(self):
        main = read("Core/Src/main.c")

        self.assertIn("APP_WATCHDOG_GATE_DECISION_REFRESH_DUE", main)
        self.assertIn("APP_WATCHDOG_GATE_REFRESH_SUCCESS", main)
        self.assertIn("APP_WATCHDOG_GATE_REFRESH_FAILURE", main)
        self.assertIn("App_WatchdogGate_RecordRefreshResult", main)
        self.assertIn("App_SystemWatchdog_Process(HAL_GetTick())", main)

    def test_core_copies_health_timestamps_before_monitor_time_sample(self):
        main = read("Core/Src/main.c")

        health_snapshot = main.index("App_SystemMonitor_FillHealthInputs(&health);")
        monitor_now = main.index("now_ms = HAL_GetTick();", health_snapshot)
        monitor_process = main.index("App_SystemMonitor_Process(", health_snapshot)
        self.assertLess(health_snapshot, monitor_now)
        self.assertLess(monitor_now, monitor_process)

if __name__ == "__main__":
    unittest.main()
