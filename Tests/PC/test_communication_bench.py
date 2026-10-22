"""Host-only campaign/transport tests; fake time and fake devices, never board evidence."""
import argparse
import ctypes as C
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Tools"))
from communication_bench import execute, run_campaign
from pc_can_request_bench import CanAdapter, Message, Pcan, Timestamp, decode_response
from pc_rs485_ping_pong import Rs485Adapter, main as rs485_main, parse_arguments, read_bounded_response, ResponseStatus


class Clock:
    def __init__(self):
        self.now = 0.0

    def __call__(self):
        return self.now

    def sleep(self, seconds):
        self.now += seconds


class Adapter:
    def __init__(self, clock, replies=None, delay=0.002):
        self.clock, self.delay = clock, delay
        self.replies = list(replies or [])
        self.sent = []
        self.quiet_error = False
        self.closed = False

    def send(self):
        self.sent.append(self.clock())

    def receive(self, timeout):
        self.clock.sleep(self.delay)
        return self.replies.pop(0) if self.replies else {"status": "ok"}

    def check_quiet(self):
        if self.quiet_error:
            raise OSError("duplicate")

    def close(self):
        self.closed = True


def campaign(adapter, **kwargs):
    return run_campaign(adapter, clock=adapter.clock, sleep=adapter.clock.sleep, **kwargs)


def frame(data=(1, 0xFF, 0x9C, 0x17, 0x70), identifier=0x322, kind=0):
    return {"id": identifier, "type": kind, "dlc": len(data), "data": list(data)}


class FakeCan:
    def __init__(self, frames):
        self.frames = list(frames)

    def read(self):
        return self.frames.pop(0) if self.frames else None


class CampaignTests(unittest.TestCase):
    def test_count_mode_is_stop_and_wait_and_has_pc_percentiles(self):
        adapter = Adapter(Clock())
        result = campaign(adapter, rounds=3)
        self.assertTrue(result["completed"])
        self.assertEqual((result["requests"], result["successes"]), (3, 3))
        self.assertAlmostEqual(result["rtt_p95_ms"], 2)
        self.assertFalse(result["board_health_verified"])
        self.assertEqual(len(adapter.sent), 3)

    def test_duration_mode_waits_the_full_window_and_uses_actual_count(self):
        adapter = Adapter(Clock())
        result = campaign(adapter, duration=1.0, interval=0.1)
        self.assertTrue(result["completed"])
        self.assertGreaterEqual(result["elapsed_s"], 1.0)
        self.assertEqual(result["requests"], len(adapter.sent))
        self.assertTrue(all(t < 1.0 for t in adapter.sent))
        self.assertTrue(result["periodic_target_met"])

    def test_timeout_stops_before_late_response_can_enter_next_round(self):
        adapter = Adapter(Clock(), [{"status": "timeout"}, {"status": "ok"}])
        result = campaign(adapter, rounds=1000)
        self.assertEqual((result["requests"], result["successes"], result["timeouts"]), (1, 0, 1))
        self.assertFalse(result["completed"])
        self.assertIsNone(result["rtt_p95_ms"])

    def test_late_periods_do_not_trigger_catchup_bursts(self):
        adapter = Adapter(Clock(), delay=0.35)
        result = campaign(adapter, rounds=3, interval=0.1)
        self.assertTrue(result["completed"])
        self.assertFalse(result["periodic_target_met"])
        self.assertGreater(result["missed_slots"], 0)
        self.assertTrue(all(b - a >= 0.35 for a, b in zip(adapter.sent, adapter.sent[1:])))

    def test_final_duplicate_invalidates_an_otherwise_successful_run(self):
        adapter = Adapter(Clock())
        original = adapter.receive
        def duplicate(timeout):
            response = original(timeout)
            adapter.quiet_error = True
            return response
        adapter.receive = duplicate
        result = campaign(adapter, rounds=1)
        self.assertEqual(result["successes"], 1)
        self.assertFalse(result["completed"])
        self.assertIn("duplicate", result["reason"])

    def test_interrupted_inflight_request_remains_in_denominator(self):
        adapter = Adapter(Clock())
        def interrupt(timeout):
            raise KeyboardInterrupt
        adapter.receive = interrupt
        result = campaign(adapter, rounds=10)
        self.assertEqual((result["requests"], result["successes"]), (1, 0))
        self.assertFalse(result["completed"])
        self.assertEqual(result["reason"], "interrupted")

    def test_write_time_consumes_the_response_deadline(self):
        adapter = Adapter(Clock())
        def slow_send():
            adapter.clock.sleep(0.6)
        adapter.send = slow_send
        result = campaign(adapter, rounds=1, timeout=0.5)
        self.assertEqual((result["requests"], result["timeouts"], result["successes"]), (1, 1, 0))

    def test_invalid_timing_and_limits_are_rejected(self):
        for kwargs in ({}, {"rounds": 0}, {"rounds": 1, "duration": 1},
                       {"duration": float("nan")}, {"rounds": 1, "interval": -1}):
            with self.subTest(kwargs=kwargs), self.assertRaises(ValueError):
                campaign(Adapter(Clock()), **kwargs)

    def test_setup_failure_is_saved_and_evidence_cannot_be_overwritten(self):
        with tempfile.TemporaryDirectory() as temp:
            args = argparse.Namespace(output=Path(temp) / "run", firmware=None, rounds=1,
                                      duration_seconds=None, interval_ms=0, response_timeout_ms=500)
            def fail(emit):
                raise OSError("device disconnected")
            with patch("communication_bench.provenance", return_value={}), patch("builtins.print"):
                self.assertEqual(execute(fail, args, "fake"), 1)
                result = json.loads((args.output / "summary.json").read_text())
                self.assertFalse(result["completed"])
                self.assertIn("device disconnected", result["reason"])
                rows = [json.loads(s) for s in (args.output / "events.jsonl").read_text().splitlines()]
                self.assertEqual(rows[-1]["event"], "summary")
                with self.assertRaises(FileExistsError):
                    execute(fail, args, "fake")


