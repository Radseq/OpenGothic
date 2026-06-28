#!/usr/bin/env python3
"""Run Step40 movement authority validation, then replay accepted checkpoints to MySQL.

Pipeline:
  client Step39 JSONL -> authority validator -> accepted JSONL -> existing Step39
  receiver/outbox/worker/MySQL checkpoint E2E -> Step40 manifest.

This keeps the OpenGothic client outside MySQL and proves the server-side gate
can reject impossible movement before persistence.
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from dataclasses import dataclass, asdict
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


def run_cmd(name: str, argv: list[str], timeout: int | None = None) -> CommandResult:
    proc = subprocess.run(argv, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout)
    return CommandResult(name, argv, proc.returncode, tail(proc.stdout), tail(proc.stderr))


def load_json(path: Path) -> dict[str, Any] | None:
    if not path.exists():
        return None
    return json.loads(path.read_text(encoding="utf-8"))


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description="Run Step40 movement authority E2E")
    ap.add_argument("--url", required=True)
    ap.add_argument("--client-jsonl", required=True, type=Path)
    ap.add_argument("--source-session-key", required=True, help="Original Step39 capture session key prefix.")
    ap.add_argument("--session-key", required=True, help="Step40 E2E replay/idempotency session key prefix.")
    ap.add_argument("--tools-dir", type=Path, default=Path("tools"))
    ap.add_argument("--authority-output", type=Path, default=Path("runtime/mmo_step40_movement_authority.json"))
    ap.add_argument("--accepted-jsonl", type=Path, default=Path("runtime/mmo_step40_movement_authority.accepted.jsonl"))
    ap.add_argument("--rejected-jsonl", type=Path, default=Path("runtime/mmo_step40_movement_authority.rejected.jsonl"))
    ap.add_argument("--e2e-output", type=Path, default=Path("runtime/mmo_step40_movement_authority_e2e.json"))
    ap.add_argument("--mysql-check-output", type=Path, default=Path("runtime/mmo_step40_movement_authority_mysql_e2e.json"))
    ap.add_argument("--manifest-output", type=Path, default=Path("runtime/mmo_step40_movement_authority_manifest.json"))
    ap.add_argument("--output", type=Path, default=Path("runtime/mmo_step40_movement_authority_run.json"))
    ap.add_argument("--min-accepted", type=int, default=2)
    ap.add_argument("--require-position-change", action="store_true")
    ap.add_argument("--allow-rejections", action="store_true")
    ap.add_argument("--tick-rate", type=float, default=1000.0)
    ap.add_argument("--min-tick-delta", type=int, default=1)
    ap.add_argument("--max-tick-delta", type=int, default=15000)
    ap.add_argument("--max-step-distance", type=float, default=2500.0)
    ap.add_argument("--max-horizontal-speed", type=float, default=2500.0)
    ap.add_argument("--max-vertical-speed", type=float, default=2500.0)
    ap.add_argument("--max-vertical-delta", type=float, default=1600.0)
    ap.add_argument("--bounds", type=float, nargs=6)
    ap.add_argument("--allow-reason", action="append", default=[])
    ap.add_argument("--max-replay-rows", type=int, default=20)
    ap.add_argument("--coalesce-min-distance", type=float, default=75.0)
    ap.add_argument("--coalesce-force-tick-delta", type=int, default=5000)
    ap.add_argument("--reset-matching-failed", action="store_true")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args(argv)

    result: dict[str, Any] = {
        "tool": "run_mmo_step40_movement_authority_e2e.py",
        "status": "planned" if args.dry_run else "running",
        "source_session_key": args.source_session_key,
        "session_key": args.session_key,
        "client_jsonl": str(args.client_jsonl),
        "authority_output": str(args.authority_output),
        "accepted_jsonl": str(args.accepted_jsonl),
        "rejected_jsonl": str(args.rejected_jsonl),
        "e2e_output": str(args.e2e_output),
        "mysql_check_output": str(args.mysql_check_output),
        "manifest_output": str(args.manifest_output),
        "commands": [],
    }

    authority_cmd = [
        sys.executable, str(args.tools_dir / "check_mmo_step40_movement_authority.py"),
        "--jsonl", str(args.client_jsonl),
        "--session-key", args.source_session_key,
        "--accepted-jsonl", str(args.accepted_jsonl),
        "--rejected-jsonl", str(args.rejected_jsonl),
        "--output", str(args.authority_output),
        "--min-accepted", str(args.min_accepted),
        "--tick-rate", str(args.tick_rate),
        "--min-tick-delta", str(args.min_tick_delta),
        "--max-tick-delta", str(args.max_tick_delta),
        "--max-step-distance", str(args.max_step_distance),
        "--max-horizontal-speed", str(args.max_horizontal_speed),
        "--max-vertical-speed", str(args.max_vertical_speed),
        "--max-vertical-delta", str(args.max_vertical_delta),
    ]
    if args.require_position_change:
        authority_cmd.append("--require-position-change")
    if args.allow_rejections:
        authority_cmd.append("--allow-rejections")
    if args.bounds:
        authority_cmd.append("--bounds")
        authority_cmd.extend(str(v) for v in args.bounds)
    for reason in args.allow_reason:
        authority_cmd.extend(["--allow-reason", reason])

    if args.dry_run:
        result["commands"].append({"name": "authority", "argv": authority_cmd})
        result["status"] = "planned"
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True), encoding="utf-8")
        print(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True))
        print(f"artifact={args.output}")
        print(f"status={result['status']}")
        return 0

    auth = run_cmd("authority", authority_cmd, timeout=60)
    result["commands"].append(asdict(auth))
    if auth.returncode != 0:
        result["status"] = "failed"

    e2e_cmd = [
        sys.executable, str(args.tools_dir / "run_mmo_step39_movement_e2e.py"),
        "--url", args.url,
        "--client-jsonl", str(args.accepted_jsonl),
        "--session-key", args.session_key,
        "--output", str(args.e2e_output),
        "--checker-output", str(args.mysql_check_output),
        "--max-rows", str(args.max_replay_rows),
        "--coalesce-min-distance", str(args.coalesce_min_distance),
        "--coalesce-force-tick-delta", str(args.coalesce_force_tick_delta),
        "--require-position-change",
    ]
    if args.reset_matching_failed:
        e2e_cmd.append("--reset-matching-failed")
    if result.get("status") != "failed":
        e2e = run_cmd("step39_e2e_from_authority_accepted", e2e_cmd, timeout=240)
        result["commands"].append(asdict(e2e))
        if e2e.returncode != 0:
            result["status"] = "failed"

    manifest_cmd = [
        sys.executable, str(args.tools_dir / "build_mmo_step40_movement_authority_manifest.py"),
        "--source-session-key", args.source_session_key,
        "--e2e-session-key", args.session_key,
        "--source-jsonl", str(args.client_jsonl),
        "--authority", str(args.authority_output),
        "--accepted-jsonl", str(args.accepted_jsonl),
        "--rejected-jsonl", str(args.rejected_jsonl),
        "--e2e", str(args.e2e_output),
        "--mysql-check", str(args.mysql_check_output),
        "--output", str(args.manifest_output),
    ]
    if result.get("status") != "failed":
        manifest = run_cmd("manifest", manifest_cmd, timeout=60)
        result["commands"].append(asdict(manifest))
        result["status"] = "passed" if manifest.returncode == 0 else "failed"

    authority_json = load_json(args.authority_output)
    manifest_json = load_json(args.manifest_output)
    result["authority_summary"] = None if authority_json is None else {
        "input_rows": authority_json.get("input_rows"),
        "accepted_rows": authority_json.get("accepted_rows"),
        "rejected_rows": authority_json.get("rejected_rows"),
        "position_changed": authority_json.get("position_changed"),
        "accepted_total_distance": authority_json.get("accepted_total_distance"),
    }
    result["manifest_status"] = None if manifest_json is None else manifest_json.get("status")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True), encoding="utf-8")

    for cmd in result["commands"]:
        if cmd.get("stdout_tail"):
            print(cmd["stdout_tail"], end="" if cmd["stdout_tail"].endswith("\n") else "\n")
        if cmd.get("stderr_tail"):
            print(cmd["stderr_tail"], file=sys.stderr, end="" if cmd["stderr_tail"].endswith("\n") else "\n")
    print(f"status={result['status']}")
    print(f"artifact={args.output}")
    print(f"manifest_artifact={args.manifest_output}")
    return 0 if result["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
