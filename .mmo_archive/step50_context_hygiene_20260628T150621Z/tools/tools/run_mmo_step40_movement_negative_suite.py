#!/usr/bin/env python3
"""Run hostile/negative Step40 movement authority scenarios.

The positive Step40 E2E proves normal walking is accepted and persisted. This
suite proves the validator fails closed: teleport-like jumps, vertical spikes,
time reversal, invalid positions and optional duplicate idempotency are rejected
or failed before any accepted replay into MySQL.
"""
from __future__ import annotations

import argparse
import json
import math
import subprocess
import sys
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any


@dataclass
class CommandResult:
    name: str
    argv: list[str]
    returncode: int
    stdout_tail: str
    stderr_tail: str


def tail(text: str, limit: int = 8000) -> str:
    return text if len(text) <= limit else text[-limit:]


def run_cmd(name: str, argv: list[str], timeout: int = 60) -> CommandResult:
    proc = subprocess.run(argv, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout)
    return CommandResult(name, argv, proc.returncode, tail(proc.stdout), tail(proc.stderr))


def load_json(path: Path) -> dict[str, Any] | None:
    if not path.exists():
        return None
    return json.loads(path.read_text(encoding="utf-8"))


def payload_of(row: dict[str, Any]) -> dict[str, Any]:
    payload = row.get("payload")
    return payload if isinstance(payload, dict) else {}


def as_float(value: Any) -> float | None:
    try:
        f = float(value)
    except (TypeError, ValueError):
        return None
    return f if math.isfinite(f) else None


def load_positions(path: Path, session_key: str) -> list[tuple[float, float, float]]:
    positions: list[tuple[float, float, float]] = []
    if not path.exists():
        return positions
    with path.open("r", encoding="utf-8", errors="replace") as f:
        for raw in f:
            text = raw.strip()
            if not text:
                continue
            try:
                obj = json.loads(text)
            except json.JSONDecodeError:
                continue
            if not isinstance(obj, dict) or obj.get("action_kind") != "character_checkpoint":
                continue
            if session_key and not str(obj.get("idempotency_key") or "").startswith(session_key + ":"):
                continue
            payload = payload_of(obj)
            x = as_float(payload.get("pos_x"))
            y = as_float(payload.get("pos_y"))
            z = as_float(payload.get("pos_z"))
            if x is not None and y is not None and z is not None:
                positions.append((x, y, z))
    return positions