class CanAcceptanceTests(unittest.TestCase):
    def test_signed_big_endian_response_and_quality_decoding(self):
        result = decode_response(frame())
        self.assertEqual(result, {"quality": 1, "temperature_c": -1, "humidity_rh": 60})

    def test_wrong_frame_and_bad_encoding_are_rejected(self):
        invalid = [frame(identifier=0x123), frame(kind=1), frame(kind=2), frame((1, 0)),
                   frame((4, 0, 0, 0, 0)), frame((1, 0, 0, 0x27, 0x11)), frame((0, 0, 1, 0, 0))]
        malformed = frame()
        malformed["dlc"] = 8
        invalid.append(malformed)
        for row in invalid:
            with self.subTest(frame=row), self.assertRaises(ValueError):
                decode_response(row)

    def test_echo_is_not_a_response_and_duplicates_are_not_cleared(self):
        clock = Clock()
        log = []
        adapter = CanAdapter(FakeCan([frame((1,), 0x123, 0x20), frame()]),
                             lambda **row: log.append(row), clock, clock.sleep)
        self.assertEqual(adapter.receive(0.5)["status"], "ok")
        self.assertEqual(len(log), 2)
        adapter.bus.frames.append(frame())
        with self.assertRaisesRegex(RuntimeError, "duplicate"):
            adapter.check_quiet()

    def test_ack_or_empty_bus_times_out_and_nonvalid_sensor_does_not_pass(self):
        clock = Clock()
        adapter = CanAdapter(FakeCan([]), lambda **row: None, clock, clock.sleep)
        self.assertEqual(adapter.receive(0.01)["status"], "timeout")
        adapter.bus.frames.append(frame((3, 0, 0, 0, 0)))
        self.assertEqual(adapter.receive(0.5)["status"], "sensor_not_valid")

    def test_pcan_boundary_marshals_messages_and_preserves_error_status(self):
        # Exercise the actual ctypes pointer boundary, without initializing hardware.
        class Function:
            def __init__(self, callback): self.callback = callback
            def __call__(self, *args): return self.callback(*args)
        class Dll:
            def __init__(self):
                self.sent = None
                self.closed = False
                self.read_status = 0
                self.CAN_Initialize = Function(lambda *args: 0)
                self.CAN_GetStatus = Function(lambda channel: 0)
                self.CAN_GetValue = Function(self.get_value)
                self.CAN_Write = Function(self.write)
                self.CAN_Read = Function(self.read)
                self.CAN_Uninitialize = Function(self.close)
            def get_value(self, channel, parameter, pointer, length):
                C.cast(pointer, C.POINTER(C.c_uint32))[0] = 0 if parameter == 0x08 else 1
                return 0
            def write(self, channel, pointer):
                msg = C.cast(pointer, C.POINTER(Message)).contents
                self.sent = (channel, msg.ID, msg.MSGTYPE, msg.LEN, msg.DATA[0])
                return 0
            def read(self, channel, message, timestamp):
                msg = C.cast(message, C.POINTER(Message)).contents
                msg.ID, msg.MSGTYPE, msg.LEN = 0x322, 0, 5
                msg.DATA[:] = [1, 0, 100, 0x17, 0x70, 0, 0, 0]
                ts = C.cast(timestamp, C.POINTER(Timestamp)).contents
                ts.millis, ts.millis_overflow, ts.micros = 1, 1, 2
                return self.read_status
            def close(self, channel):
                self.closed = True
                return 0
        dll = Dll()
        with patch("pc_can_request_bench.sys.platform", "win32"), patch.object(C, "WinDLL", return_value=dll, create=True):
            bus = Pcan("fake.dll", 0x51)
        bus.send()
        self.assertEqual(dll.sent, (0x51, 0x123, 0, 1, 1))
        result = bus.read()
        self.assertEqual(result["device_timestamp_us"], 2 + 1000 * (1 + (1 << 32)))
        self.assertEqual(decode_response(result)["temperature_c"], 1)
        dll.read_status = 0x20
        self.assertIsNone(bus.read())
        dll.read_status = 0x60  # Empty plus overrun must not become a harmless empty read.
        with self.assertRaises(OSError):
            bus.read()
        bus.close()
        self.assertTrue(dll.closed)


