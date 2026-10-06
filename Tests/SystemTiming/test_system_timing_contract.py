from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


class SystemTimingContractTests(unittest.TestCase):
    def test_bsp_uses_dwt_cycle_counter_without_hal_delay(self):
        source = read("BSP/Src/bsp_system_timing.c")

        self.assertIn("CoreDebug_DEMCR_TRCENA_Msk", source)
        self.assertIn("DWT_CTRL_CYCCNTENA_Msk", source)
        self.assertIn("DWT->CYCCNT", source)
        self.assertIn("SystemCoreClock", source)
        self.assertNotIn("HAL_Delay", source)

    def test_monitor_keeps_active_and_observed_reason_boundaries(self):
        header = read("Application/Inc/app_system_monitor.h")
        source = read("Application/Src/app_system_monitor.c")

        for name in (
            "APP_SYSTEM_HEALTH_STARTING",
            "APP_SYSTEM_HEALTH_HEALTHY",
            "APP_SYSTEM_HEALTH_DEGRADED",
            "APP_SYSTEM_HEALTH_UNHEALTHY",
            "active_reasons",
            "observed_reasons",
            "hard_overrun_count",
            "transition_count",
        ):
            self.assertIn(name, header)

        self.assertIn("s_report.observed_reasons |= active_reasons", source)
        self.assertIn("APP_SYSTEM_MONITOR_TIMING_RECOVERY_MS", source)

    def test_thresholds_match_user_designed_mvp(self):
        header = read("Application/Inc/app_system_monitor.h")

        self.assertIn("APP_SYSTEM_MONITOR_LOOP_WARNING_MS       5U", header)
        self.assertIn("APP_SYSTEM_MONITOR_LOOP_HARD_MS         25U", header)
        self.assertIn("APP_SYSTEM_MONITOR_DATA_FRESHNESS_MS  1500U", header)
        self.assertIn("APP_SYSTEM_MONITOR_TX_SILENCE_MS       6000U", header)
        self.assertIn("APP_SYSTEM_MONITOR_TIMING_RECOVERY_MS  1000U", header)

    def test_main_measures_each_top_level_service_and_publishes_health(self):
        main = read("Core/Src/main.c")

        self.assertIn("BSP_SystemTiming_Init", main)
        self.assertIn("timing.collector_cycles", main)
        self.assertIn("timing.snapshot_cycles", main)
        self.assertIn("timing.uplink_cycles", main)
        self.assertIn("timing.loop_gap_cycles", main)
        self.assertIn("App_SystemMonitor_Process", main)

    def test_keil_and_native_test_paths_include_new_production_sources(self):
        project = read("MDK-ARM/IndustrialTerminal.uvprojx")
        runner = read("Tools/run_c_tests.ps1")
        native_test = read("Tests/C/test_core_contracts.c")

        self.assertIn("bsp_system_timing.c", project)
        self.assertIn("app_system_monitor.c", project)
        self.assertIn("app_system_monitor.c", runner)
        self.assertIn("TestSystemMonitorUserDesignedLifecycle", native_test)


if __name__ == "__main__":
    unittest.main()
