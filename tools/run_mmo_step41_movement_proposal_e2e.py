#!/usr/bin/env python3
"""Validate Step41 movement proposals, convert accepted rows to checkpoints, replay via Step39 E2E."""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from dataclasses import asdict, dataclass
from pathlib import Path


@dataclass
class CommandResult:
    name: str
    argv: list[str]
    returncode: int
    stdout_tail: str = ""
    stderr_tail: str = ""


def tail(text: str, limit: int = 7000) -> str:
    return text if len(text) <= limit else text[-limit:]


def run(name: str, argv: list[str]) -> CommandResult:
    proc = subprocess.run(argv, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if proc.stdout:
        print(proc.stdout, end="")
    if proc.stderr:
        print(proc.stderr, end="", file=sys.stderr)
    return CommandResult(name, argv, proc.returncode, tail(proc.stdout), tail(proc.stderr))


def main() -> int:
    ap = argparse.ArgumentParser(description="Run Step41 movement proposal authority E2E")
    ap.add_argument("--url", required=True)
    ap.add_argument("--proposal-jsonl", required=True, type=Path)
    ap.add_argument("--source-session-key", required=True)
    ap.add_argument("--session-key", required=True, help="session prefix used for accepted checkpoint replay")
    ap.add_argument("--output", required=True, type=Path)
    ap.add_argument("--proposal-check-output", type=Path, default=Path("runtime/mmo_step41_movement_proposal_check.json"))
    ap.add_argument("--accepted-proposal-jsonl", type=Path, default=Path("runtime/mmo_step41_movement_proposals.accepted.jsonl"))
    ap.add_argument("--rejected-proposal-jsonl", type=Path, default=Path("runtime/mmo_step41_movement_proposals.rejected.jsonl"))
    ap.add_argument("--accepted-checkpoint-jsonl", type=Path, default=Path("runtime/mmo_step41_movement_proposals.accepted_checkpoints.jsonl"))
    ap.add_argument("--e2e-output", type=Path, default=Path("runtime/mmo_step41_movement_proposal_e2e.json"))
    ap.add_argument("--mysql-check-output", type=Path, default=Path("runtime/mmo_step41_movement_proposal_mysql_e2e.json"))
    ap.add_argument("--manifest-output", type=Path, default=Path("runtime/mmo_step41_movement_proposal_manifest.json"))
    ap.add_argument("--tools-dir", type=Path, default=Path("tools"))
    ap.add_argument("--min-accepted", type=int, default=2)
    ap.add_argument("--require-position-change", action="store_true")
    ap.add_argument("--max-step-distance", type=float, default=2500.0)
    ap.add_argument("--max-horizontal-speed", type=float, default=2500.0)
    ap.add_argument("--max-vertical-speed", type=float, default=2500.0)
    ap.add_argument("--max-vertical-delta", type=float, default=1600.0)
    ap.add_argument("--max-upward-speed", type=float, default=-1.0)
    ap.add_argument("--max-upward-delta", type=float, default=-1.0)
    ap.add_argument("--max-fall-speed", type=float, default=9000.0)
    ap.add_argument("--max-fall-delta", type=float, default=12000.0)
    ap.add_argument("--large-fall-delta", type=float, default=800.0)
    ap.add_argument("--allow-unmarked-small-down-step", type=float, default=250.0)
    ap.add_argument("--require-motion-state-for-large-fall", action="store_true")
    ap.add_argument("--require-fall-state-for-health-drop", action="store_true")
    ap.add_argument("--max-replay-rows", type=int, default=20)
    ap.add_argument("--coalesce-min-distance", type=float, default=75.0)
    ap.add_argument("--coalesce-force-tick-delta", type=int, default=5000)
    ap.add_argument("--reset-matching-failed", action="store_true")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    commands: list[CommandResult] = []
    check_cmd = [
        sys.executable, str(args.tools_dir / "check_mmo_step41_movement_proposal_jsonl.py"),
        "--jsonl", str(args.proposal_jsonl),
        "--session-key", args.source_session_key,
        "--output", str(args.proposal_check_output),
        "--accepted-jsonl", str(args.accepted_proposal_jsonl),
        "--rejected-jsonl", str(args.rejected_proposal_jsonl),
        "--accepted-checkpoint-jsonl", str(args.accepted_checkpoint_jsonl),
        "--min-accepted", str(args.min_accepted),
        "--max-rejected", "0",
        "--max-step-distance", str(args.max_step_distance),
        "--max-horizontal-speed", str(args.max_horizontal_speed),
        "--max-vertical-speed", str(args.max_vertical_speed),
        "--max-vertical-delta", str(args.max_vertical_delta),
        "--max-upward-speed", str(args.max_upward_speed),
        "--max-upward-delta", str(args.max_upward_delta),
        "--max-fall-speed", str(args.max_fall_speed),
        "--max-fall-delta", str(args.max_fall_delta),
        "--large-fall-delta", str(args.large_fall_delta),
        "--allow-unmarked-small-down-step", str(args.allow_unmarked_small_down_step),
    ]
    if args.require_position_change:
        check_cmd.append("--require-position-change")
    if args.require_motion_state_for_large_fall:
        check_cmd.append("--require-motion-state-for-large-fall")
    if args.require_fall_state_for_health_drop:
        check_cmd.append("--require-fall-state-for-health-drop")
    if args.dry_run:
        print("DRY RUN:", " ".join(check_cmd))
        return 0
    r = run("proposal_check", check_cmd); commands.append(r)
    if r.returncode != 0:
        status = "failed"
    else:
        e2e_cmd = [
            sys.executable, str(args.tools_dir / "run_mmo_step39_movement_e2e.py"),
            "--url", args.url,
            "--client-jsonl", str(args.accepted_checkpoint_jsonl),
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
        r2 = run("checkpoint_e2e", e2e_cmd); commands.append(r2)
        status = "passed" if r2.returncode == 0 else "failed"

    manifest_cmd = [
        sys.executable, str(args.tools_dir / "build_mmo_step41_movement_proposal_manifest.py"),
        "--source-session-key", args.source_session_key,
        "--e2e-session-key", args.session_key,
        "--proposal-jsonl", str(args.proposal_jsonl),
        "--proposal-check", str(args.proposal_check_output),
        "--accepted-proposal-jsonl", str(args.accepted_proposal_jsonl),
        "--accepted-checkpoint-jsonl", str(args.accepted_checkpoint_jsonl),
        "--e2e", str(args.e2e_output),
        "--mysql-check", str(args.mysql_check_output),
        "--output", str(args.manifest_output),
    ]
    rm = run("manifest", manifest_cmd); commands.append(rm)
    if rm.returncode != 0:
        status = "failed"

    result = {
        "tool": "run_mmo_step41_movement_proposal_e2e.py",
        "status": status,
        "source_session_key": args.source_session_key,
        "session_key": args.session_key,
        "proposal_jsonl": str(args.proposal_jsonl),
        "proposal_check_output": str(args.proposal_check_output),
        "accepted_checkpoint_jsonl": str(args.accepted_checkpoint_jsonl),
        "e2e_output": str(args.e2e_output),
        "mysql_check_output": str(args.mysql_check_output),
        "manifest_output": str(args.manifest_output),
        "commands": [asdict(c) for c in commands],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True), encoding="utf-8")
    print(f"artifact={args.output}")
    print(f"status={status}")
    return 0 if status == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
