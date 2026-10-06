import re
import unittest
from pathlib import Path


HEADER_PATH = (
    Path(__file__).resolve().parents[2]
    / "Application"
    / "Inc"
    / "app_sht30_collector.h"
)


def read_policy_value(name):
    header = HEADER_PATH.read_text(encoding="utf-8")
    match = re.search(rf"^#define\s+{name}\s+(\d+)U$", header, re.MULTILINE)
    if match is None:
        raise AssertionError(f"missing policy constant: {name}")
    return int(match.group(1))


class CollectorPolicyModel:
    def __init__(self, offline_threshold):
        self.offline_threshold = offline_threshold
        self.device_state = "UNKNOWN"
        self.quality = "NOT_AVAILABLE"
        self.has_valid_measurement = False
        self.last_value = None
        self.last_success_at_ms = None
        self.consecutive_failed_rounds = 0
        self.failed_round_count = 0
        self.success_count = 0
        self.recovery_count = 0

    def finish_failed_round(self):
        self.failed_round_count += 1
        self.consecutive_failed_rounds += 1
        if self.consecutive_failed_rounds >= self.offline_threshold:
            self.device_state = "OFFLINE"
            self.quality = "OFFLINE"
        elif self.has_valid_measurement:
            self.quality = "STALE"

    def finish_successful_round(self, value, now_ms):
        recovered = self.device_state == "OFFLINE"
        self.device_state = "ONLINE"
        self.quality = "VALID"
        self.has_valid_measurement = True
        self.last_value = value
        self.last_success_at_ms = now_ms
        self.consecutive_failed_rounds = 0
        self.success_count += 1
        if recovered:
            self.recovery_count += 1


class AppSht30PolicyTests(unittest.TestCase):
    def setUp(self):
        self.offline_threshold = read_policy_value(
            "APP_SHT30_OFFLINE_THRESHOLD_ROUNDS"
        )

    def test_experimental_policy_values(self):
        self.assertEqual(read_policy_value("APP_SHT30_SAMPLE_PERIOD_MS"), 1000)
        self.assertEqual(
            read_policy_value("APP_SHT30_MAX_RETRIES_PER_ROUND"), 2
        )
        self.assertEqual(read_policy_value("APP_SHT30_RETRY_DELAY_MS"), 100)
        self.assertEqual(self.offline_threshold, 3)
        self.assertEqual(
            read_policy_value("APP_SHT30_OFFLINE_PROBE_PERIOD_MS"), 2000
        )

    def test_three_failed_attempts_are_one_failed_round(self):
        attempts = 1 + read_policy_value(
            "APP_SHT30_MAX_RETRIES_PER_ROUND"
        )
        model = CollectorPolicyModel(self.offline_threshold)

        self.assertEqual(attempts, 3)
        model.finish_failed_round()

        self.assertEqual(model.failed_round_count, 1)
        self.assertEqual(model.consecutive_failed_rounds, 1)
        self.assertNotEqual(model.device_state, "OFFLINE")

    def test_last_good_value_becomes_stale_without_new_timestamp(self):
        model = CollectorPolicyModel(self.offline_threshold)
        model.finish_successful_round((28500, 70000), 1000)
        model.finish_failed_round()

        self.assertEqual(model.last_value, (28500, 70000))
        self.assertEqual(model.last_success_at_ms, 1000)
        self.assertEqual(model.quality, "STALE")

    def test_three_failed_rounds_mark_device_offline(self):
        model = CollectorPolicyModel(self.offline_threshold)

        for _ in range(self.offline_threshold):
            model.finish_failed_round()

        self.assertEqual(model.device_state, "OFFLINE")
        self.assertEqual(model.quality, "OFFLINE")

    def test_only_complete_success_recovers_and_counts_transition(self):
        model = CollectorPolicyModel(self.offline_threshold)
        for _ in range(self.offline_threshold):
            model.finish_failed_round()

        # An ACK followed by a CRC error is still a failed round.
        model.finish_failed_round()
        self.assertEqual(model.device_state, "OFFLINE")
        self.assertEqual(model.recovery_count, 0)

        model.finish_successful_round((29000, 71000), 9000)
        self.assertEqual(model.device_state, "ONLINE")
        self.assertEqual(model.quality, "VALID")
        self.assertEqual(model.consecutive_failed_rounds, 0)
        self.assertEqual(model.recovery_count, 1)

        model.finish_successful_round((29100, 71100), 10000)
        self.assertEqual(model.recovery_count, 1)


if __name__ == "__main__":
    unittest.main()
