#!/usr/bin/env python3
"""Build a compact Step39 movement evidence manifest from JSONL/MySQL artifacts."""
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


def status_of(obj: dict[str, Any] | None) -> str:
    if obj is None:
        return "missing"
    return str(obj.get("status") or "unknown")


def main() -> int:
    ap = argparse.ArgumentParser(description="Build Step39 movement/checkpoint evidence manifest")
    ap.add_argument("--session-key", required=True)
    ap.add_argument("--client-jsonl", required=True, type=Path)
    ap.add_argument("--jsonl-check", type=Path, default=Path("runtime/mmo_step39_movement_jsonl_check.json"))
    ap.add_argument("--e2e", type=Path, default=Path("runtime/mmo_step39_movement_e2e.json"))
    ap.add_argument("--mysql-check", type=Path, default=Path("runtime/mmo_step39_movement_mysql_e2e.json"))
    ap.add_argument("--output", type=Path, default=Path("runtime/mmo_step39_movement_manifest.json"))
    args = ap.parse_args()

    jsonl_check = load_json(args.jsonl_check)
    e2e = load_json(args.e2e)
    mysql_check = load_json(args.mysql_check)

    statuses = {
        "jsonl_check": status_of(jsonl_check),
        "e2e": status_of(e2e),
        "mysql_check": status_of(mysql_check),
    }
    passed = all(v == "passed" for v in statuses.values())

    manifest = {
        "tool": "build_mmo_step39_movement_manifest.py",
        "status": "passed" if passed else "failed",
        "session_key": args.session_key,
        "statuses": statuses,
        "artifacts": {
            "client_jsonl": {"path": str(args.client_jsonl), "sha256": sha256(args.client_jsonl)},
            "jsonl_check": {"path": str(args.jsonl_check), "sha256": sha256(args.jsonl_check)},
            "e2e": {"path": str(args.e2e), "sha256": sha256(args.e2e)},
            "mysql_check": {"path": str(args.mysql_check), "sha256": sha256(args.mysql_check)},
        },
        "jsonl_summary": {
            "rows": None if jsonl_check is None else jsonl_check.get("rows"),
            "position_changed": None if jsonl_check is None else jsonl_check.get("position_changed"),
            "total_distance": None if jsonl_check is None else jsonl_check.get("total_distance"),
            "stationary_ratio": None if jsonl_check is None else jsonl_check.get("stationary_ratio"),
            "reasons": None if jsonl_check is None else jsonl_check.get("reasons"),
        },
        "mysql_summary": {
            "outbox_rows": None if mysql_check is None else mysql_check.get("outbox_rows"),
            "applied_rows": None if mysql_check is None else mysql_check.get("applied_rows"),
            "journal_events": None if mysql_check is None else mysql_check.get("journal_events"),
            "audit_rows": None if mysql_check is None else mysql_check.get("audit_rows"),
            "distinct_positions": None if mysql_check is None else mysql_check.get("distinct_positions"),
            "latest_projection_distance_to_audit": None if mysql_check is None else mysql_check.get("latest_projection_distance_to_audit"),
        },
        "coalesce": None if e2e is None else e2e.get("coalesce"),
        "errors": [],
    }
    for name, st in statuses.items():
        if st != "passed":
            manifest["errors"].append(f"{name} status={st}")
    for name, obj in (("jsonl_check", jsonl_check), ("e2e", e2e), ("mysql_check", mysql_check)):
        if obj and obj.get("errors"):
            manifest["errors"].append({name: obj.get("errors")})

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True), encoding="utf-8")
    if manifest["status"] == "passed":
        print("OK: Step39 movement manifest:", {"jsonl_rows": manifest["jsonl_summary"]["rows"], "mysql_applied": manifest["mysql_summary"]["applied_rows"], "distinct_positions": manifest["mysql_summary"]["distinct_positions"]})
    else:
        for error in manifest["errors"]:
            print("ERROR:", error)
    print(f"artifact={args.output}")
    print(f"status={manifest['status']}")
    return 0 if manifest["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