def bounds_from_positions(positions: list[tuple[float, float, float]], padding: float) -> list[str]:
    if not positions:
        return ["-1", "1", "-1", "1", "-1", "1"]
    xs = [p[0] for p in positions]
    ys = [p[1] for p in positions]
    zs = [p[2] for p in positions]
    return [
        str(min(xs) - padding), str(max(xs) + padding),
        str(min(ys) - padding), str(max(ys) + padding),
        str(min(zs) - padding), str(max(zs) + padding),
    ]


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description="Run Step40 movement authority negative suite")
    ap.add_argument("--jsonl", required=True, type=Path, help="Clean source Step39/Step40 movement capture")
    ap.add_argument("--session-key", required=True, help="Source capture session key")
    ap.add_argument("--tools-dir", type=Path, default=Path("tools"))
    ap.add_argument("--output-dir", type=Path, default=Path("runtime/step40_movement_negative_suite"))
    ap.add_argument("--corpus-manifest", type=Path, default=None, help="Existing corpus manifest. If omitted, the corpus is generated first.")
    ap.add_argument("--output", type=Path, default=Path("runtime/mmo_step40_movement_negative_suite.json"))
    ap.add_argument("--min-accepted", type=int, default=2)
    ap.add_argument("--tick-rate", type=float, default=1000.0)
    ap.add_argument("--min-tick-delta", type=int, default=1)
    ap.add_argument("--max-tick-delta", type=int, default=15000)
    ap.add_argument("--max-step-distance", type=float, default=2500.0)
    ap.add_argument("--max-horizontal-speed", type=float, default=2500.0)
    ap.add_argument("--max-vertical-speed", type=float, default=2500.0)
    ap.add_argument("--max-vertical-delta", type=float, default=1600.0)
    ap.add_argument("--bounds-padding", type=float, default=250.0)
    ap.add_argument("--include-duplicate-idempotency", action="store_true")
    args = ap.parse_args(argv)

    args.output_dir.mkdir(parents=True, exist_ok=True)
    result: dict[str, Any] = {
        "tool": "run_mmo_step40_movement_negative_suite.py",
        "status": "running",
        "source_jsonl": str(args.jsonl),
        "session_key": args.session_key,
        "output_dir": str(args.output_dir),
        "commands": [],
        "scenarios": [],
        "errors": [],
    }

    corpus_manifest = args.corpus_manifest if args.corpus_manifest is not None else args.output_dir / "corpus" / "manifest.json"
    if not corpus_manifest.exists():
        corpus_cmd = [
            sys.executable, str(args.tools_dir / "build_mmo_step40_movement_negative_corpus.py"),
            "--jsonl", str(args.jsonl),
            "--session-key", args.session_key,
            "--output-dir", str(args.output_dir / "corpus"),
            "--manifest", str(corpus_manifest),
        ]
        if args.include_duplicate_idempotency:
            corpus_cmd.append("--include-duplicate-idempotency")
        cmd = run_cmd("build_negative_corpus", corpus_cmd)
        result["commands"].append(asdict(cmd))
        if cmd.returncode != 0:
            result["status"] = "failed"
            result["errors"].append("negative corpus generation failed")
            args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True), encoding="utf-8")
            print(cmd.stdout_tail, end="" if cmd.stdout_tail.endswith("\n") else "\n")
            print(cmd.stderr_tail, file=sys.stderr, end="" if cmd.stderr_tail.endswith("\n") else "\n")
            print(f"artifact={args.output}")
            print("status=failed")
            return 1

    corpus = load_json(corpus_manifest)
    if not corpus:
        raise SystemExit(f"corpus manifest missing or invalid: {corpus_manifest}")
    positions = load_positions(args.jsonl, args.session_key)
    scenario_bounds = bounds_from_positions(positions, args.bounds_padding)

    for scenario in corpus.get("scenarios", []):
        name = str(scenario.get("name") or "unknown")
        expectation = str(scenario.get("expectation") or "")
        expected_reason = str(scenario.get("expected_reject_reason") or "")
        path = Path(str(scenario.get("jsonl") or ""))
        report_path = args.output_dir / f"{name}.authority.json"
        accepted_path = args.output_dir / f"{name}.accepted.jsonl"
        rejected_path = args.output_dir / f"{name}.rejected.jsonl"
        checker_cmd = [
            sys.executable, str(args.tools_dir / "check_mmo_step40_movement_authority.py"),
            "--jsonl", str(path),
            "--session-key", args.session_key,
            "--accepted-jsonl", str(accepted_path),
            "--rejected-jsonl", str(rejected_path),
            "--output", str(report_path),
            "--min-accepted", str(args.min_accepted),
            "--tick-rate", str(args.tick_rate),
            "--min-tick-delta", str(args.min_tick_delta),
            "--max-tick-delta", str(args.max_tick_delta),
            "--max-step-distance", str(args.max_step_distance),
            "--max-horizontal-speed", str(args.max_horizontal_speed),
            "--max-vertical-speed", str(args.max_vertical_speed),
            "--max-vertical-delta", str(args.max_vertical_delta),
            "--require-position-change",
        ]
        if expectation == "must_reject_and_pass":
            checker_cmd.extend(["--allow-rejections", "--min-rejected", "1", "--require-reject-reason", expected_reason])
        if name == "outside_world_bounds":
            checker_cmd.append("--bounds")
            checker_cmd.extend(scenario_bounds)

        cmd = run_cmd(f"authority_negative:{name}", checker_cmd)
        result["commands"].append(asdict(cmd))
        report = load_json(report_path)
        scenario_status = "unknown"
        scenario_errors: list[str] = []
        if expectation == "must_reject_and_pass":
            if cmd.returncode != 0:
                scenario_status = "failed"
                scenario_errors.append("checker returned non-zero for reject-and-pass scenario")
            elif not report or report.get("status") != "passed":
                scenario_status = "failed"
                scenario_errors.append("authority report did not pass")
            elif int(report.get("rejected_rows") or 0) < 1:
                scenario_status = "failed"
                scenario_errors.append("scenario did not reject any row")
            elif int((report.get("reject_reasons") or {}).get(expected_reason) or 0) < 1:
                scenario_status = "failed"
                scenario_errors.append(f"expected reject reason missing: {expected_reason}")
            else:
                scenario_status = "passed"
        elif expectation == "must_fail":
            if cmd.returncode == 0:
                scenario_status = "failed"
                scenario_errors.append("checker unexpectedly passed must-fail scenario")
            else:
                scenario_status = "passed"
        else:
            scenario_status = "failed"
            scenario_errors.append(f"unknown scenario expectation: {expectation}")

        entry = {
            "name": name,
            "expectation": expectation,
            "expected_reject_reason": expected_reason,
            "status": scenario_status,
            "jsonl": str(path),
            "authority_report": str(report_path),
            "accepted_jsonl": str(accepted_path),
            "rejected_jsonl": str(rejected_path),
            "returncode": cmd.returncode,
            "summary": None if report is None else {
                "accepted_rows": report.get("accepted_rows"),
                "rejected_rows": report.get("rejected_rows"),
                "reject_reasons": report.get("reject_reasons"),
            },
            "errors": scenario_errors,
        }
        result["scenarios"].append(entry)
        if scenario_errors:
            result["errors"].append({name: scenario_errors})

    result["corpus_manifest"] = str(corpus_manifest)
    result["status"] = "passed" if not result["errors"] else "failed"
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True), encoding="utf-8")

    for cmd in result["commands"]:
        if cmd.get("stdout_tail"):
            print(cmd["stdout_tail"], end="" if cmd["stdout_tail"].endswith("\n") else "\n")
        if cmd.get("stderr_tail"):
            print(cmd["stderr_tail"], file=sys.stderr, end="" if cmd["stderr_tail"].endswith("\n") else "\n")
    print("OK: Step40 movement negative suite:" if result["status"] == "passed" else "ERROR: Step40 movement negative suite failed", {"scenarios": len(result["scenarios"]), "errors": len(result["errors"])})
    print(f"artifact={args.output}")
    print(f"status={result['status']}")
    return 0 if result["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
