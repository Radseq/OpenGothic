#!/usr/bin/env python3
"""Run hostile Step41/42 movement_proposal authority tests."""
from __future__ import annotations

import argparse
import json
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


def tail(text: str, limit: int = 5000) -> str:
    return text if len(text) <= limit else text[-limit:]


def run(name: str, argv: list[str]) -> CommandResult:
    proc = subprocess.run(argv, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if proc.stdout:
        print(proc.stdout, end="")
    if proc.stderr:
        print(proc.stderr, end="", file=sys.stderr)
    return CommandResult(name, argv, proc.returncode, tail(proc.stdout), tail(proc.stderr))


def load(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def main() -> int:
    ap = argparse.ArgumentParser(description="Run Step41/42 movement proposal negative suite")
    ap.add_argument("--jsonl", required=True, type=Path)
    ap.add_argument("--session-key", required=True)
    ap.add_argument("--output-dir", required=True, type=Path)
    ap.add_argument("--output", required=True, type=Path)
    ap.add_argument("--tools-dir", type=Path, default=Path("tools"))
    ap.add_argument("--min-accepted", type=int, default=2)
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
    args = ap.parse_args()

    args.output_dir.mkdir(parents=True, exist_ok=True)
    corpus_dir = args.output_dir / "corpus"
    corpus_manifest = corpus_dir / "manifest.json"
    commands: list[CommandResult] = []
    errors: list[Any] = []

    corpus_cmd = [
        sys.executable, str(args.tools_dir / "build_mmo_step41_movement_proposal_negative_corpus.py"),
        "--jsonl", str(args.jsonl),
        "--session-key", args.session_key,
        "--output-dir", str(corpus_dir),
        "--output", str(corpus_manifest),
    ]
    r = run("build_negative_corpus", corpus_cmd); commands.append(r)
    if r.returncode != 0:
        errors.append("negative corpus build failed")
        manifest = {"scenarios": {}}
    else:
        manifest = load(corpus_manifest)

    scenario_results: dict[str, Any] = {}
    if not errors:
        for name, info in manifest.get("scenarios", {}).items():
            jsonl = Path(info["jsonl"])
            expected = str(info["expected_reject_reason"])
            out = args.output_dir / f"{name}.authority.json"
            accepted = args.output_dir / f"{name}.accepted.jsonl"
            rejected = args.output_dir / f"{name}.rejected.jsonl"
            checkpoints = args.output_dir / f"{name}.accepted_checkpoints.jsonl"
            cmd = [
                sys.executable, str(args.tools_dir / "check_mmo_step41_movement_proposal_jsonl.py"),
                "--jsonl", str(jsonl),
                "--session-key", args.session_key,
                "--output", str(out),
                "--accepted-jsonl", str(accepted),
                "--rejected-jsonl", str(rejected),
                "--accepted-checkpoint-jsonl", str(checkpoints),
                "--min-accepted", str(args.min_accepted),
                "--min-rejected", "1",
                "--require-reject-reason", expected,
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
            if args.require_motion_state_for_large_fall:
                cmd.append("--require-motion-state-for-large-fall")
            rr = run(name, cmd); commands.append(rr)
            report = load(out) if out.exists() else None
            scenario_results[name] = {
                "status": None if report is None else report.get("status"),
                "expected_reject_reason": expected,
                "reject_reasons": None if report is None else report.get("reject_reasons"),
                "accepted_rows": None if report is None else report.get("accepted_rows"),
                "rejected_rows": None if report is None else report.get("rejected_rows"),
                "authority_report": str(out),
                "accepted_jsonl": str(accepted),
                "rejected_jsonl": str(rejected),
            }
            if rr.returncode != 0:
                errors.append({name: "authority check failed"})

    result = {
        "tool": "run_mmo_step41_movement_proposal_negative_suite.py",
        "authority_model": "step42_movement_proposal_fall_aware_v1",
        "status": "passed" if not errors else "failed",
        "source_jsonl": str(args.jsonl),
        "session_key": args.session_key,
        "corpus_manifest": str(corpus_manifest),
        "scenario_results": scenario_results,
        "commands": [asdict(c) for c in commands],
        "errors": errors,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True), encoding="utf-8")
    if errors:
        for e in errors:
            print("ERROR:", e)
    else:
        print("OK: Step42 movement proposal negative suite:", {"scenarios": len(scenario_results), "errors": 0})
    print(f"artifact={args.output}")
    print(f"status={result['status']}")
    return 0 if result["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
