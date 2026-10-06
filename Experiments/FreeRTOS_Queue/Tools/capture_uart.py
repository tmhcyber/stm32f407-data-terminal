"""Capture real UART bytes and validate FIFO normal/congestion or latest mailbox.

No serial writes, reset commands, or synthetic-data fallback. A log replay only
validates that file; it is not a new board run.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import time

ROW = re.compile(
    r"RTOSQ\|seq=(\d+)\|tick=(\d+)\|rx=(\d+)\|drop=(\d+)"
    r"\|temp=(-?\d+)\|hum=(\d+)\|txerr=(\d+)\|stack_a=(\d+)\|stack_b=(\d+)"
)
FIELDS = ("seq", "tick", "rx", "drop", "temp", "hum", "txerr", "stack_a", "stack_b")
LATEST_ROW = re.compile(
    r"RTOSM\|seq=(\d+)\|tick=(\d+)\|rx=(\d+)\|skip=(\d+)"
    r"\|sample_tick=(\d+)\|age=(\d+)\|temp=(-?\d+)\|hum=(\d+)"
    r"\|txerr=(\d+)\|stack_a=(\d+)\|stack_b=(\d+)"
)
LATEST_FIELDS = ("seq", "tick", "rx", "skip", "sample_tick", "age",
                 "temp", "hum", "txerr", "stack_a", "stack_b")
SENSOR_ROW = re.compile(LATEST_ROW.pattern.replace("RTOSM", "RTOSS") + r"\|fail=(\d+)\|last=(\d+)")
SENSOR_FIELDS = LATEST_FIELDS + ("fail", "last")
DIAG_ROW = re.compile(SENSOR_ROW.pattern + r"\|fail_stage=(\d+)\|fail_status=(\d+)")
DIAG_FIELDS = SENSOR_FIELDS + ("fail_stage", "fail_status")
MASK = 0xFFFFFFFF


def analyze(raw, min_samples=20, mode="normal"):
    if mode not in ("normal", "congestion", "latest", "sensor", "sensor-diag"):
        raise ValueError("unknown analysis mode")
    rows, errors = [], []
    prefix_discarded = False
    lines = raw.splitlines(keepends=True)
    for index, line in enumerate(lines):
        if not line.endswith(b"\n"):
            # Capture may end in the middle of the final UART frame.
            continue
        text = line.rstrip(b"\r\n").decode("ascii", errors="replace")
        if text.startswith(("RTOSQ_BOOT|", "RTOSM_BOOT|", "RTOSS_BOOT|")):
            if rows:
                errors.append("board reboot banner inside sample window")
            if not text.startswith({"normal": "RTOSQ_BOOT|", "congestion": "RTOSQ_BOOT|", "latest": "RTOSM_BOOT|", "sensor": "RTOSS_BOOT|", "sensor-diag": "RTOSS_BOOT|v=4|"}[mode]):
                errors.append("firmware boot protocol does not match selected mode")
            continue
        match = (DIAG_ROW if mode == "sensor-diag" else SENSOR_ROW if mode == "sensor" else LATEST_ROW if mode == "latest" else ROW).fullmatch(text)
        if not match:
            # Opening a serial port mid-frame may yield one incomplete prefix.
            if index == 0 and not text.startswith(("RTOSQ|", "RTOSM|", "RTOSS|", "SHTSTAT|")):
                prefix_discarded = True
                continue
            errors.append(f"invalid complete line {index + 1}: {text[:120]}")
            continue
        row = dict(zip(DIAG_FIELDS if mode == "sensor-diag" else SENSOR_FIELDS if mode == "sensor" else LATEST_FIELDS if mode == "latest" else FIELDS, map(int, match.groups())))
        rows.append(row)
        if mode == "sensor-diag" and (row["fail_stage"] != 0 or row["fail_status"] != 0):
            errors.append("unexpected failure history in nominal diagnostic smoke window")
        counters = ("seq", "tick", "rx", "txerr") + (("skip", "sample_tick", "age") if mode in ("latest", "sensor", "sensor-diag") else ("drop",))
        if any(not 0 <= row[k] <= MASK for k in counters):
            errors.append("counter outside uint32 range")
        if mode in ("sensor", "sensor-diag") and (not -4500 <= row["temp"] <= 13000 or not 0 <= row["hum"] <= 10000 or row["fail"] != 0 or row["last"] != 0):
            errors.append("sensor range or failure status check failed")
        if mode not in ("sensor", "sensor-diag") and (row["temp"], row["hum"]) != (2536, 6000):
            errors.append("simulated payload differs from 2536/6000")
        if (mode == "normal" and row["drop"] != 0) or row["txerr"] != 0:
            errors.append("firmware reports dropped sample or earlier UART error")
        if mode == "normal" and row["rx"] != ((row["seq"] + 1) & MASK):
            errors.append("rx count does not match seq+1 in normal mode")
        if mode in ("latest", "sensor", "sensor-diag"):
            if row["skip"] != ((row["seq"] + 1 - row["rx"]) & MASK):
                errors.append("skip does not account for sequence gaps up to this receive")
            if row["age"] != ((row["tick"] - row["sample_tick"]) & MASK):
                errors.append("age does not match receive minus acquisition tick")
            if row["age"] > 1100:
                errors.append("sample too old for current 1-second acquisition smoke bound")
        if not (0 < row["stack_a"] <= 256 and 0 < row["stack_b"] <= 512):
            errors.append("invalid/exhausted observed task stack margin")
        if len(rows) > 1:
            previous = rows[-2]
            seq_step = (row["seq"] - previous["seq"]) & MASK
            if (mode == "normal" and seq_step != 1) or (mode != "normal" and not 1 <= seq_step <= 4):
                errors.append("nonconsecutive sequence: loss, duplicate, or reset")
            if mode == "congestion" and not 0 <= row["drop"] - previous["drop"] <= 4:
                errors.append("unexpected drop counter jump/reset")
            if ((row["rx"] - previous["rx"]) & MASK) != 1:
                errors.append("nonconsecutive receive count")
            # Wide smoke-test bound, not a real-time performance acceptance.
            lower, upper = (900, 1200) if mode == "normal" else (2900, 3300)
            if not lower <= ((row["tick"] - previous["tick"]) & MASK) <= upper:
                errors.append(f"report tick interval outside smoke bound {lower}..{upper}")
    if len(rows) < min_samples:
        errors.append(f"only {len(rows)} complete samples; need {min_samples}")
    if mode != "normal" and rows:
        loss_key = "skip" if mode in ("latest", "sensor", "sensor-diag") else "drop"
        if not 0 < ((rows[-1][loss_key] - rows[0][loss_key]) & MASK) < 0x80000000:
            errors.append("no growing loss count observed")
        if not any(((b["seq"] - a["seq"]) & MASK) > 1 for a, b in zip(rows, rows[1:])):
            errors.append("no skipped sample sequence observed")
    return {
        "mode": mode,
        "passed": not errors,
        "sample_count": len(rows),
        "first": rows[0] if rows else None,
        "last": rows[-1] if rows else None,
        "age_ticks": ({"min": min(r["age"] for r in rows),
                       "max": max(r["age"] for r in rows)} if mode in ("latest", "sensor", "sensor-diag") and rows else None),
        "sensor_ranges": ({k: {"min": min(r[k] for r in rows), "max": max(r[k] for r in rows)} for k in ("temp", "hum")} if mode in ("sensor", "sensor-diag") and rows else None),
        "stack_min_words": {
            k: min((r[k] for r in rows), default=None) for k in ("stack_a", "stack_b")
        },
        "initial_partial_line_discarded": prefix_discarded,
        "errors": list(dict.fromkeys(errors)),
        "limits": "Checks the selected short observation only; not strict timing, long stability, or independent flash readback.",
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--list", action="store_true", help="list current serial ports")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--port", help="explicit board CH340 COM port")
    mode.add_argument("--input", type=Path, help="replay an existing raw log")
    parser.add_argument("--seconds", type=float, default=35)
    parser.add_argument("--mode", choices=("normal", "congestion", "latest", "sensor", "sensor-diag"), default="normal")
    parser.add_argument("--min-samples", type=int, default=20)
    parser.add_argument("--output", type=Path, help="new evidence directory")
    parser.add_argument("--firmware", type=Path, help="local AXF/HEX to hash; not readback proof")
    args = parser.parse_args()
    if args.list:
        from serial.tools import list_ports
        for port in list_ports.comports():
            print(f"{port.device}: {port.description} [{port.hwid}]")
        return 0
    if args.min_samples < 2 or args.seconds <= 0:
        parser.error("min-samples must be >=2 and seconds >0")
    if args.input:
        summary = analyze(args.input.read_bytes(), args.min_samples, args.mode)
        summary["source"] = "existing_log_replay_not_new_board_run"
        print(json.dumps(summary, indent=2, ensure_ascii=False))
        return 0 if summary["passed"] else 1
    if not args.port or not args.output:
        parser.error("capture requires --port and a new --output directory")
    firmware = None
    if args.firmware:
        firmware = {"path": str(args.firmware.resolve()),
                    "sha256": hashlib.sha256(args.firmware.read_bytes()).hexdigest(),
                    "binding": "local artifact only; no board readback performed"}
    import serial
    args.output.mkdir(parents=True, exist_ok=False)
    started = datetime.now(timezone.utc).isoformat()
    chunks = bytearray()
    capture_error = None
    start = time.monotonic()
    try:
        with serial.Serial(args.port, 115200, timeout=0.2, rtscts=False,
                           dsrdtr=False, xonxoff=False) as port:
            # Keep startup bytes; do not flush input or write to the MCU.
            while time.monotonic() - start < args.seconds:
                data = port.read(port.in_waiting or 1)
                chunks.extend(data)
    except (serial.SerialException, OSError) as exc:
        capture_error = str(exc)
    raw = bytes(chunks)
    (args.output / "uart_raw.log").write_bytes(raw)
    summary = analyze(raw, args.min_samples, args.mode)
    if capture_error:
        summary["errors"].append("capture error: " + capture_error)
        summary["passed"] = False
    summary.update(source="serial_capture", port=args.port, baud=115200,
                   started_utc=started, elapsed_seconds=time.monotonic()-start,
                   raw_sha256=hashlib.sha256(raw).hexdigest(), firmware=firmware)
    text = json.dumps(summary, indent=2, ensure_ascii=False)
    (args.output / "summary.json").write_text(text + "\n", encoding="utf-8")
    print(text)
    return 0 if summary["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
