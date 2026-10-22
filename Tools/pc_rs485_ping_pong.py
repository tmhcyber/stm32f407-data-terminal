"""Run the bounded USART2/RS485 stage-one PING/PONG experiment."""

import argparse
import sys
import time
from dataclasses import dataclass
from enum import Enum, auto

from communication_bench import (
    add_campaign_arguments, execute, positive, validate_campaign_arguments,
)


REQUEST = b"V1|PING\r\n"
EXPECTED_RESPONSE = b"V1|PONG\r\n"
MAX_FRAME_BYTES = 32
BAUDRATE = 115200
SERIAL_READ_TIMEOUT_SECONDS = 0.01


class ResponseStatus(Enum):
    PONG = auto()
    UNEXPECTED = auto()
    OVERLENGTH = auto()
    TIMEOUT = auto()


@dataclass(frozen=True)
class ResponseResult:
    status: ResponseStatus
    frame: bytes


@dataclass
class LoopbackStats:
    request_count: int = 0
    pong_count: int = 0
    unexpected_count: int = 0
    overlength_count: int = 0
    timeout_count: int = 0
    received_byte_count: int = 0


def read_bounded_response(serial_port, response_timeout_seconds, clock=None):
    if response_timeout_seconds <= 0:
        raise ValueError("response_timeout_seconds must be positive")
    if clock is None:
        clock = time.monotonic

    deadline = clock() + response_timeout_seconds
    frame = bytearray()
    while clock() < deadline:
        chunk = serial_port.read(1)
        if not chunk:
            continue
        if clock() >= deadline:
            return ResponseResult(ResponseStatus.TIMEOUT, bytes(frame) + chunk)

        for byte in chunk:
            frame.append(byte)
            if frame.endswith(b"\r\n"):
                status = (
                    ResponseStatus.PONG
                    if bytes(frame) == EXPECTED_RESPONSE
                    else ResponseStatus.UNEXPECTED
                )
                return ResponseResult(status, bytes(frame))
            if len(frame) >= MAX_FRAME_BYTES:
                return ResponseResult(ResponseStatus.OVERLENGTH, bytes(frame))

    return ResponseResult(ResponseStatus.TIMEOUT, bytes(frame))


def run_ping_pong(serial_port, rounds, response_timeout_seconds, clock=None):
    if rounds <= 0:
        raise ValueError("rounds must be positive")

    stats = LoopbackStats()
    for _ in range(rounds):
        # A new stop-and-wait round never inherits late bytes from a previous
        # timed-out or malformed response.
        serial_port.reset_input_buffer()
        serial_port.write(REQUEST)
        serial_port.flush()
        stats.request_count += 1

        result = read_bounded_response(
            serial_port,
            response_timeout_seconds,
            clock=clock,
        )
        stats.received_byte_count += len(result.frame)
        if result.status is ResponseStatus.PONG:
            stats.pong_count += 1
        elif result.status is ResponseStatus.UNEXPECTED:
            stats.unexpected_count += 1
        elif result.status is ResponseStatus.OVERLENGTH:
            stats.overlength_count += 1
        else:
            stats.timeout_count += 1
        if result.status is not ResponseStatus.PONG:
            break

    return stats


def open_serial_port(port):
    try:
        import serial
    except ImportError as exc:
        raise RuntimeError(
            "pyserial is required: python -m pip install pyserial"
        ) from exc

    return serial.Serial(
        port=port,
        baudrate=BAUDRATE,
        bytesize=serial.EIGHTBITS,
        parity=serial.PARITY_NONE,
        stopbits=serial.STOPBITS_ONE,
        timeout=SERIAL_READ_TIMEOUT_SECONDS,
        write_timeout=1.0,
    )


def parse_arguments(argv=None):
    parser = argparse.ArgumentParser(
        description="Send one bounded PING at a time and verify PONG."
    )
    parser.add_argument("--port", required=True, help="USB-RS485 COM port")
    add_campaign_arguments(parser, default_rounds=10)
    parser.add_argument(
        "--response-timeout-ms",
        type=positive,
        required=True,
        help="PC-side PONG wait; choose and record it for the experiment",
    )
    args = parser.parse_args(argv)
    return validate_campaign_arguments(parser, args)


def format_summary(stats):
    return (
        f"requests={stats.request_count} pong={stats.pong_count} "
        f"unexpected={stats.unexpected_count} "
        f"overlength={stats.overlength_count} "
        f"timeouts={stats.timeout_count} "
        f"rx_bytes={stats.received_byte_count}"
    )


class Rs485Adapter:
    def __init__(self, port, emit):
        self.port = port
        self.emit = emit
        # Pre-window bytes are logged, not included in the formal denominator.
        waiting = port.in_waiting
        if waiting:
            emit(event="startup_rx", hex=port.read(min(waiting, 4096)).hex())
        if port.in_waiting:
            raise RuntimeError("continuous startup input; check other senders")

    def check_quiet(self):
        waiting = self.port.in_waiting
        if waiting:
            raw = self.port.read(min(waiting, 4096))
            self.emit(event="extra_rx", hex=raw.hex())
            raise RuntimeError("unsolicited/duplicate response between requests")

    def send(self):
        written = self.port.write(REQUEST)
        self.emit(event="tx", hex=REQUEST.hex(), written=written)
        if written != len(REQUEST):
            raise OSError("partial serial write")
        self.port.flush()

    def receive(self, timeout):
        # Log every nonempty read, including a partial frame on timeout/error.
        adapter = self

        class LoggedReader:
            def read(self, size):
                raw = adapter.port.read(size)
                if raw:
                    adapter.emit(event="rx", hex=raw.hex())
                return raw

        result = read_bounded_response(LoggedReader(), timeout)
        return {"status": "ok" if result.status is ResponseStatus.PONG else result.status.name.lower(),
                "response_hex": result.frame.hex()}

    def close(self):
        self.port.close()


def main(argv=None):
    args = parse_arguments(argv)

    def create(emit):
        port = open_serial_port(args.port)
        try:
            return Rs485Adapter(port, emit)
        except BaseException:
            port.close()
            raise

    try:
        return execute(create, args, "rs485")
    except (OSError, RuntimeError, ValueError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
