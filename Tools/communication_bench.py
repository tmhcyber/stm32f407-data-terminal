"""Shared evidence and stop-and-wait scheduling for the two bench tools.

These are host observations, not proof of board identity, resets or sensor health.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import subprocess
import time


ROOT = Path(__file__).resolve().parents[1]


def positive(text):
    value = float(text)
    if not math.isfinite(value) or value <= 0:
        raise argparse.ArgumentTypeError("must be finite and positive")
    return value


def nonnegative(text):
    value = float(text)
    if not math.isfinite(value) or value < 0:
        raise argparse.ArgumentTypeError("must be finite and nonnegative")
    return value


def add_campaign_arguments(parser, default_rounds):
    limit = parser.add_mutually_exclusive_group()
    limit.add_argument("--rounds", type=int)
    limit.add_argument("--duration-seconds", type=positive)
    parser.add_argument("--interval-ms", type=nonnegative, default=0)
    parser.add_argument("--output", type=Path, help="NEW evidence directory")
    parser.add_argument("--firmware", type=Path, help="operator-selected flashed AXF/HEX")
    parser.add_argument("--setup-note", default="", help="wiring/power/download evidence reference")
    parser.set_defaults(default_rounds=default_rounds)


def validate_campaign_arguments(parser, args):
    if args.rounds is None and args.duration_seconds is None:
        args.rounds = args.default_rounds
    if args.rounds is not None and args.rounds <= 0:
        parser.error("--rounds must be positive")
    if args.firmware is not None and not args.firmware.is_file():
        parser.error("--firmware must name an existing artifact")
    return args


def provenance(firmware=None):
    def git(*args):
        return subprocess.check_output(
            ["git", "-c", "core.quotepath=false", *args], cwd=ROOT
        ).decode("utf-8", errors="replace").strip()

    sources = {}
    for folder in ("Core", "Application", "BSP", "Components", "Drivers", "MDK-ARM", "Tools"):
        for path in sorted((ROOT / folder).rglob("*")):
            if path.is_file() and path.suffix.lower() in (".c", ".h", ".s", ".sct", ".uvprojx", ".py"):
                if "Objects" not in path.parts and "Listings" not in path.parts:
                    sources[path.relative_to(ROOT).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
    result = {"git_head": git("rev-parse", "HEAD"), "git_status": git("status", "--short"),
              "source_sha256": sources,
              "flash_binding": "operator supplied; no target readback performed"}
    if firmware is not None:
        result["host_artifact"] = {"path": str(firmware.resolve()),
                                   "sha256": hashlib.sha256(firmware.read_bytes()).hexdigest()}
    else:
        result["flash_binding"] = "UNCONFIRMED: no firmware artifact supplied"
    return result


def percentile(values, fraction):
    if not values:
        return None
    values = sorted(values)
    return values[max(0, math.ceil(len(values) * fraction) - 1)]


def run_campaign(adapter, *, rounds=None, duration=None, interval=0.0,
                 timeout=0.5, emit=lambda **row: None, clock=time.monotonic,
                 sleep=time.sleep):
    """Adapter: send(), receive(timeout)->dict with status, check_quiet().

    Missed periodic slots are skipped, never replayed as a catch-up burst.
    Any failure ends the run because the existing protocols have no transaction ID.
    """
    if (rounds is None) == (duration is None):
        raise ValueError("choose exactly one of rounds or duration")
    if rounds is not None and (not isinstance(rounds, int) or rounds <= 0):
        raise ValueError("rounds must be a positive integer")
    if (duration is not None and (not math.isfinite(duration) or duration <= 0)) or not math.isfinite(interval) or interval < 0 or not math.isfinite(timeout) or timeout <= 0:
        raise ValueError("invalid timing")
    stats = {"send_attempts": 0, "requests": 0, "successes": 0, "timeouts": 0, "errors": 0,
             "missed_slots": 0, "completed": False, "reason": "", "rtt_ms": [],
             "max_start_lateness_ms": 0.0}
    start = clock()
    end = None if duration is None else start + duration
    due = start
    slot = 0
    next_progress = start + 60
    try:
        while rounds is None or stats["requests"] < rounds:
            now = clock()
            if interval and now > due + interval:
                missed = math.ceil((now - due) / interval)
                stats["missed_slots"] += missed
                slot += missed
                due = start + slot * interval
            wake = due if end is None else min(due, end)
            if wake > now:
                sleep(wake - now)
            adapter.check_quiet()
            now = clock()
            if end is not None and now >= end:
                break
            # A request started before the end gets its full response timeout;
            # final drain can therefore extend the measured window slightly.
            sent_at = clock()
            stats["max_start_lateness_ms"] = max(stats["max_start_lateness_ms"],
                                                 (sent_at - due) * 1000 if interval else 0)
            stats["send_attempts"] += 1
            adapter.send()
            stats["requests"] += 1
            remaining = timeout - (clock() - sent_at)
            response = adapter.receive(remaining) if remaining > 0 else {"status": "timeout"}
            rtt_ms = (clock() - sent_at) * 1000
            emit(event="round", round=stats["requests"], elapsed_s=sent_at - start,
                 rtt_ms=rtt_ms, **response)
            if response["status"] == "timeout":
                stats["timeouts"] += 1
                stats["reason"] = "response timeout; stopped before next request"
                break
            if response["status"] != "ok":
                stats["errors"] += 1
                stats["reason"] = response["status"]
                break
            stats["successes"] += 1
            stats["rtt_ms"].append(rtt_ms)
            if clock() >= next_progress:
                emit(event="progress", requests=stats["requests"], successes=stats["successes"])
                next_progress = clock() + 60
            slot += 1
            due = start + slot * interval if interval else clock()
        else:
            # Count mode: guard the final response against an immediate duplicate.
            sleep(0.02)
            adapter.check_quiet()
        stats["completed"] = not stats["reason"] and stats["requests"] > 0
    except KeyboardInterrupt:
        stats["reason"] = "interrupted"
    except Exception as exc:
        stats["errors"] += 1
        stats["reason"] = f"{type(exc).__name__}: {exc}"
        emit(event="error", detail=stats["reason"])
    stats["elapsed_s"] = clock() - start
    values = stats.pop("rtt_ms")
    stats.update(rtt_p50_ms=percentile(values, 0.5), rtt_p95_ms=percentile(values, 0.95),
                 rtt_max_ms=max(values) if values else None,
                 rtt_scope="PC write through validated receive; includes USB/OS/logging")
    stats["periodic_target_met"] = stats["completed"] and stats["missed_slots"] == 0
    # Completed exchanges alone cannot prove sensor continuity or absence of resets.
    stats["board_health_verified"] = False
    return stats


def execute(adapter_factory, args, transport):
    output = args.output or ROOT / "Docs" / "Evidence" / "Communication" / (
        datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ") + "_" + transport)
    output.mkdir(parents=True, exist_ok=False)
    config = {key: str(value) if isinstance(value, Path) else value
              for key, value in vars(args).items()}
    manifest = {"transport": transport, "config": config, "provenance": provenance(args.firmware)}
    if getattr(args, "dll", None) is not None:
        manifest["pcan_dll_sha256"] = hashlib.sha256(args.dll.read_bytes()).hexdigest()
    (output / "manifest.json").write_text(json.dumps(manifest,
        ensure_ascii=False, indent=2), encoding="utf-8")
    with (output / "events.jsonl").open("x", encoding="utf-8", buffering=1) as journal:
        def emit(**row):
            row.update(utc=datetime.now(timezone.utc).isoformat(), monotonic_s=time.monotonic())
            journal.write(json.dumps(row, ensure_ascii=False) + "\n")
            if row["event"] == "progress":
                print(f"progress requests={row['requests']} success={row['successes']}", flush=True)
        emit(event="start", transport=transport)
        adapter = None
        try:
            adapter = adapter_factory(emit)
            summary = run_campaign(adapter, rounds=args.rounds, duration=args.duration_seconds,
                                   interval=args.interval_ms / 1000,
                                   timeout=args.response_timeout_ms / 1000, emit=emit)
        except (Exception, KeyboardInterrupt) as exc:
            summary = {"completed": False, "reason": f"open/setup: {type(exc).__name__}: {exc}"}
            emit(event="error", detail=summary["reason"])
        finally:
            if adapter is not None:
                try:
                    adapter.close()
                except Exception as exc:
                    summary.update(completed=False, reason=f"close: {exc}")
        emit(event="summary", **summary)
    summary["events_sha256"] = hashlib.sha256((output / "events.jsonl").read_bytes()).hexdigest()
    (output / "summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    print(f"Evidence: {output.resolve()}")
    return 0 if summary["completed"] and summary.get("periodic_target_met", False) else 1
