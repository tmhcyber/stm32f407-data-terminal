"""Receive and validate the teaching MVP's read-only serial uplink."""

import argparse
import re
import sys
import time
from dataclasses import dataclass, field
from decimal import Decimal
from enum import Enum, auto


CRLF = b"\r\n"
DEFAULT_MAX_PAYLOAD_BYTES = 509
SERIAL_BAUDRATE = 115200
SERIAL_READ_SIZE = 256
SERIAL_TIMEOUT_SECONDS = 0.2
UINT32_MAX = (1 << 32) - 1
INT32_MIN = -(1 << 31)
INT32_MAX = (1 << 31) - 1
KNOWN_EVENT_FLAGS = 0x000000FF

HEADER_FIELDS = frozenset(
    ("v", "reason", "flags", "gap", "coal", "snap", "n")
)
POINT_FIELD_SUFFIXES = (
    "src",
    "id",
    "value",
    "scale",
    "unit",
    "quality",
    "status",
    "value_ms",
    "status_ms",
    "seq",
)
EXPECTED_FIELDS = HEADER_FIELDS | frozenset(
    f"p{index}.{suffix}"
    for index in range(2)
    for suffix in POINT_FIELD_SUFFIXES
)

REASONS = frozenset(("EVENT", "HEARTBEAT"))
QUALITIES = frozenset(("NOT_AVAILABLE", "VALID", "STALE", "OFFLINE"))
STATUSES = frozenset(
    (
        "NOT_RUN",
        "OK",
        "INVALID_ARGUMENT",
        "NACK",
        "TIMEOUT",
        "BUSY",
        "BUS_ERROR",
        "TEMPERATURE_CRC_ERROR",
        "HUMIDITY_CRC_ERROR",
    )
)

_UNSIGNED_DECIMAL_RE = re.compile(r"(?:0|[1-9][0-9]*)\Z")
_SIGNED_DECIMAL_RE = re.compile(r"(?:0|-[1-9][0-9]*|[1-9][0-9]*)\Z")
_FLAGS_RE = re.compile(r"0x[0-9A-F]{8}\Z")


class FramerState(Enum):
    SEEK_FIRST_CRLF = auto()
    SYNCED = auto()
    DISCARD_UNTIL_CRLF = auto()


class ProtocolErrorCode(Enum):
    NON_ASCII = auto()
    SYNTAX = auto()
    DUPLICATE_FIELD = auto()
    SCHEMA = auto()
    VALUE_FORMAT = auto()
    VALUE_RANGE = auto()
    UNSUPPORTED_VALUE = auto()
    SEMANTIC = auto()


class ProtocolError(ValueError):
    def __init__(self, code, message):
        super().__init__(message)
        self.code = code


@dataclass
class FramerStats:
    chunks_received: int = 0
    bytes_received: int = 0
    complete_line_count: int = 0
    framing_error_count: int = 0
    startup_discard_count: int = 0
    resync_count: int = 0
    discarded_byte_count: int = 0


@dataclass(frozen=True)
class UplinkPoint:
    source: str
    point_id: str
    value: int
    scale: int
    unit: str
    quality: str
    status: str
    value_ms: int
    status_ms: int
    sequence: int

    @property
    def scaled_value(self):
        return Decimal(self.value).scaleb(self.scale)

    @property
    def is_current(self):
        return self.quality == "VALID" and self.status == "OK"


@dataclass(frozen=True)
class UplinkRecord:
    version: int
    reason: str
    flags: int
    gap: int
    coalesced_count: int
    snapshot_version: int
    points: tuple


@dataclass(frozen=True)
class RejectedLine:
    raw_line: bytes
    code: ProtocolErrorCode
    message: str


@dataclass(frozen=True)
class ProcessBatch:
    records: tuple
    rejected_lines: tuple


@dataclass
class ProcessorStats:
    valid_record_count: int = 0
    parse_error_count: int = 0
    observed_snapshot_change_count: int = 0
    parse_error_counts: dict = field(default_factory=dict)


@dataclass(frozen=True)
class ReceiverRunResult:
    processor: object
    elapsed_seconds: float


