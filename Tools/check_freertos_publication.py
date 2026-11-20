"""Check the FreeRTOS release's source, HEX, and bench evidence."""
import hashlib
import json
from pathlib import Path

from analyze_modbus_waveform import analyze

ROOT = Path(__file__).resolve().parents[1]
EVIDENCE = ROOT / "evidence" / "freertos"


def load(path):
    return json.loads(path.read_text(encoding="utf-8"))


def check_hashes(entries, root, normalize_newlines=False):
    for name, expected in entries.items():
        data = (root / name).read_bytes()
        if normalize_newlines:
            data = data.replace(b"\r\n", b"\n")
        if hashlib.sha256(data).hexdigest() != expected:
            raise ValueError("SHA256 mismatch: " + name)


def main():
    manifest = load(EVIDENCE / "MANIFEST.json")
    check_hashes(manifest["sources_sha256_lf"], ROOT, normalize_newlines=True)
    check_hashes(manifest["evidence_sha256"], EVIDENCE)

    for window in ("bare-metal", "rtos"):
        normal = EVIDENCE / window
        modbus = load(normal / "modbus-summary.json")
        can = load(normal / "can-summary.json")
        rows = (normal / "modbus-readings.jsonl").read_text(encoding="utf-8").splitlines()
        events = (normal / "can-events.jsonl").read_bytes()
        assert modbus["attempts"] == modbus["valid_responses"] == len(rows) == 600
        assert modbus["errors"] == 0 and all("error" not in json.loads(row) for row in rows)
        assert can["requests"] == can["successes"] == 60
        assert can["timeouts"] == can["errors"] == can["missed_slots"] == 0
        assert hashlib.sha256(events).hexdigest() == can["events_sha256"]

    fault = load(EVIDENCE / "sensor-fault.json")["summary"]
    assert fault["errors"] == 0 and fault["valid_responses"] == 320
    assert fault["qualities"]["stale"] > 0 and fault["qualities"]["offline"] > 0
    cases = load(EVIDENCE / "modbus-cases.json")["cases"]
    assert len(cases) == 11 and all(row["passed"] and row["recovery_passed"] for row in cases)
    injections = load(EVIDENCE / "watchdog-and-fault.json")
    assert len(injections) == 5 and all(row["passed"] for row in injections)
    for row in injections[-2:]:
        assert row["frame_valid"] == 1 and row["pc"] > 0
        assert row["original_sp"] == row["core_frame_sp"]
    gateway = load(EVIDENCE / "gateway.json")
    assert gateway["records"] == gateway["mqtt_records"] == 29 and gateway["diagnostics"] == 6
    waveform, _, _ = analyze(EVIDENCE / "waveform")
    assert waveform["passed"] and len(waveform["measurements"]) == 10
    print("PASS: source and evidence hashes, board-tested HEX, 2 parallel windows, "
          "diagnostic recovery, Linux gateway data and 10 waveforms")


if __name__ == "__main__":
    main()
