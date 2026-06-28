#!/usr/bin/env python3
"""Build Step40 movement authority evidence manifest."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any


def load_json(path: Path) -> dict[str, Any] | None:
    if not path.exists():
        return None
    return json.loads(path.read_text(encoding="utf-8"))


def sha256(path: Path) -> str | None:
    if not path.exists():
        return None
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def status(obj: dict[str, Any] | None) -> str:
    if obj is None:
        return "missing"
    return str(obj.get("status") or "unknown")


def main() -> int:
    ap = argparse.ArgumentParser(description="Build Step40 movement authority manifest")
    ap.add_argument("--source-session-key", required=True)
    ap.add_argument("--e2e-session-key", required=True)
    ap.add_argument("--source-jsonl", required=True, type=Path)
    ap.add_argument("--authority", required=True, type=Path)
    ap.add_argument("--accepted-jsonl", required=True, type=Path)
    ap.add_argument("--rejected-jsonl", required=True, type=Path)
    ap.add_argument("--e2e", required=True, type=Path)
    ap.add_argument("--mysql-check", required=True, type=Path)
    ap.add_argument("--output", type=Path, default=Path("runtime/mmo_step40_movement_authority_manifest.json"))
    args = ap.parse_args()

    authority = load_json(args.authority)
    e2e = load_json(args.e2e)
    mysql = load_json(args.mysql_check)
    statuses = {
        "authority": status(authority),
        "e2e": status(e2e),
        "mysql_check": status(mysql),
    }
    passed = all(v == "passed" for v in statuses.values())

    manifest = {
        "tool": "build_mmo_step40_movement_authority_manifest.py",
        "status": "passed" if passed else "failed",
        "source_session_key": args.source_session_key,
        "e2e_session_key": args.e2e_session_key,
        "statuses": statuses,
        "artifacts": {
            "source_jsonl": {"path": str(args.source_jsonl), "sha256": sha256(args.source_jsonl)},
            "authority": {"path": str(args.authority), "sha256": sha256(args.authority)},
            "accepted_jsonl": {"path": str(args.accepted_jsonl), "sha256": sha256(args.accepted_jsonl)},
            "rejected_jsonl": {"path": str(args.rejected_jsonl), "sha256": sha256(args.rejected_jsonl)},
            "e2e": {"path": str(args.e2e), "sha256": sha256(args.e2e)},
            "mysql_check": {"path": str(args.mysql_check), "sha256": sha256(args.mysql_check)},
        },
        "authority_summary": None if authority is None else {
            "input_rows": authority.get("input_rows"),
            "accepted_rows": authority.get("accepted_rows"),
            "rejected_rows": authority.get("rejected_rows"),
            "reject_reasons": authority.get("reject_reasons"),
            "position_changed": authority.get("position_changed"),
            "accepted_total_distance": authority.get("accepted_total_distance"),
            "accepted_max_step_distance": authority.get("accepted_max_step_distance"),
            "accepted_max_horizontal_speed": authority.get("accepted_max_horizontal_speed"),
            "accepted_max_vertical_speed": authority.get("accepted_max_vertical_speed"),
            "limits": authority.get("limits"),
        },
        "e2e_summary": None if e2e is None else {
            "rows": e2e.get("rows"),
            "input_rows": e2e.get("input_rows"),
            "kind_counts": e2e.get("kind_counts"),
            "coalesce": e2e.get("coalesce"),
        },
        "mysql_summary": None if mysql is None else {
            "outbox_rows": mysql.get("outbox_rows"),
            "applied_rows": mysql.get("applied_rows"),
            "failed_rows": mysql.get("failed_rows"),
            "journal_events": mysql.get("journal_events"),
            "audit_rows": mysql.get("audit_rows"),
            "distinct_positions": mysql.get("distinct_positions"),
            "latest_projection_distance_to_audit": mysql.get("latest_projection_distance_to_audit"),
        },
        "errors": [],
    }
    for name, st in statuses.items():
        if st != "passed":
            manifest["errors"].append(f"{name} status={st}")
    for name, obj in (("authority", authority), ("e2e", e2e), ("mysql_check", mysql)):
        if obj and obj.get("errors"):
            manifest["errors"].append({name: obj.get("errors")})

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True), encoding="utf-8")
    if passed:
        print("OK: Step40 movement authority manifest:", {
            "accepted": manifest["authority_summary"]["accepted_rows"] if manifest["authority_summary"] else None,
            "mysql_applied": manifest["mysql_summary"]["applied_rows"] if manifest["mysql_summary"] else None,
            "distinct_positions": manifest["mysql_summary"]["distinct_positions"] if manifest["mysql_summary"] else None,
        })
    else:
        for error in manifest["errors"]:
            print("ERROR:", error)
    print(f"artifact={args.output}")
    print(f"status={manifest['status']}")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