class CrlfLineFramer:
    """Turn arbitrary byte chunks into bounded CRLF-delimited payloads."""

    def __init__(self, max_payload_bytes=DEFAULT_MAX_PAYLOAD_BYTES):
        if not isinstance(max_payload_bytes, int):
            raise TypeError("max_payload_bytes must be an integer")
        if max_payload_bytes < 0:
            raise ValueError("max_payload_bytes must not be negative")

        self.max_payload_bytes = max_payload_bytes
        self.state = FramerState.SEEK_FIRST_CRLF
        self.stats = FramerStats()
        self._buffer = bytearray()

    def feed(self, chunk):
        if not isinstance(chunk, (bytes, bytearray, memoryview)):
            raise TypeError("chunk must be bytes-like")

        data = bytes(chunk)
        if not data:
            return []

        self.stats.chunks_received += 1
        self.stats.bytes_received += len(data)
        self._buffer.extend(data)

        lines = []
        while True:
            if self.state == FramerState.SEEK_FIRST_CRLF:
                if not self._finish_startup_discard_if_possible():
                    break
                continue

            if self.state == FramerState.DISCARD_UNTIL_CRLF:
                if not self._finish_error_resync_if_possible():
                    break
                continue

            delimiter_index = self._buffer.find(CRLF)
            if delimiter_index >= 0:
                if delimiter_index > self.max_payload_bytes:
                    self.stats.framing_error_count += 1
                    self.stats.discarded_byte_count += delimiter_index + len(CRLF)
                    del self._buffer[: delimiter_index + len(CRLF)]
                    self.stats.resync_count += 1
                    continue

                lines.append(bytes(self._buffer[:delimiter_index]))
                del self._buffer[: delimiter_index + len(CRLF)]
                self.stats.complete_line_count += 1
                continue

            payload_length = len(self._buffer)
            if self._buffer.endswith(b"\r"):
                payload_length -= 1

            if payload_length > self.max_payload_bytes:
                self.stats.framing_error_count += 1
                self.state = FramerState.DISCARD_UNTIL_CRLF
                self._discard_bytes_without_losing_crlf_prefix()
            break

        return lines

    def _finish_startup_discard_if_possible(self):
        delimiter_index = self._buffer.find(CRLF)
        if delimiter_index < 0:
            self._discard_bytes_without_losing_crlf_prefix()
            return False

        discard_length = delimiter_index + len(CRLF)
        self.stats.discarded_byte_count += discard_length
        del self._buffer[:discard_length]
        self.stats.startup_discard_count += 1
        self.state = FramerState.SYNCED
        return True

    def _finish_error_resync_if_possible(self):
        delimiter_index = self._buffer.find(CRLF)
        if delimiter_index < 0:
            self._discard_bytes_without_losing_crlf_prefix()
            return False

        discard_length = delimiter_index + len(CRLF)
        self.stats.discarded_byte_count += discard_length
        del self._buffer[:discard_length]
        self.stats.resync_count += 1
        self.state = FramerState.SYNCED
        return True

    def _discard_bytes_without_losing_crlf_prefix(self):
        keep_length = 1 if self._buffer.endswith(b"\r") else 0
        discard_length = len(self._buffer) - keep_length
        self.stats.discarded_byte_count += discard_length
        if discard_length:
            del self._buffer[:discard_length]


class UplinkStreamProcessor:
    """Compose framing and parsing while keeping PC-owned counters."""

    def __init__(self, max_payload_bytes=DEFAULT_MAX_PAYLOAD_BYTES):
        self.framer = CrlfLineFramer(max_payload_bytes=max_payload_bytes)
        self.stats = ProcessorStats()
        self._last_snapshot_version = None

    def process(self, chunk):
        records = []
        rejected_lines = []

        for line in self.framer.feed(chunk):
            try:
                record = parse_uplink_line(line)
            except ProtocolError as exc:
                self.stats.parse_error_count += 1
                current_count = self.stats.parse_error_counts.get(exc.code, 0)
                self.stats.parse_error_counts[exc.code] = current_count + 1
                rejected_lines.append(
                    RejectedLine(
                        raw_line=line,
                        code=exc.code,
                        message=str(exc),
                    )
                )
                continue

            self.stats.valid_record_count += 1
            if (
                self._last_snapshot_version is not None
                and record.snapshot_version != self._last_snapshot_version
            ):
                self.stats.observed_snapshot_change_count += 1
            self._last_snapshot_version = record.snapshot_version
            records.append(record)

        return ProcessBatch(
            records=tuple(records),
            rejected_lines=tuple(rejected_lines),
        )


