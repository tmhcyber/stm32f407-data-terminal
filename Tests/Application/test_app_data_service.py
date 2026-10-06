import copy
import re
import unittest
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HEADER_PATH = ROOT / "Application" / "Inc" / "app_data_service.h"
SOURCE_PATH = ROOT / "Application" / "Src" / "app_data_service.c"
COLLECTOR_PATH = ROOT / "Application" / "Src" / "app_sht30_collector.c"
CONSUMER_PATH = ROOT / "Application" / "Src" / "app_snapshot_consumer.c"
MAIN_PATH = ROOT / "Core" / "Src" / "main.c"
PROJECT_PATH = ROOT / "MDK-ARM" / "IndustrialTerminal.uvprojx"


@dataclass
class Point:
    source_id: str
    point_id: str
    raw_value: int = 0
    scale: int = -3
    unit: str = "NONE"
    quality: str = "NOT_AVAILABLE"
    last_status: str = "NOT_RUN"
    timestamp_ms: int = 0
    status_timestamp_ms: int = 0
    sequence: int = 0


class VersionedDataServiceModel:
    def __init__(self):
        self.version = 2
        self.next_sequence = 0
        self.points = [
            Point("SHT30", "TEMPERATURE", unit="DEGREE_CELSIUS"),
            Point("SHT30", "HUMIDITY", unit="PERCENT_RH"),
        ]

    def _begin_write(self):
        self.version += 1
        if self.version % 2 != 1:
            raise AssertionError("write must begin on an odd version")

    def _end_write(self):
        self.version += 1
        if self.version % 2 != 0:
            raise AssertionError("write must end on an even version")

    def publish_sht30(self, temperature, humidity, timestamp_ms):
        self.next_sequence += 1
        self._begin_write()
        for point, value in zip(self.points, (temperature, humidity)):
            point.raw_value = value
            point.quality = "VALID"
            point.last_status = "OK"
            point.timestamp_ms = timestamp_ms
            point.status_timestamp_ms = timestamp_ms
            point.sequence = self.next_sequence
        self._end_write()

    def update_quality(self, quality, status, status_timestamp_ms):
        self._begin_write()
        for point in self.points:
            point.quality = quality
            point.last_status = status
            point.status_timestamp_ms = status_timestamp_ms
        self._end_write()

    def copy_snapshot(self, after_first_copy=None):
        version_before = self.version
        if version_before % 2 != 0:
            return None

        copied = [copy.deepcopy(self.points[0])]
        if after_first_copy is not None:
            after_first_copy()
        copied.append(copy.deepcopy(self.points[1]))

        version_after = self.version
        if version_before != version_after or version_after % 2 != 0:
            return None
        return version_after, copied


class AppDataServiceTests(unittest.TestCase):
    def test_public_record_contains_required_fields_without_writable_storage(self):
        header = HEADER_PATH.read_text(encoding="utf-8")
        for field in (
            "source_id",
            "point_id",
            "raw_value",
            "scale",
            "unit",
            "quality",
            "timestamp_ms",
            "sequence",
        ):
            self.assertRegex(header, rf"\b{field}\b")

        self.assertIn("App_DataService_GetSnapshot", header)
        self.assertNotRegex(header, r"extern\s+.*\bs_points\b")

    def test_success_batch_publishes_two_points_with_one_sequence(self):
        model = VersionedDataServiceModel()
        model.publish_sht30(23567, 48321, 1000)

        self.assertEqual(model.version, 4)
        self.assertEqual([point.sequence for point in model.points], [1, 1])
        self.assertEqual(
            [point.timestamp_ms for point in model.points], [1000, 1000]
        )
        self.assertEqual(
            [point.quality for point in model.points], ["VALID", "VALID"]
        )

    def test_quality_change_preserves_value_timestamp_and_sequence(self):
        model = VersionedDataServiceModel()
        model.publish_sht30(23567, 48321, 1000)
        before = copy.deepcopy(model.points)

        model.update_quality("STALE", "TEMPERATURE_CRC_ERROR", 1100)

        self.assertEqual(model.version, 6)
        for old, current in zip(before, model.points):
            self.assertEqual(current.raw_value, old.raw_value)
            self.assertEqual(current.timestamp_ms, old.timestamp_ms)
            self.assertEqual(current.sequence, old.sequence)
            self.assertEqual(current.quality, "STALE")
            self.assertEqual(current.status_timestamp_ms, 1100)

    def test_reader_rejects_mixed_snapshot_when_version_changes(self):
        model = VersionedDataServiceModel()
        model.publish_sht30(20000, 40000, 1000)

        snapshot = model.copy_snapshot(
            after_first_copy=lambda: model.publish_sht30(21000, 50000, 2000)
        )

        self.assertIsNone(snapshot)
        stable_snapshot = model.copy_snapshot()
        self.assertIsNotNone(stable_snapshot)
        self.assertEqual(
            [point.raw_value for point in stable_snapshot[1]], [21000, 50000]
        )

    def test_c_source_uses_odd_even_protocol_and_bounded_retry(self):
        source = SOURCE_PATH.read_text(encoding="utf-8")
        header = HEADER_PATH.read_text(encoding="utf-8")

        self.assertGreaterEqual(source.count("++s_snapshot_version;"), 2)
        self.assertIn("version_before & 1U", source)
        self.assertIn("version_before == version_after", source)
        self.assertIn("APP_DATA_SNAPSHOT_MAX_RETRIES", source)
        self.assertRegex(
            header,
            r"#define\s+APP_DATA_SNAPSHOT_MAX_RETRIES\s+3U",
        )

    def test_collector_consumer_and_keil_project_include_data_service(self):
        collector = COLLECTOR_PATH.read_text(encoding="utf-8")
        consumer = CONSUMER_PATH.read_text(encoding="utf-8")
        main = MAIN_PATH.read_text(encoding="utf-8")
        project = PROJECT_PATH.read_text(encoding="utf-8")

        self.assertIn("App_DataService_PublishValues", collector)
        self.assertIn("App_DataService_UpdateSourceQuality", collector)
        self.assertIn("App_DataService_GetSnapshot", consumer)
        self.assertIn("App_DataService_Init();", main)
        self.assertIn("App_SnapshotConsumer_Process", main)
        self.assertIn("g_app_snapshot_event_report_version", main)
        self.assertIn("app_data_service.c", project)
        self.assertIn("app_snapshot_consumer.c", project)


if __name__ == "__main__":
    unittest.main()
