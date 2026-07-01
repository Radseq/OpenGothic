#!/usr/bin/env python3
"""Build final Step42 fall-aware movement proposal authority manifest."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any


def load(path: Path) -> dict[str, Any] | None:
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


def st(obj: dict[str, Any] | None) -> str:
    return "missing" if obj is None else str(obj.get("status") or "unknown")


def main() -> int:
    ap = argparse.ArgumentParser(description="Build Step42 movement proposal final manifest")
    ap.add_argument("--source-session-key", required=True)
    ap.add_argument("--positive-manifest", required=True, type=Path)
    ap.add_argument("--negative-suite", required=True, type=Path)
    ap.add_argument("--output", required=True, type=Path)
    args = ap.parse_args()

    positive = load(args.positive_manifest)
    negative = load(args.negative_suite)
    statuses = {
        "positive_proposal_e2e": st(positive),
        "negative_proposal_suite": st(negative),
    }
    errors: list[Any] = []
    if statuses["positive_proposal_e2e"] != "passed":
        errors.append(f"positive_proposal_e2e status={statuses['positive_proposal_e2e']}")
    if statuses["negative_proposal_suite"] != "passed":
        errors.append(f"negative_proposal_suite status={statuses['negative_proposal_suite']}")
    if positive and positive.get("errors"):
        errors.append({"positive_errors": positive.get("errors")})
    if negative and negative.get("errors"):
        errors.append({"negative_errors": negative.get("errors")})

    manifest = {
        "tool": "build_mmo_step42_movement_proposal_final_manifest.py",
        "authority_model": "step42_movement_proposal_fall_aware_v1",
        "status": "passed" if not errors else "failed",
        "source_session_key": args.source_session_key,
        "statuses": statuses,
        "artifacts": {
            "positive_manifest": {"path": str(args.positive_manifest), "sha256": sha256(args.positive_manifest)},
            "negative_suite": {"path": str(args.negative_suite), "sha256": sha256(args.negative_suite)},
        },
        "positive_summary": None if positive is None else positive.get("proposal_summary"),
        "mysql_summary": None if positive is None else positive.get("mysql_summary"),
        "negative_summary": None if negative is None else {
            "scenario_count": len(negative.get("scenario_results", {})),
            "scenarios": {
                name: {
                    "status": info.get("status"),
                    "expected_reject_reason": info.get("expected_reject_reason"),
                    "reject_reasons": info.get("reject_reasons"),
                    "accepted_rows": info.get("accepted_rows"),
                    "rejected_rows": info.get("rejected_rows"),
                }
                for name, info in sorted((negative.get("scenario_results") or {}).items())
            },
        },
        "errors": errors,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True), encoding="utf-8")
    if errors:
        for e in errors:
            print("ERROR:", e)
    else:
        print("OK: Step42 final movement proposal manifest:", statuses)
    print(f"artifact={args.output}")
    print(f"status={manifest['status']}")
    return 0 if manifest["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