def parse_uplink_line(line):
    """Parse one CRLF-stripped protocol-version-1 snapshot line."""
    if not isinstance(line, (bytes, bytearray, memoryview)):
        raise TypeError("line must be bytes-like")

    try:
        text = bytes(line).decode("ascii")
    except UnicodeDecodeError as exc:
        raise ProtocolError(
            ProtocolErrorCode.NON_ASCII,
            "line is not ASCII",
        ) from exc

    fields = _parse_key_value_fields(text)
    _validate_field_set(fields)

    version = _parse_uint32(fields, "v")
    if version != 1:
        raise ProtocolError(
            ProtocolErrorCode.UNSUPPORTED_VALUE,
            f"unsupported protocol version: {version}",
        )

    reason = _parse_enum(fields, "reason", REASONS)
    flags = _parse_flags(fields["flags"])
    gap = _parse_uint32(fields, "gap")
    coalesced_count = _parse_uint32(fields, "coal")
    snapshot_version = _parse_uint32(fields, "snap")
    point_count = _parse_uint32(fields, "n")

    if flags & ~KNOWN_EVENT_FLAGS:
        raise ProtocolError(
            ProtocolErrorCode.UNSUPPORTED_VALUE,
            f"flags contain unknown bits: {fields['flags']}",
        )
    if snapshot_version & 1:
        raise ProtocolError(
            ProtocolErrorCode.SEMANTIC,
            "snapshot version must be even",
        )
    if point_count != 2:
        raise ProtocolError(
            ProtocolErrorCode.SEMANTIC,
            "protocol version 1 requires exactly two points",
        )

    if reason == "EVENT" and flags == 0:
        raise ProtocolError(
            ProtocolErrorCode.SEMANTIC,
            "EVENT requires non-zero flags",
        )
    if reason == "HEARTBEAT" and (flags != 0 or gap != 0):
        raise ProtocolError(
            ProtocolErrorCode.SEMANTIC,
            "HEARTBEAT requires zero flags and zero gap",
        )

    points = (
        _parse_point(fields, 0),
        _parse_point(fields, 1),
    )
    _validate_points_are_one_sht30_batch(points)

    return UplinkRecord(
        version=version,
        reason=reason,
        flags=flags,
        gap=gap,
        coalesced_count=coalesced_count,
        snapshot_version=snapshot_version,
        points=points,
    )


def _parse_key_value_fields(text):
    if not text or "\r" in text or "\n" in text or not text.endswith(";"):
        raise ProtocolError(
            ProtocolErrorCode.SYNTAX,
            "line must be non-empty, CRLF-stripped, and end with ';'",
        )

    segments = text.split(";")
    if segments[-1] != "":
        raise ProtocolError(ProtocolErrorCode.SYNTAX, "missing final ';'")
    segments.pop()

    fields = {}
    for segment in segments:
        if not segment or segment.count("=") != 1:
            raise ProtocolError(
                ProtocolErrorCode.SYNTAX,
                "each field must contain exactly one '='",
            )
        key, value = segment.split("=", 1)
        if not key or not value:
            raise ProtocolError(
                ProtocolErrorCode.SYNTAX,
                "field key and value must be non-empty",
            )
        if key in fields:
            raise ProtocolError(
                ProtocolErrorCode.DUPLICATE_FIELD,
                f"duplicate field: {key}",
            )
        fields[key] = value
    return fields


def _validate_field_set(fields):
    actual_fields = frozenset(fields)
    missing = sorted(EXPECTED_FIELDS - actual_fields)
    unknown = sorted(actual_fields - EXPECTED_FIELDS)
    if missing or unknown:
        details = []
        if missing:
            details.append("missing=" + ",".join(missing))
        if unknown:
            details.append("unknown=" + ",".join(unknown))
        raise ProtocolError(
            ProtocolErrorCode.SCHEMA,
            "invalid field set: " + "; ".join(details),
        )


def _parse_uint32(fields, key):
    text = fields[key]
    if _UNSIGNED_DECIMAL_RE.fullmatch(text) is None:
        raise ProtocolError(
            ProtocolErrorCode.VALUE_FORMAT,
            f"{key} must be a canonical unsigned decimal integer",
        )
    value = int(text, 10)
    if value > UINT32_MAX:
        raise ProtocolError(
            ProtocolErrorCode.VALUE_RANGE,
            f"{key} is outside uint32 range",
        )
    return value


