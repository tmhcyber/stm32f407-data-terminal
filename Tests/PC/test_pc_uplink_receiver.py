import sys
import unittest
from pathlib import Path
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
TOOLS_PATH = ROOT / "Tools"
sys.path.insert(0, str(TOOLS_PATH))

from pc_uplink_receiver import (
    CrlfLineFramer,
    FramerState,
    ProtocolError,
    ProtocolErrorCode,
    UplinkStreamProcessor,
    format_record,
    format_summary,
    open_serial_port,
    parse_arguments,
    parse_uplink_line,
    run_receive_loop,
)


def make_valid_line(
    *,
    reason="EVENT",
    flags="0x00000016",
    gap="0",
    snap="56",
    quality="VALID",
    status="OK",
    value_ms="26015",
    status_ms="26015",
    sequence="27",
):
    return (
        f"v=1;reason={reason};flags={flags};gap={gap};coal=0;"
        f"snap={snap};n=2;"
        "p0.src=SHT30;p0.id=TEMPERATURE;p0.value=31745;"
        f"p0.scale=-3;p0.unit=DEG_C;p0.quality={quality};"
        f"p0.status={status};p0.value_ms={value_ms};"
        f"p0.status_ms={status_ms};p0.seq={sequence};"
        "p1.src=SHT30;p1.id=HUMIDITY;p1.value=76593;"
        f"p1.scale=-3;p1.unit=PERCENT_RH;p1.quality={quality};"
        f"p1.status={status};p1.value_ms={value_ms};"
        f"p1.status_ms={status_ms};p1.seq={sequence};"
    ).encode("ascii")


class CrlfLineFramerTests(unittest.TestCase):
    # User-led design: expected states, outputs, and final counters were
    # independently derived before the production framer was implemented.
    def test_overlong_line_resynchronizes_and_emits_next_complete_line(self):
        framer = CrlfLineFramer(max_payload_bytes=5)

        self.assertEqual(framer.feed(b"junk\r"), [])
        self.assertEqual(
            framer.state,
            FramerState.SEEK_FIRST_CRLF,
        )

        self.assertEqual(
            framer.feed(b"\nOK\r\nP"),
            [b"OK"],
        )
        self.assertEqual(
            framer.state,
            FramerState.SYNCED,
        )

        self.assertEqual(framer.feed(b"12345"), [])
        self.assertEqual(
            framer.state,
            FramerState.DISCARD_UNTIL_CRLF,
        )

        self.assertEqual(framer.feed(b"tail\r"), [])
        self.assertEqual(
            framer.state,
            FramerState.DISCARD_UNTIL_CRLF,
        )

        self.assertEqual(
            framer.feed(b"\nNEXT\r\n"),
            [b"NEXT"],
        )
        self.assertEqual(
            framer.state,
            FramerState.SYNCED,
        )

        self.assertEqual(framer.stats.chunks_received, 5)
        self.assertEqual(framer.stats.bytes_received, 28)
        self.assertEqual(framer.stats.complete_line_count, 2)
        self.assertEqual(framer.stats.framing_error_count, 1)
        self.assertEqual(framer.stats.startup_discard_count, 1)
        self.assertEqual(framer.stats.resync_count, 1)
        self.assertEqual(framer.stats.discarded_byte_count, 18)

    def test_startup_crlf_can_span_chunks_and_one_chunk_can_hold_many_lines(self):
        framer = CrlfLineFramer(max_payload_bytes=20)

        self.assertEqual(framer.feed(b"fragment\r"), [])
        self.assertEqual(
            framer.feed(b"\nONE\r\nTWO\r\npartial"),
            [b"ONE", b"TWO"],
        )
        self.assertEqual(framer.feed(b"\r\n"), [b"partial"])

        self.assertEqual(framer.state, FramerState.SYNCED)
        self.assertEqual(framer.stats.complete_line_count, 3)
        self.assertEqual(framer.stats.startup_discard_count, 1)
        self.assertEqual(framer.stats.discarded_byte_count, 10)

    def test_exact_maximum_payload_accepts_crlf_split_across_chunks(self):
        framer = CrlfLineFramer(max_payload_bytes=5)

        self.assertEqual(framer.feed(b"\r\n"), [])
        self.assertEqual(framer.feed(b"12345\r"), [])
        self.assertEqual(framer.feed(b"\n"), [b"12345"])

        self.assertEqual(framer.state, FramerState.SYNCED)
        self.assertEqual(framer.stats.framing_error_count, 0)
        self.assertEqual(framer.stats.resync_count, 0)

    def test_complete_overlong_line_resyncs_within_the_same_chunk(self):
        framer = CrlfLineFramer(max_payload_bytes=5)

        self.assertEqual(framer.feed(b"\r\n"), [])
        self.assertEqual(
            framer.feed(b"123456\r\nOK\r\n"),
            [b"OK"],
        )

        self.assertEqual(framer.state, FramerState.SYNCED)
        self.assertEqual(framer.stats.complete_line_count, 1)
        self.assertEqual(framer.stats.framing_error_count, 1)
        self.assertEqual(framer.stats.resync_count, 1)
        self.assertEqual(framer.stats.discarded_byte_count, 10)

    def test_empty_chunks_do_not_change_receive_counters(self):
        framer = CrlfLineFramer()

        self.assertEqual(framer.feed(b""), [])
        self.assertEqual(framer.stats.chunks_received, 0)
        self.assertEqual(framer.stats.bytes_received, 0)

    def test_invalid_configuration_and_chunk_type_are_rejected(self):
        with self.assertRaisesRegex(ValueError, "must not be negative"):
            CrlfLineFramer(max_payload_bytes=-1)

        framer = CrlfLineFramer()
        with self.assertRaisesRegex(TypeError, "bytes-like"):
            framer.feed("not bytes")


