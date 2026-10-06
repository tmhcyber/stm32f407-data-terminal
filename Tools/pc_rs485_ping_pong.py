"""Run the bounded USART2/RS485 stage-one PING/PONG experiment."""

import argparse
import sys
import time
from dataclasses import dataclass
from enum import Enum, auto


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
    parser.add_argument("--rounds", type=int, default=10)
    parser.add_argument(
        "--response-timeout-ms",
        type=int,
        required=True,
        help="PC-side PONG wait; choose and record it for the experiment",
    )
    args = parser.parse_args(argv)
    if args.rounds <= 0:
        parser.error("--rounds must be positive")
    if args.response_timeout_ms <= 0:
        parser.error("--response-timeout-ms must be positive")
    return args


def format_summary(stats):
    return (
        f"requests={stats.request_count} pong={stats.pong_count} "
        f"unexpected={stats.unexpected_count} "
        f"overlength={stats.overlength_count} "
        f"timeouts={stats.timeout_count} "
        f"rx_bytes={stats.received_byte_count}"
    )


def main(argv=None):
    args = parse_arguments(argv)
    try:
        with open_serial_port(args.port) as serial_port:
            stats = run_ping_pong(
                serial_port,
                args.rounds,
                args.response_timeout_ms / 1000.0,
            )
    except (OSError, RuntimeError, ValueError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2

    print(format_summary(stats))
    return 0 if stats.pong_count == stats.request_count else 1


if __name__ == "__main__":
    raise SystemExit(main())
