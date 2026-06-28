#!/usr/bin/env python3
"""Build a final Step40 movement-authority manifest from positive and negative evidence."""
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
    if not path.exists() or not path.is_file():
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
    ap = argparse.ArgumentParser(description="Build Step40 positive+negative authority manifest")
    ap.add_argument("--source-session-key", required=True)
    ap.add_argument("--positive-manifest", required=True, type=Path, help="runtime/mmo_step40_movement_authority_manifest.json")
    ap.add_argument("--negative-suite", required=True, type=Path, help="runtime/mmo_step40_movement_negative_suite.json")
    ap.add_argument("--output", type=Path, default=Path("runtime/mmo_step40_movement_authority_final_manifest.json"))
    args = ap.parse_args()

    positive = load_json(args.positive_manifest)
    negative = load_json(args.negative_suite)
    statuses = {
        "positive_authority_e2e": status_of(positive),
        "negative_authority_suite": status_of(negative),
    }
    passed = all(v == "passed" for v in statuses.values())

    negative_summary = None
    if negative is not None:
        negative_summary = {
            "scenario_count": len(negative.get("scenarios") or []),
            "passed_scenarios": sum(1 for s in (negative.get("scenarios") or []) if s.get("status") == "passed"),
            "scenarios": [
                {
                    "name": s.get("name"),
                    "status": s.get("status"),
                    "expected_reject_reason": s.get("expected_reject_reason"),
                    "summary": s.get("summary"),
                }
                for s in (negative.get("scenarios") or [])
            ],
        }

    manifest = {
        "tool": "build_mmo_step40_movement_authority_final_manifest.py",
        "status": "passed" if passed else "failed",
        "source_session_key": args.source_session_key,
        "statuses": statuses,
        "artifacts": {
            "positive_manifest": {"path": str(args.positive_manifest), "sha256": sha256(args.positive_manifest)},
            "negative_suite": {"path": str(args.negative_suite), "sha256": sha256(args.negative_suite)},
        },
        "positive_summary": None if positive is None else {
            "authority": positive.get("authority_summary"),
            "mysql": positive.get("mysql_summary"),
            "statuses": positive.get("statuses"),
        },
        "negative_summary": negative_summary,
        "meaning": {
            "normal_path": "clean movement checkpoint capture is accepted, replayed and persisted through MySQL checkpoint procedure",
            "hostile_path": "mutated impossible checkpoint proposals are rejected or failed before persistence",
            "boundary": "offline authority harness only; final live MMO movement still needs input proposals, collision/world-bound runtime validation and replication",
        },
        "errors": [],
    }
    for name, st in statuses.items():
        if st != "passed":
            manifest["errors"].append(f"{name} status={st}")
    if negative and negative.get("errors"):
        manifest["errors"].append({"negative_suite": negative.get("errors")})
    if positive and positive.get("errors"):
        manifest["errors"].append({"positive_manifest": positive.get("errors")})

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True), encoding="utf-8")
    if manifest["status"] == "passed":
        print("OK: Step40 final authority manifest:", statuses)
    else:
        for error in manifest["errors"]:
            print("ERROR:", error)
    print(f"artifact={args.output}")
    print(f"status={manifest['status']}")
    return 0 if manifest["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
