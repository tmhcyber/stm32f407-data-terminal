import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Tools"))

from pc_rs485_ping_pong import (
    EXPECTED_RESPONSE,
    MAX_FRAME_BYTES,
    REQUEST,
    ResponseStatus,
    format_summary,
    parse_arguments,
    read_bounded_response,
    run_ping_pong,
)


class StepClock:
    def __init__(self, step=0.001):
        self.value = 0.0
        self.step = step

    def __call__(self):
        current = self.value
        self.value += self.step
        return current


class FakeSerial:
    def __init__(self, reads):
        self.reads = list(reads)
        self.events = []

    def reset_input_buffer(self):
        self.events.append(("reset", b""))

    def write(self, data):
        self.events.append(("write", bytes(data)))
        return len(data)

    def flush(self):
        self.events.append(("flush", b""))

    def read(self, size):
        self.events.append(("read", bytes([size])))
        return self.reads.pop(0) if self.reads else b""


class PcRs485PingPongTests(unittest.TestCase):
    def test_exact_pong_is_accepted(self):
        port = FakeSerial([bytes([byte]) for byte in EXPECTED_RESPONSE])
        result = read_bounded_response(port, 1.0, clock=StepClock())

        self.assertIs(result.status, ResponseStatus.PONG)
        self.assertEqual(result.frame, EXPECTED_RESPONSE)

    def test_complete_non_pong_is_unexpected(self):
        response = b"V1|NOPE\r\n"
        port = FakeSerial([response])
        result = read_bounded_response(port, 1.0, clock=StepClock())

        self.assertIs(result.status, ResponseStatus.UNEXPECTED)
        self.assertEqual(result.frame, response)

    def test_nonterminated_32_bytes_is_overlength(self):
        payload = b"X" * MAX_FRAME_BYTES
        port = FakeSerial([payload])
        result = read_bounded_response(port, 1.0, clock=StepClock())

        self.assertIs(result.status, ResponseStatus.OVERLENGTH)
        self.assertEqual(result.frame, payload)

    def test_partial_response_times_out(self):
        port = FakeSerial([b"V1|PO"])
        result = read_bounded_response(port, 0.01, clock=StepClock())

        self.assertIs(result.status, ResponseStatus.TIMEOUT)
        self.assertEqual(result.frame, b"V1|PO")

    def test_runner_is_strict_stop_and_wait(self):
        port = FakeSerial([EXPECTED_RESPONSE, EXPECTED_RESPONSE])
        stats = run_ping_pong(port, 2, 1.0, clock=StepClock())

        writes = [index for index, event in enumerate(port.events)
                  if event[0] == "write"]
        reads = [index for index, event in enumerate(port.events)
                 if event[0] == "read"]
        resets = [index for index, event in enumerate(port.events)
                  if event[0] == "reset"]
        self.assertEqual(len(writes), 2)
        self.assertEqual(len(reads), 2)
        self.assertEqual(len(resets), 2)
        self.assertLess(writes[0], reads[0])
        self.assertLess(reads[0], writes[1])
        self.assertLess(resets[1], writes[1])
        self.assertEqual(port.events[writes[0]][1], REQUEST)
        self.assertEqual(stats.request_count, 2)
        self.assertEqual(stats.pong_count, 2)
        self.assertEqual(stats.received_byte_count, 18)

    def test_cli_requires_explicit_pc_timeout_and_summary_has_counters(self):
        args = parse_arguments(
            ["--port", "COM9", "--rounds", "3", "--response-timeout-ms", "500"]
        )
        self.assertEqual(args.response_timeout_ms, 500)
        stats = run_ping_pong(
            FakeSerial([EXPECTED_RESPONSE]),
            1,
            1.0,
            clock=StepClock(),
        )
        self.assertIn("requests=1 pong=1", format_summary(stats))


if __name__ == "__main__":
    unittest.main()