class UplinkParserTests(unittest.TestCase):
    def assert_protocol_error(self, code, line):
        with self.assertRaises(ProtocolError) as context:
            parse_uplink_line(line)
        self.assertEqual(context.exception.code, code)

    def test_valid_event_builds_typed_record_and_exact_decimal_values(self):
        record = parse_uplink_line(make_valid_line())

        self.assertEqual(record.version, 1)
        self.assertEqual(record.reason, "EVENT")
        self.assertEqual(record.flags, 0x00000016)
        self.assertEqual(record.snapshot_version, 56)
        self.assertEqual(record.points[0].scaled_value.as_tuple().exponent, -3)
        self.assertEqual(str(record.points[0].scaled_value), "31.745")
        self.assertEqual(str(record.points[1].scaled_value), "76.593")
        self.assertTrue(record.points[0].is_current)

    def test_offline_record_keeps_last_value_without_marking_it_current(self):
        record = parse_uplink_line(
            make_valid_line(
                flags="0x0000003A",
                snap="24",
                quality="OFFLINE",
                status="NACK",
                value_ms="7015",
                status_ms="10200",
                sequence="8",
            )
        )

        self.assertEqual(str(record.points[0].scaled_value), "31.745")
        self.assertFalse(record.points[0].is_current)
        self.assertEqual(record.points[0].value_ms, 7015)
        self.assertEqual(record.points[0].status_ms, 10200)
        self.assertEqual(record.points[0].sequence, 8)

    def test_heartbeat_requires_zero_flags_and_gap(self):
        record = parse_uplink_line(
            make_valid_line(
                reason="HEARTBEAT",
                flags="0x00000000",
            )
        )
        self.assertEqual(record.reason, "HEARTBEAT")

        self.assert_protocol_error(
            ProtocolErrorCode.SEMANTIC,
            make_valid_line(reason="HEARTBEAT", flags="0x00000001"),
        )
        self.assert_protocol_error(
            ProtocolErrorCode.SEMANTIC,
            make_valid_line(
                reason="HEARTBEAT",
                flags="0x00000000",
                gap="1",
            ),
        )

    def test_duplicate_missing_and_unknown_fields_are_rejected(self):
        valid = make_valid_line()
        duplicate = valid.replace(b"v=1;", b"v=1;v=1;", 1)
        missing = valid.replace(b"flags=0x00000016;", b"", 1)
        unknown = valid + b"debug=1;"

        self.assert_protocol_error(
            ProtocolErrorCode.DUPLICATE_FIELD,
            duplicate,
        )
        self.assert_protocol_error(ProtocolErrorCode.SCHEMA, missing)
        self.assert_protocol_error(ProtocolErrorCode.SCHEMA, unknown)

    def test_non_ascii_and_invalid_field_syntax_are_rejected(self):
        self.assert_protocol_error(
            ProtocolErrorCode.NON_ASCII,
            make_valid_line() + b"\xFF",
        )
        self.assert_protocol_error(
            ProtocolErrorCode.SYNTAX,
            make_valid_line().replace(b"reason=EVENT;", b"reason=EVENT;;"),
        )

    def test_flags_format_known_bits_and_event_semantics_are_checked(self):
        self.assert_protocol_error(
            ProtocolErrorCode.VALUE_FORMAT,
            make_valid_line(flags="0x1"),
        )
        self.assert_protocol_error(
            ProtocolErrorCode.UNSUPPORTED_VALUE,
            make_valid_line(flags="0x00000100"),
        )
        self.assert_protocol_error(
            ProtocolErrorCode.SEMANTIC,
            make_valid_line(flags="0x00000000"),
        )

    def test_integer_ranges_even_snapshot_and_point_count_are_checked(self):
        self.assert_protocol_error(
            ProtocolErrorCode.VALUE_FORMAT,
            make_valid_line(gap="-1"),
        )
        self.assert_protocol_error(
            ProtocolErrorCode.SEMANTIC,
            make_valid_line(snap="3"),
        )
        self.assert_protocol_error(
            ProtocolErrorCode.SCHEMA,
            make_valid_line().replace(b"n=2;", b"n=2;p2.src=SHT30;"),
        )

    def test_point_identity_scale_and_batch_consistency_are_checked(self):
        wrong_unit = make_valid_line().replace(
            b"p0.unit=DEG_C;",
            b"p0.unit=PERCENT_RH;",
        )
        wrong_scale = make_valid_line().replace(
            b"p1.scale=-3;",
            b"p1.scale=-2;",
        )
        mismatched_sequence = make_valid_line().replace(
            b"p1.seq=27;",
            b"p1.seq=28;",
        )

        self.assert_protocol_error(ProtocolErrorCode.SEMANTIC, wrong_unit)
        self.assert_protocol_error(ProtocolErrorCode.SEMANTIC, wrong_scale)
        self.assert_protocol_error(
            ProtocolErrorCode.SEMANTIC,
            mismatched_sequence,
        )

    def test_timestamp_wrap_values_are_preserved_without_absolute_order_check(self):
        record = parse_uplink_line(
            make_valid_line(
                value_ms="4294967290",
                status_ms="10",
            )
        )

        self.assertEqual(record.points[0].value_ms, 4294967290)
        self.assertEqual(record.points[0].status_ms, 10)

    def test_a_bad_line_does_not_prevent_the_next_line_from_parsing(self):
        duplicate = make_valid_line().replace(b"v=1;", b"v=1;v=1;", 1)
        self.assert_protocol_error(
            ProtocolErrorCode.DUPLICATE_FIELD,
            duplicate,
        )

        record = parse_uplink_line(make_valid_line())
        self.assertEqual(record.snapshot_version, 56)