def _parse_int(fields, key, minimum, maximum):
    text = fields[key]
    if _SIGNED_DECIMAL_RE.fullmatch(text) is None:
        raise ProtocolError(
            ProtocolErrorCode.VALUE_FORMAT,
            f"{key} must be a canonical signed decimal integer",
        )
    value = int(text, 10)
    if value < minimum or value > maximum:
        raise ProtocolError(
            ProtocolErrorCode.VALUE_RANGE,
            f"{key} is outside the allowed integer range",
        )
    return value


def _parse_flags(text):
    if _FLAGS_RE.fullmatch(text) is None:
        raise ProtocolError(
            ProtocolErrorCode.VALUE_FORMAT,
            "flags must use 0x followed by eight uppercase hexadecimal digits",
        )
    return int(text[2:], 16)


def _parse_enum(fields, key, allowed):
    value = fields[key]
    if value not in allowed:
        raise ProtocolError(
            ProtocolErrorCode.UNSUPPORTED_VALUE,
            f"unsupported {key}: {value}",
        )
    return value


def _parse_point(fields, index):
    prefix = f"p{index}."
    source = _parse_enum(fields, prefix + "src", frozenset(("SHT30",)))
    point_id = _parse_enum(
        fields,
        prefix + "id",
        frozenset(("TEMPERATURE", "HUMIDITY")),
    )
    value = _parse_int(fields, prefix + "value", INT32_MIN, INT32_MAX)
    scale = _parse_int(fields, prefix + "scale", -128, 127)
    unit = _parse_enum(
        fields,
        prefix + "unit",
        frozenset(("DEG_C", "PERCENT_RH")),
    )
    quality = _parse_enum(fields, prefix + "quality", QUALITIES)
    status = _parse_enum(fields, prefix + "status", STATUSES)
    value_ms = _parse_uint32(fields, prefix + "value_ms")
    status_ms = _parse_uint32(fields, prefix + "status_ms")
    sequence = _parse_uint32(fields, prefix + "seq")

    expected_id = "TEMPERATURE" if index == 0 else "HUMIDITY"
    expected_unit = "DEG_C" if index == 0 else "PERCENT_RH"
    if point_id != expected_id or unit != expected_unit or scale != -3:
        raise ProtocolError(
            ProtocolErrorCode.SEMANTIC,
            f"p{index} identity, unit, or scale does not match protocol version 1",
        )

    if index == 0 and not -45000 <= value <= 130000:
        raise ProtocolError(
            ProtocolErrorCode.VALUE_RANGE,
            "temperature value is outside the SHT30 conversion range",
        )
    if index == 1 and not 0 <= value <= 100000:
        raise ProtocolError(
            ProtocolErrorCode.VALUE_RANGE,
            "humidity value is outside the SHT30 conversion range",
        )

    _validate_quality_status_pair(index, quality, status)
    return UplinkPoint(
        source=source,
        point_id=point_id,
        value=value,
        scale=scale,
        unit=unit,
        quality=quality,
        status=status,
        value_ms=value_ms,
        status_ms=status_ms,
        sequence=sequence,
    )


def _validate_quality_status_pair(index, quality, status):
    valid = (
        (quality == "NOT_AVAILABLE" and status == "NOT_RUN")
        or (quality == "VALID" and status == "OK")
        or (
            quality in ("STALE", "OFFLINE")
            and status not in ("NOT_RUN", "OK")
        )
    )
    if not valid:
        raise ProtocolError(
            ProtocolErrorCode.SEMANTIC,
            f"p{index} quality/status combination is invalid",
        )


def _validate_points_are_one_sht30_batch(points):
    first, second = points
    shared_values = (
        (first.quality, second.quality, "quality"),
        (first.status, second.status, "status"),
        (first.value_ms, second.value_ms, "value_ms"),
        (first.status_ms, second.status_ms, "status_ms"),
        (first.sequence, second.sequence, "seq"),
    )
    for first_value, second_value, name in shared_values:
        if first_value != second_value:
            raise ProtocolError(
                ProtocolErrorCode.SEMANTIC,
                f"SHT30 points disagree on {name}",
            )


def format_record(record):
    lines = [
        (
            f"RECORD reason={record.reason} flags=0x{record.flags:08X} "
            f"gap={record.gap} coal={record.coalesced_count} "
            f"snap={record.snapshot_version}"
        )
    ]
    for point in record.points:
        if point.is_current:
            value_kind = "current"
        elif point.quality in ("STALE", "OFFLINE"):
            value_kind = "last_valid"
        else:
            value_kind = "not_available"
        lines.append(
            (
                f"  {point.point_id}={point.scaled_value} {point.unit} "
                f"value_kind={value_kind} quality={point.quality} "
                f"status={point.status} value_ms={point.value_ms} "
                f"status_ms={point.status_ms} seq={point.sequence}"
            )
        )
    return "\n".join(lines)