class SerialEvidenceTests(unittest.TestCase):
    def test_cli_writes_real_exchange_log_and_closes_port(self):
        class Port:
            def __init__(self):
                self.pending = bytearray()
                self.closed = False
            @property
            def in_waiting(self): return len(self.pending)
            def write(self, data):
                self.pending.extend(b"V1|PONG\r\n")
                return len(data)
            def flush(self): pass
            def read(self, size):
                result = bytes(self.pending[:size])
                del self.pending[:size]
                return result
            def close(self): self.closed = True
        port = Port()
        with tempfile.TemporaryDirectory() as temp:
            destination = Path(temp) / "one_round"
            with patch("pc_rs485_ping_pong.open_serial_port", return_value=port), patch("communication_bench.provenance", return_value={}), patch("builtins.print"):
                code = rs485_main(["--port", "FAKE", "--rounds", "1", "--response-timeout-ms", "500",
                                   "--output", str(destination)])
            self.assertEqual(code, 0)
            self.assertTrue(port.closed)
            summary = json.loads((destination / "summary.json").read_text())
            self.assertEqual((summary["requests"], summary["successes"]), (1, 1))
            rows = [json.loads(line) for line in (destination / "events.jsonl").read_text().splitlines()]
            self.assertEqual(bytes.fromhex(next(row["hex"] for row in rows if row["event"] == "tx")), b"V1|PING\r\n")
            self.assertEqual(b"".join(bytes.fromhex(row["hex"]) for row in rows if row["event"] == "rx"), b"V1|PONG\r\n")

    def test_partial_write_and_unsolicited_bytes_are_visible_failures(self):
        class Port:
            in_waiting = 0
            def write(self, data): return 2
            def read(self, size): return b"extra"
        port = Port()
        log = []
        adapter = Rs485Adapter(port, lambda **row: log.append(row))
        with self.assertRaisesRegex(OSError, "partial"):
            adapter.send()
        port.in_waiting = 5
        with self.assertRaisesRegex(RuntimeError, "duplicate"):
            adapter.check_quiet()
        self.assertEqual([row["event"] for row in log], ["tx", "extra_rx"])

    def test_reply_arriving_after_deadline_is_not_success(self):
        clock = Clock()
        class Port:
            def read(self, size):
                clock.sleep(1)
                return b"V1|PONG\r\n"
        self.assertIs(read_bounded_response(Port(), 0.5, clock).status, ResponseStatus.TIMEOUT)

    def test_cli_selects_duration_without_default_round_limit(self):
        args = parse_arguments(["--port", "COM9", "--duration-seconds", "3600",
                                "--interval-ms", "100", "--response-timeout-ms", "500"])
        self.assertIsNone(args.rounds)
        self.assertEqual(args.duration_seconds, 3600)


if __name__ == "__main__":
    unittest.main()