class UplinkStreamProcessorTests(unittest.TestCase):
    def test_framing_and_parsing_errors_update_only_their_own_counters(self):
        processor = UplinkStreamProcessor(max_payload_bytes=509)
        valid_first = make_valid_line()
        duplicate = valid_first.replace(b"v=1;", b"v=1;v=1;", 1)
        valid_second = make_valid_line(snap="58", sequence="28")

        stream = (
            b"startup-fragment\r\n"
            + valid_first
            + b"\r\n"
            + duplicate
            + b"\r\n"
            + (b"X" * 510)
            + b"\r\n"
            + valid_second
            + b"\r\n"
        )
        batch = processor.process(stream)

        self.assertEqual(len(batch.records), 2)
        self.assertEqual(len(batch.rejected_lines), 1)
        self.assertEqual(
            batch.rejected_lines[0].code,
            ProtocolErrorCode.DUPLICATE_FIELD,
        )
        self.assertEqual(batch.records[0].snapshot_version, 56)
        self.assertEqual(batch.records[1].snapshot_version, 58)

        self.assertEqual(processor.framer.stats.complete_line_count, 3)
        self.assertEqual(processor.stats.valid_record_count, 2)
        self.assertEqual(processor.stats.parse_error_count, 1)
        self.assertEqual(processor.framer.stats.framing_error_count, 1)
        self.assertEqual(processor.framer.stats.resync_count, 1)
        self.assertEqual(
            processor.stats.observed_snapshot_change_count,
            1,
        )
        self.assertEqual(
            processor.stats.parse_error_counts,
            {ProtocolErrorCode.DUPLICATE_FIELD: 1},
        )

    def test_rejected_raw_line_is_available_for_bounded_diagnostics(self):
        processor = UplinkStreamProcessor()
        duplicate = make_valid_line().replace(b"v=1;", b"v=1;v=1;", 1)

        processor.process(b"\r\n")
        batch = processor.process(duplicate + b"\r\n")

        self.assertEqual(len(batch.records), 0)
        self.assertEqual(len(batch.rejected_lines), 1)
        self.assertEqual(batch.rejected_lines[0].raw_line, duplicate)
        self.assertIn("duplicate field", batch.rejected_lines[0].message)

    # User-led unknown-input diagnosis: the rejected field used a lowercase
    # hexadecimal digit even though protocol version 1 requires uppercase.
    def test_lowercase_flag_is_rejected_then_uppercase_flag_is_accepted(self):
        processor = UplinkStreamProcessor()
        lowercase = make_valid_line(flags="0x0000001a")
        uppercase = make_valid_line(flags="0x0000001A")

        processor.process(b"\r\n")
        batch = processor.process(
            lowercase + b"\r\n" + uppercase + b"\r\n"
        )

        self.assertEqual(len(batch.rejected_lines), 1)
        self.assertEqual(
            batch.rejected_lines[0].code,
            ProtocolErrorCode.VALUE_FORMAT,
        )
        self.assertEqual(len(batch.records), 1)
        self.assertEqual(batch.records[0].flags, 0x1A)

        self.assertEqual(processor.framer.stats.complete_line_count, 2)
        self.assertEqual(processor.stats.parse_error_count, 1)
        self.assertEqual(processor.stats.valid_record_count, 1)
        self.assertEqual(processor.framer.stats.framing_error_count, 0)
        self.assertEqual(processor.framer.stats.resync_count, 0)


