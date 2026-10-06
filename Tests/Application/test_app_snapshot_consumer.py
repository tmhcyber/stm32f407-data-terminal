import copy
import unittest
from dataclasses import dataclass, field
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HEADER_PATH = ROOT / "Application" / "Inc" / "app_snapshot_consumer.h"
SOURCE_PATH = ROOT / "Application" / "Src" / "app_snapshot_consumer.c"
MAIN_PATH = ROOT / "Core" / "Src" / "main.c"
PROJECT_PATH = ROOT / "MDK-ARM" / "IndustrialTerminal.uvprojx"


INITIAL_SNAPSHOT = "INITIAL_SNAPSHOT"
SNAPSHOT_CHANGED = "SNAPSHOT_CHANGED"
NEW_VALID_DATA = "NEW_VALID_DATA"
QUALITY_CHANGED = "QUALITY_CHANGED"
STATUS_CHANGED = "STATUS_CHANGED"
SOURCE_OFFLINE = "SOURCE_OFFLINE"
SOURCE_RECOVERED = "SOURCE_RECOVERED"
VERSION_GAP = "VERSION_GAP"


@dataclass
class Point:
    raw_value: int
    quality: str
    last_status: str
    timestamp_ms: int
    status_timestamp_ms: int
    sequence: int


@dataclass
class Snapshot:
    version: int
    points: list[Point] = field(default_factory=list)


class SnapshotConsumerModel:
    def __init__(self):
        self.previous = None
        self.total_skipped_count = 0

    def process(self, snapshot):
        if snapshot is None:
            return "BUSY", None

        current = copy.deepcopy(snapshot)
        if self.previous is None:
            self.previous = current
            return "OK", {
                "flags": {INITIAL_SNAPSHOT},
                "skipped_count": 0,
                "snapshot": current,
            }

        if self.previous.version == current.version:
            return "OK", None

        flags = {SNAPSHOT_CHANGED}
        for old_point, new_point in zip(
            self.previous.points, current.points
        ):
            if old_point.sequence != new_point.sequence:
                flags.add(NEW_VALID_DATA)
            if old_point.quality != new_point.quality:
                flags.add(QUALITY_CHANGED)
            if (
                old_point.last_status != new_point.last_status
                or old_point.status_timestamp_ms
                != new_point.status_timestamp_ms
            ):
                flags.add(STATUS_CHANGED)
            if (
                old_point.quality != "OFFLINE"
                and new_point.quality == "OFFLINE"
            ):
                flags.add(SOURCE_OFFLINE)
            if (
                old_point.quality == "OFFLINE"
                and new_point.quality == "VALID"
                and old_point.sequence != new_point.sequence
            ):
                flags.add(SOURCE_RECOVERED)

        update_count = ((current.version - self.previous.version) & 0xFFFFFFFF) // 2
        skipped_count = max(0, update_count - 1)
        if skipped_count:
            flags.add(VERSION_GAP)

        self.previous = current
        self.total_skipped_count += skipped_count
        return "OK", {
            "flags": flags,
            "skipped_count": skipped_count,
            "snapshot": current,
        }


def make_snapshot(version, sequence, quality, status, value=30501, now=1000):
    return Snapshot(
        version,
        [
            Point(value, quality, status, now, now, sequence),
            Point(79110, quality, status, now, now, sequence),
        ],
    )