def format_rejected_line(rejected):
    return (
        f"PARSE_ERROR code={rejected.code.name} "
        f"message={rejected.message} raw={rejected.raw_line!r}"
    )


def format_summary(processor, elapsed_seconds):
    framer = processor.framer.stats
    parser = processor.stats
    error_counts = ",".join(
        f"{code.name}:{count}"
        for code, count in sorted(
            parser.parse_error_counts.items(),
            key=lambda item: item[0].name,
        )
    )
    if not error_counts:
        error_counts = "none"

    return (
        "SUMMARY "
        f"elapsed_s={elapsed_seconds:.3f} "
        f"chunks={framer.chunks_received} bytes={framer.bytes_received} "
        f"complete_lines={framer.complete_line_count} "
        f"valid_records={parser.valid_record_count} "
        f"parse_errors={parser.parse_error_count} "
        f"framing_errors={framer.framing_error_count} "
        f"startup_discards={framer.startup_discard_count} "
        f"resyncs={framer.resync_count} "
        f"discarded_bytes={framer.discarded_byte_count} "
        f"snapshot_changes={parser.observed_snapshot_change_count} "
        f"parse_error_codes={error_counts}"
    )


def run_receive_loop(
    serial_port,
    *,
    duration_seconds=0.0,
    output=print,
    clock=time.monotonic,
):
    if duration_seconds < 0:
        raise ValueError("duration_seconds must not be negative")

    processor = UplinkStreamProcessor()
    started_at = clock()
    try:
        while True:
            if duration_seconds > 0 and clock() - started_at >= duration_seconds:
                break

            chunk = serial_port.read(SERIAL_READ_SIZE)
            if not chunk:
                continue

            batch = processor.process(chunk)
            for record in batch.records:
                output(format_record(record))
            for rejected in batch.rejected_lines:
                output(format_rejected_line(rejected))
    except KeyboardInterrupt:
        pass

    elapsed_seconds = clock() - started_at
    output(format_summary(processor, elapsed_seconds))
    return ReceiverRunResult(
        processor=processor,
        elapsed_seconds=elapsed_seconds,
    )


def open_serial_port(port):
    import serial

    return serial.Serial(
        port=port,
        baudrate=SERIAL_BAUDRATE,
        bytesize=serial.EIGHTBITS,
        parity=serial.PARITY_NONE,
        stopbits=serial.STOPBITS_ONE,
        timeout=SERIAL_TIMEOUT_SECONDS,
    )


def list_serial_ports():
    from serial.tools import list_ports

    return tuple(
        (port.device, port.description)
        for port in list_ports.comports()
    )


def _non_negative_float(text):
    try:
        value = float(text)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("must be a number") from exc
    if value < 0:
        raise argparse.ArgumentTypeError("must not be negative")
    return value


def parse_arguments(argv=None):
    parser = argparse.ArgumentParser(
        description="Receive and validate the STM32 teaching MVP serial uplink."
    )
    selection = parser.add_mutually_exclusive_group(required=True)
    selection.add_argument(
        "--port",
        help="Windows COM port, for example COM8; never assumed by the tool",
    )
    selection.add_argument(
        "--list-ports",
        action="store_true",
        help="list currently enumerated serial ports and exit",
    )
    parser.add_argument(
        "--duration-seconds",
        type=_non_negative_float,
        default=0.0,
        help="observation duration; zero means run until Ctrl+C",
    )
    return parser.parse_args(argv)


def main(argv=None):
    args = parse_arguments(argv)
    if args.list_ports:
        ports = list_serial_ports()
        if not ports:
            print("No serial ports are currently enumerated.")
        for device, description in ports:
            print(f"{device}: {description}")
        return 0

    try:
        import serial
    except ImportError as exc:
        print(f"SERIAL_ERROR: pyserial is not installed: {exc}", file=sys.stderr)
        return 1

    try:
        with open_serial_port(args.port) as serial_port:
            print(
                f"OPEN port={args.port} baud={SERIAL_BAUDRATE} "
                "format=8N1 mode=read-only"
            )
            run_receive_loop(
                serial_port,
                duration_seconds=args.duration_seconds,
            )
    except (serial.SerialException, OSError) as exc:
        print(f"SERIAL_ERROR: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