class FakeSerial:
    def __init__(self, chunks):
        self._chunks = list(chunks)
        self.read_sizes = []

    def read(self, size):
        self.read_sizes.append(size)
        if not self._chunks:
            raise KeyboardInterrupt
        return self._chunks.pop(0)


class SerialReceiverTests(unittest.TestCase):
    def test_empty_reads_are_not_counted_and_arbitrary_chunks_are_processed(self):
        valid = make_valid_line()
        split_at = 80
        serial_port = FakeSerial(
            (
                b"",
                b"startup\r",
                b"\n" + valid[:split_at],
                valid[split_at:] + b"\r\n",
                b"",
            )
        )
        output = []

        result = run_receive_loop(serial_port, output=output.append)

        framer = result.processor.framer.stats
        parser = result.processor.stats
        self.assertEqual(framer.chunks_received, 3)
        self.assertEqual(framer.complete_line_count, 1)
        self.assertEqual(parser.valid_record_count, 1)
        self.assertEqual(parser.parse_error_count, 0)
        self.assertEqual(framer.framing_error_count, 0)
        self.assertTrue(any(text.startswith("RECORD ") for text in output))
        self.assertTrue(output[-1].startswith("SUMMARY "))

    def test_offline_display_marks_value_as_last_valid(self):
        record = parse_uplink_line(
            make_valid_line(
                flags="0x0000003A",
                snap="24",
                quality="OFFLINE",
                status="NACK",
                value_ms="7015",
                status_ms="10200",
                sequence="8",
            )
        )

        text = format_record(record)
        self.assertIn("value_kind=last_valid", text)
        self.assertIn("quality=OFFLINE", text)
        self.assertIn("status=NACK", text)

    def test_summary_keeps_pc_counters_separate_and_quantified(self):
        processor = UplinkStreamProcessor()
        processor.process(b"fragment\r\n" + make_valid_line() + b"\r\n")

        summary = format_summary(processor, 10.0)

        self.assertIn("elapsed_s=10.000", summary)
        self.assertIn("complete_lines=1", summary)
        self.assertIn("valid_records=1", summary)
        self.assertIn("parse_errors=0", summary)
        self.assertIn("resyncs=0", summary)
        self.assertNotIn("skipped_count", summary)
        self.assertNotIn("format_error_count", summary)
        self.assertNotIn("coalesced_count", summary)

    def test_command_line_requires_explicit_port_or_list_request(self):
        args = parse_arguments(
            ["--port", "COM8", "--duration-seconds", "10"]
        )
        self.assertEqual(args.port, "COM8")
        self.assertEqual(args.duration_seconds, 10.0)

        listed = parse_arguments(["--list-ports"])
        self.assertTrue(listed.list_ports)

    def test_serial_port_is_opened_as_fixed_115200_8n1_read_only_input(self):
        with patch("serial.Serial") as serial_constructor:
            open_serial_port("COM9")

        kwargs = serial_constructor.call_args.kwargs
        self.assertEqual(kwargs["port"], "COM9")
        self.assertEqual(kwargs["baudrate"], 115200)
        self.assertEqual(kwargs["bytesize"], 8)
        self.assertEqual(kwargs["parity"], "N")
        self.assertEqual(kwargs["stopbits"], 1)
        self.assertEqual(kwargs["timeout"], 0.2)


if __name__ == "__main__":
    unittest.main()
