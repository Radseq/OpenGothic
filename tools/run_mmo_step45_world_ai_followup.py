#!/usr/bin/env python3
"""Run the Step45 post-capture MySQL follow-up pipeline.

This is a convenience wrapper for the workflow that appeared in real Step43
live testing:
  1. Server has already captured/enqueued live actions.
  2. A pickup may fail because MySQL projection says the loose item is already
     removed while the current runtime save still shows it.
  3. For dev evidence only, optionally align pickup projections.
  4. Re-run the resolved worker with prefix reset and continue-on-error.
  5. Build a gameplay-domain coverage report.
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from dataclasses import dataclass
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
    return text[-limit:]


def run(name: str, argv: list[str], *, allow_failure: bool = False) -> CommandResult:
    print("[RUN] " + " ".join(argv), flush=True)
    proc = subprocess.run(argv, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if proc.stdout:
        print(proc.stdout, end="")
    if proc.stderr:
        print(proc.stderr, file=sys.stderr, end="")
    if proc.returncode != 0 and not allow_failure:
        raise SystemExit(proc.returncode)
    return CommandResult(name, argv, proc.returncode, tail(proc.stdout or ""), tail(proc.stderr or ""))


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description="Step45 worker follow-up: optional pickup fixture, worker, gameplay domain checker.")
    ap.add_argument("--url", required=True)
    ap.add_argument("--session-key", required=True)
    ap.add_argument("--worker-id", default="dev-resolved-worker-step45")
    ap.add_argument("--max-actions", type=int, default=1000)
    ap.add_argument("--accepted-jsonl", default="runtime/mmo_server_actions_step45.jsonl")
    ap.add_argument("--checkpoint-jsonl", default="runtime/mmo_server_checkpoints_step45.jsonl")
    ap.add_argument("--rejected-jsonl", default="runtime/mmo_server_rejects_step45.jsonl")
    ap.add_argument("--summary-json", default="runtime/mmo_server_step45_summary.json")
    ap.add_argument("--output", default="runtime/mmo_step45_world_ai_weapon_loot_check.json")
    ap.add_argument("--manifest", default="runtime/mmo_step45_world_ai_followup_manifest.json")
    ap.add_argument("--prepare-pickup-fixture", action="store_true", help="DEV only: run prepare_mmo_dispatch_dev_fixture.py --apply before worker")
    ap.add_argument("--fail-on-mysql-errors", action="store_true")
    ap.add_argument("--require-domain", action="append", default=[])
    ap.add_argument("--require-default-domains", action="store_true")
    ap.add_argument("--require-world-ai-domains", action="store_true")
    ap.add_argument("--fail-on-invalid-packets", action="store_true")
    args = ap.parse_args(argv)

    script_dir = Path(__file__).resolve().parent
    results: list[CommandResult] = []

    if args.prepare_pickup_fixture:
        fixture = script_dir / "prepare_mmo_dispatch_dev_fixture.py"
        if not fixture.exists():
            raise SystemExit(f"missing {fixture}; copy it from tools/ or do not use --prepare-pickup-fixture")
        results.append(run("prepare_pickup_fixture", [sys.executable, str(fixture), "--url", args.url, "--session-key", args.session_key, "--apply"], allow_failure=False))

    worker = script_dir / "run_mmo_resolved_action_worker.py"
    results.append(run("resolved_worker", [
        sys.executable, str(worker),
        "--url", args.url,
        "--worker-id", args.worker_id,
        "--session-key", args.session_key,
        "--max-actions", str(args.max_actions),
        "--reset-matching-failed",
        "--continue-on-error",
    ], allow_failure=True))

    checker = script_dir / "check_mmo_step45_world_ai_weapon_loot.py"
    check_cmd = [
        sys.executable, str(checker),
        "--accepted-jsonl", args.accepted_jsonl,
        "--checkpoint-jsonl", args.checkpoint_jsonl,
        "--rejected-jsonl", args.rejected_jsonl,
        "--summary-json", args.summary_json,
        "--url", args.url,
        "--session-key", args.session_key,
        "--output", args.output,
    ]
    if args.require_default_domains:
        check_cmd.append("--require-default-domains")
    if args.fail_on_mysql_errors:
        check_cmd.append("--fail-on-mysql-errors")
    if args.require_world_ai_domains:
        check_cmd.append("--require-world-ai-domains")
    if args.fail_on_invalid_packets:
        check_cmd.append("--fail-on-invalid-packets")
    for domain in args.require_domain:
        check_cmd.extend(["--require-domain", domain])
    results.append(run("step45_domain_check", check_cmd, allow_failure=True))

    manifest = {
        "status": "passed" if results[-1].returncode == 0 else "failed",
        "session_key": args.session_key,
        "commands": [r.__dict__ for r in results],
        "domain_report": args.output,
    }
    out = Path(args.manifest)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"artifact={out}")
    print(f"status={manifest['status']}")
    return 0 if manifest["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
