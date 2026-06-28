#!/usr/bin/env python3
"""Build Step41 movement proposal evidence manifest."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any


def load_json(path: Path | None) -> dict[str, Any] | None:
    if path is None or not path.exists():
        return None
    return json.loads(path.read_text(encoding="utf-8"))


def sha256(path: Path | None) -> str | None:
    if path is None or not path.exists():
        return None
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def status(obj: dict[str, Any] | None) -> str:
    return "missing" if obj is None else str(obj.get("status") or "unknown")


def main() -> int:
    ap = argparse.ArgumentParser(description="Build Step41 movement proposal manifest")
    ap.add_argument("--source-session-key", required=True)
    ap.add_argument("--e2e-session-key", default="")
    ap.add_argument("--proposal-jsonl", required=True, type=Path)
    ap.add_argument("--proposal-check", required=True, type=Path)
    ap.add_argument("--accepted-proposal-jsonl", type=Path)
    ap.add_argument("--accepted-checkpoint-jsonl", type=Path)
    ap.add_argument("--e2e", type=Path)
    ap.add_argument("--mysql-check", type=Path)
    ap.add_argument("--output", required=True, type=Path)
    args = ap.parse_args()

    proposal_check = load_json(args.proposal_check)
    e2e = load_json(args.e2e)
    mysql = load_json(args.mysql_check)
    statuses = {
        "proposal_check": status(proposal_check),
        "checkpoint_e2e": status(e2e) if args.e2e else "not_run",
        "mysql_check": status(mysql) if args.mysql_check else "not_run",
    }
    replay_required = args.e2e is not None or args.mysql_check is not None
    passed = statuses["proposal_check"] == "passed" and (not replay_required or (statuses["checkpoint_e2e"] == "passed" and statuses["mysql_check"] == "passed"))
    errors: list[Any] = []
    for k, v in statuses.items():
        if v not in {"passed", "not_run"}:
            errors.append(f"{k} status={v}")
    for name, obj in (("proposal_check", proposal_check), ("e2e", e2e), ("mysql", mysql)):
        if obj and obj.get("errors"):
            errors.append({name: obj.get("errors")})

    manifest = {
        "tool": "build_mmo_step41_movement_proposal_manifest.py",
        "status": "passed" if passed else "failed",
        "source_session_key": args.source_session_key,
        "e2e_session_key": args.e2e_session_key,
        "statuses": statuses,
        "artifacts": {
            "proposal_jsonl": {"path": str(args.proposal_jsonl), "sha256": sha256(args.proposal_jsonl)},
            "proposal_check": {"path": str(args.proposal_check), "sha256": sha256(args.proposal_check)},
            "accepted_proposal_jsonl": {"path": None if args.accepted_proposal_jsonl is None else str(args.accepted_proposal_jsonl), "sha256": sha256(args.accepted_proposal_jsonl)},
            "accepted_checkpoint_jsonl": {"path": None if args.accepted_checkpoint_jsonl is None else str(args.accepted_checkpoint_jsonl), "sha256": sha256(args.accepted_checkpoint_jsonl)},
            "e2e": {"path": None if args.e2e is None else str(args.e2e), "sha256": sha256(args.e2e)},
            "mysql_check": {"path": None if args.mysql_check is None else str(args.mysql_check), "sha256": sha256(args.mysql_check)},
        },
        "proposal_summary": {
            "authority_model": None if proposal_check is None else proposal_check.get("authority_model"),
            "rows": None if proposal_check is None else proposal_check.get("rows"),
            "accepted_rows": None if proposal_check is None else proposal_check.get("accepted_rows"),
            "rejected_rows": None if proposal_check is None else proposal_check.get("rejected_rows"),
            "accepted_checkpoint_rows": None if proposal_check is None else proposal_check.get("accepted_checkpoint_rows"),
            "total_distance": None if proposal_check is None else proposal_check.get("total_distance"),
            "max_horizontal_speed": None if proposal_check is None else proposal_check.get("max_horizontal_speed"),
            "max_upward_speed": None if proposal_check is None else proposal_check.get("max_upward_speed"),
            "max_fall_speed": None if proposal_check is None else proposal_check.get("max_fall_speed"),
            "max_upward_delta": None if proposal_check is None else proposal_check.get("max_upward_delta"),
            "max_fall_delta": None if proposal_check is None else proposal_check.get("max_fall_delta"),
            "fall_segments": None if proposal_check is None else proposal_check.get("fall_segments"),
            "airborne_segments": None if proposal_check is None else proposal_check.get("airborne_segments"),
            "health_drop_segments": None if proposal_check is None else proposal_check.get("health_drop_segments"),
            "reject_reasons": None if proposal_check is None else proposal_check.get("reject_reasons"),
        },
        "mysql_summary": None if mysql is None else {
            "outbox_rows": mysql.get("outbox_rows"),
            "applied_rows": mysql.get("applied_rows"),
            "journal_events": mysql.get("journal_events"),
            "audit_rows": mysql.get("audit_rows"),
            "distinct_positions": mysql.get("distinct_positions"),
        },
        "errors": errors,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True), encoding="utf-8")
    if manifest["status"] == "passed":
        print("OK: Step42 movement proposal manifest:", manifest["proposal_summary"])
    else:
        for e in errors:
            print("ERROR:", e)
    print(f"artifact={args.output}")
    print(f"status={manifest['status']}")
    return 0 if manifest["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
