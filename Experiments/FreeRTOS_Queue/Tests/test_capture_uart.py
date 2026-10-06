"""Synthetic host tests for the evidence validator, never board evidence."""
import importlib.util
from pathlib import Path
import unittest

path = Path(__file__).resolve().parents[1] / "Tools/capture_uart.py"
spec = importlib.util.spec_from_file_location("capture_uart", path)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def frame(seq, tick, drop=0, txerr=0):
    return (f"RTOSQ|seq={seq}|tick={tick}|rx={(seq+1)&0xffffffff}|drop={drop}"
            f"|temp=2536|hum=6000|txerr={txerr}|stack_a=180|stack_b=280\r\n").encode()


class CaptureTests(unittest.TestCase):
    def test_diagnostic_protocol_requires_history_fields(self):
        raw = self.latest(3, 3009, 2, 3000) + self.latest(6, 6019, 3, 6000)
        old = raw.replace(b"RTOSM", b"RTOSS").replace(b"\r\n", b"|fail=0|last=0\r\n")
        new = old.replace(b"\r\n", b"|fail_stage=0|fail_status=0\r\n")
        self.assertTrue(module.analyze(new, 2, "sensor-diag")["passed"])
        self.assertFalse(module.analyze(old, 2, "sensor-diag")["passed"])
        self.assertFalse(module.analyze(new, 2, "sensor")["passed"])
        for bad in (new.replace(b"fail_stage=0", b"fail_stage=2"),
                    new.replace(b"fail_status=0", b"fail_status=6"),
                    b"RTOSS_BOOT|v=3|source=sht30\r\n" + new,
                    b"SHTSTAT|tick=2000|state=no_new_data|fail=1|last=2|fail_stage=1|fail_status=2\r\n" + new):
            self.assertFalse(module.analyze(bad, 2, "sensor-diag")["passed"])

    def test_sensor_accepts_real_values_and_rejects_failures(self):
        raw = self.latest(3, 3009, 2, 3000) + self.latest(6, 6019, 3, 6000)
        raw = raw.replace(b"RTOSM", b"RTOSS").replace(b"temp=2536", b"temp=-125")
        raw = raw.replace(b"\r\n", b"|fail=0|last=0\r\n")
        self.assertTrue(module.analyze(raw, 2, "sensor")["passed"])
        for bad in (raw.replace(b"fail=0", b"fail=1"),
                    raw.replace(b"last=0", b"last=6"),
                    raw.replace(b"hum=6000", b"hum=10001"),
                    b"SHTSTAT|tick=2000|state=no_new_data|fail=2|last=2\r\n" + raw):
            self.assertFalse(module.analyze(bad, 2, "sensor")["passed"])

    @staticmethod
    def latest(seq, tick, rx, sample_tick):
        return (f"RTOSM|seq={seq}|tick={tick}|rx={rx}|skip={(seq+1-rx)&0xffffffff}"
                f"|sample_tick={sample_tick}|age={(tick-sample_tick)&0xffffffff}"
                "|temp=2536|hum=6000|txerr=0|stack_a=180|stack_b=280\r\n").encode()

    def test_latest_fresh_samples_with_skips(self):
        raw=self.latest(3, 3009, 2, 3000)+self.latest(6, 6019, 3, 6000)
        result=module.analyze(raw, 2, "latest")
        self.assertTrue(result["passed"])
        self.assertEqual(result["age_ticks"], {"min": 9, "max": 19})
        self.assertFalse(module.analyze(raw, 2, "congestion")["passed"])

    def test_latest_rejects_old_data_bad_age_or_skip(self):
        first=self.latest(3, 3009, 2, 3000)
        second=self.latest(6, 6019, 3, 6000)
        for bad in (self.latest(6, 6019, 3, 4000),
                    second.replace(b"age=19", b"age=18"),
                    second.replace(b"skip=4", b"skip=3"),
                    second.replace(b"txerr=0", b"txerr=1")):
            self.assertFalse(module.analyze(first+bad, 2, "latest")["passed"])

    def test_latest_rejects_missing_pc_line(self):
        raw=self.latest(3, 3009, 2, 3000)+self.latest(9, 9030, 4, 9000)
        self.assertFalse(module.analyze(raw, 2, "latest")["passed"])

    def test_latest_tick_and_sequence_wrap(self):
        mask=0xffffffff
        first=self.latest(mask-1, mask-100, mask-5, mask-110)
        second=self.latest(1, 2909, mask-4, 2899)
        self.assertTrue(module.analyze(first+second, 2, "latest")["passed"])

    def test_congestion_requires_drop_growth_and_seq_gap(self):
        raw = frame(10, 18000, drop=5).replace(b"rx=11", b"rx=7")
        raw += frame(13, 21010, drop=7).replace(b"rx=14", b"rx=8")
        self.assertTrue(module.analyze(raw, 2, "congestion")["passed"])
        self.assertFalse(module.analyze(raw, 2, "normal")["passed"])

    def test_congestion_still_rejects_pc_loss_and_uart_error(self):
        first = frame(10, 18000, drop=5).replace(b"rx=11", b"rx=7")
        lost = frame(16, 24020, drop=9).replace(b"rx=17", b"rx=9")
        error = frame(13, 21010, drop=7, txerr=1).replace(b"rx=14", b"rx=8")
        for second in (lost, error):
            self.assertFalse(module.analyze(first+second, 2, "congestion")["passed"])

    def test_slow_output_without_congestion_is_not_pass(self):
        self.assertFalse(module.analyze(frame(0, 0)+frame(1, 3000), 2, "congestion")["passed"])

    def test_valid_window_after_late_attach(self):
        self.assertTrue(module.analyze(frame(12, 12000)+frame(13, 13000), 2)["passed"])

    def test_sequence_loss_rejected(self):
        self.assertFalse(module.analyze(frame(1, 1000)+frame(3, 3000), 2)["passed"])

    def test_duplicate_rejected(self):
        self.assertFalse(module.analyze(frame(1, 1000)*2, 2)["passed"])

    def test_reboot_rejected(self):
        raw = frame(1, 1000)+b"RTOSQ_BOOT|v=1\r\n"+frame(0, 0)
        self.assertFalse(module.analyze(raw, 2)["passed"])

    def test_dropped_and_uart_errors_rejected(self):
        for options in ({"drop": 1}, {"txerr": 1}):
            with self.subTest(options=options):
                self.assertFalse(module.analyze(frame(0, 0)+frame(1, 1000, **options), 2)["passed"])

    def test_partial_capture_boundaries(self):
        raw=b"hum=6000\r\n"+frame(0, 0)+frame(1, 1000)+b"RTOSQ|seq=2"
        result=module.analyze(raw, 2)
        self.assertTrue(result["passed"])
        self.assertTrue(result["initial_partial_line_discarded"])

    def test_bad_complete_line_not_silently_skipped(self):
        raw=frame(0, 0)+b"corruption\r\n"+frame(1, 1000)
        self.assertFalse(module.analyze(raw, 2)["passed"])

    def test_empty_and_insufficient_fail(self):
        for raw in (b"", frame(0, 0)):
            self.assertFalse(module.analyze(raw, 2)["passed"])

    def test_wrap_is_not_reset(self):
        raw=frame(0xffffffff, 0xfffffff0)+frame(0, (0xfffffff0+1000)&0xffffffff)
        self.assertTrue(module.analyze(raw, 2)["passed"])


if __name__ == "__main__":
    unittest.main()