class AppSnapshotConsumerTests(unittest.TestCase):
    def test_public_event_contract_carries_flags_gap_and_full_snapshot(self):
        header = HEADER_PATH.read_text(encoding="utf-8")

        for name in (
            "APP_SNAPSHOT_EVENT_INITIAL_SNAPSHOT",
            "APP_SNAPSHOT_EVENT_SNAPSHOT_CHANGED",
            "APP_SNAPSHOT_EVENT_NEW_VALID_DATA",
            "APP_SNAPSHOT_EVENT_QUALITY_CHANGED",
            "APP_SNAPSHOT_EVENT_STATUS_CHANGED",
            "APP_SNAPSHOT_EVENT_SOURCE_OFFLINE",
            "APP_SNAPSHOT_EVENT_SOURCE_RECOVERED",
            "APP_SNAPSHOT_EVENT_VERSION_GAP",
        ):
            self.assertIn(name, header)

        self.assertIn("uint32_t skipped_count;", header)
        self.assertIn("App_DataSnapshot_t snapshot;", header)
        self.assertNotIn("UART", header)
        self.assertNotIn("Modbus", header)

    def test_initial_snapshot_is_emitted_once_and_unchanged_version_is_quiet(self):
        consumer = SnapshotConsumerModel()
        initial = make_snapshot(2, 0, "NOT_AVAILABLE", "NOT_RUN")

        status, event = consumer.process(initial)
        self.assertEqual(status, "OK")
        self.assertEqual(event["flags"], {INITIAL_SNAPSHOT})

        status, event = consumer.process(initial)
        self.assertEqual(status, "OK")
        self.assertIsNone(event)

    def test_offline_event_keeps_last_measurement_without_new_data_flag(self):
        consumer = SnapshotConsumerModel()
        valid = make_snapshot(40, 12, "VALID", "OK", now=1000)
        offline = make_snapshot(46, 12, "OFFLINE", "NACK", now=4000)
        offline.points[0].timestamp_ms = 1000
        offline.points[1].timestamp_ms = 1000

        consumer.process(valid)
        _, event = consumer.process(offline)

        self.assertEqual(event["skipped_count"], 2)
        self.assertIn(VERSION_GAP, event["flags"])
        self.assertIn(QUALITY_CHANGED, event["flags"])
        self.assertIn(STATUS_CHANGED, event["flags"])
        self.assertIn(SOURCE_OFFLINE, event["flags"])
        self.assertNotIn(NEW_VALID_DATA, event["flags"])
        self.assertEqual(event["snapshot"].points[0].raw_value, 30501)
        self.assertEqual(event["snapshot"].points[0].sequence, 12)
        self.assertEqual(event["snapshot"].points[0].timestamp_ms, 1000)

    def test_recovery_is_one_composite_event_with_new_valid_data(self):
        consumer = SnapshotConsumerModel()
        offline = make_snapshot(46, 12, "OFFLINE", "NACK", now=4000)
        recovered = make_snapshot(48, 13, "VALID", "OK", value=30600, now=5000)

        consumer.process(offline)
        _, event = consumer.process(recovered)

        self.assertIn(NEW_VALID_DATA, event["flags"])
        self.assertIn(QUALITY_CHANGED, event["flags"])
        self.assertIn(STATUS_CHANGED, event["flags"])
        self.assertIn(SOURCE_RECOVERED, event["flags"])
        self.assertEqual(event["skipped_count"], 0)
        self.assertEqual(event["snapshot"].points[0].raw_value, 30600)

    def test_busy_does_not_replace_comparison_base(self):
        consumer = SnapshotConsumerModel()
        valid = make_snapshot(40, 12, "VALID", "OK")
        consumer.process(valid)

        status, event = consumer.process(None)
        self.assertEqual(status, "BUSY")
        self.assertIsNone(event)
        self.assertEqual(consumer.previous.version, 40)

        changed = make_snapshot(42, 13, "VALID", "OK", value=30600)
        _, event = consumer.process(changed)
        self.assertIn(NEW_VALID_DATA, event["flags"])

    def test_c_source_and_firmware_project_use_the_consumer(self):
        source = SOURCE_PATH.read_text(encoding="utf-8")
        main = MAIN_PATH.read_text(encoding="utf-8")
        project = PROJECT_PATH.read_text(encoding="utf-8")

        self.assertIn("App_DataService_GetSnapshot", source)
        self.assertIn("result != APP_DATA_RESULT_OK", source)
        self.assertIn("APP_SNAPSHOT_EVENT_VERSION_GAP", source)
        self.assertIn("App_SnapshotConsumer_Process", main)
        self.assertIn("g_app_snapshot_event_report", main)
        self.assertIn("App_UplinkService_Init", main)
        self.assertIn("App_UplinkService_SubmitEvent", main)
        self.assertIn("App_UplinkService_Process", main)
        self.assertNotIn("App_UART_Bringup_Process", main)
        self.assertIn("g_app_uplink_heartbeat_test_hold_collection = 0U", main)
        self.assertIn(
            "g_app_uplink_heartbeat_test_hold_collection == 0U", main
        )
        self.assertIn("app_snapshot_consumer.c", project)
        self.assertIn("app_uplink_service.c", project)


if __name__ == "__main__":
    unittest.main()
