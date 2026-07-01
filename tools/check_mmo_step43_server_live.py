#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import sys
from collections import Counter
from pathlib import Path
from typing import Any

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")


def load_json(path: Path | None) -> dict[str, Any]:
    if path is None or not path.exists():
        return {}
    obj = json.loads(path.read_text(encoding="utf-8", errors="replace") or "{}")
    return obj if isinstance(obj, dict) else {}


def load_jsonl(path: Path | None) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    if path is None or not path.exists():
        return rows
    with path.open("r", encoding="utf-8", errors="replace") as f:
        for line_no, raw in enumerate(f, 1):
            text = raw.strip()
            if not text:
                continue
            try:
                obj = json.loads(text)
            except json.JSONDecodeError as exc:
                raise SystemExit(f"ERROR: {path}:{line_no}: invalid JSON: {exc}") from exc
            if isinstance(obj, dict):
                rows.append(obj)
    return rows


def reject_reason(row: dict[str, Any]) -> str:
    payload = row.get("payload") if isinstance(row.get("payload"), dict) else {}
    return str(row.get("server_reject_reason") or payload.get("authority_reject_reason") or row.get("error") or "")


def main() -> int:
    ap = argparse.ArgumentParser(description="Check Step43 live MMO server artifacts")
    ap.add_argument("--summary", type=Path, required=True)
    ap.add_argument("--accepted-jsonl", type=Path, required=True)
    ap.add_argument("--rejected-jsonl", type=Path, required=True)
    ap.add_argument("--checkpoint-jsonl", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    ap.add_argument("--session-key", default="")
    ap.add_argument("--min-accepted", type=int, default=1)
    ap.add_argument("--min-accepted-movement-proposals", type=int, default=1)
    ap.add_argument("--min-checkpoints", type=int, default=1)
    ap.add_argument("--min-rejected", type=int, default=0)
    ap.add_argument("--require-reject-reason", action="append", default=[])
    ap.add_argument("--require-fall-segment", action="store_true")
    ap.add_argument("--allow-invalid", action="store_true")
    args = ap.parse_args()

    summary = load_json(args.summary)
    accepted = load_jsonl(args.accepted_jsonl)
    rejected = load_jsonl(args.rejected_jsonl)
    checkpoints = load_jsonl(args.checkpoint_jsonl)

    accepted_kinds = Counter(str(row.get("action_kind") or "") for row in accepted)
    checkpoint_kinds = Counter(str(row.get("action_kind") or "") for row in checkpoints)
    reject_reasons = Counter(reject_reason(row) for row in rejected)
    stats = summary.get("stats") if isinstance(summary.get("stats"), dict) else {}
    movement = summary.get("movement_authority") if isinstance(summary.get("movement_authority"), dict) else {}
    errors: list[str] = []

    if not summary:
        errors.append(f"summary file missing or empty: {args.summary}")
    if summary and int(stats.get("invalid", 0) or 0) > 0 and not args.allow_invalid:
        errors.append(f"server reported invalid packets={stats.get('invalid')}")
    if len(accepted) < args.min_accepted:
        errors.append(f"accepted rows {len(accepted)} < {args.min_accepted}")
    if accepted_kinds.get("movement_proposal", 0) < args.min_accepted_movement_proposals:
        errors.append(f"accepted movement_proposal rows {accepted_kinds.get('movement_proposal', 0)} < {args.min_accepted_movement_proposals}")
    if len(checkpoints) < args.min_checkpoints:
        errors.append(f"checkpoint rows {len(checkpoints)} < {args.min_checkpoints}")
    if checkpoint_kinds.get("character_checkpoint", 0) < args.min_checkpoints:
        errors.append(f"character_checkpoint rows {checkpoint_kinds.get('character_checkpoint', 0)} < {args.min_checkpoints}")
    if len(rejected) < args.min_rejected:
        errors.append(f"rejected rows {len(rejected)} < {args.min_rejected}")
    for reason in args.require_reject_reason:
        if reject_reasons.get(reason, 0) <= 0:
            errors.append(f"required reject reason not observed: {reason}")
    if args.require_fall_segment and int(movement.get("fall_segments", 0) or 0) <= 0:
        errors.append("movement authority did not record a fall segment")
    if args.session_key:
        bad = [row.get("idempotency_key") for row in accepted + checkpoints if not str(row.get("idempotency_key") or "").startswith(args.session_key + ":")]
        # Checkpoint idempotency inherits the source idempotency prefix, so this is strict enough.
        if bad:
            errors.append(f"rows outside session prefix {args.session_key}: {bad[:5]}")

    report = {
        "tool": "check_mmo_step43_server_live.py",
        "status": "failed" if errors else "passed",
        "summary": str(args.summary),
        "accepted_jsonl": str(args.accepted_jsonl),
        "rejected_jsonl": str(args.rejected_jsonl),
        "checkpoint_jsonl": str(args.checkpoint_jsonl),
        "accepted_rows": len(accepted),
        "rejected_rows": len(rejected),
        "checkpoint_rows": len(checkpoints),
        "accepted_kinds": dict(sorted(accepted_kinds.items())),
        "checkpoint_kinds": dict(sorted(checkpoint_kinds.items())),
        "reject_reasons": dict(sorted(reject_reasons.items())),
        "summary_stats": stats,
        "movement_authority": movement,
        "errors": errors,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    if errors:
        for error in errors:
            print("ERROR:", error)
    else:
        print("OK: Step43 live server artifacts:", {"accepted": len(accepted), "checkpoints": len(checkpoints), "rejected": len(rejected), "fall_segments": movement.get("fall_segments", 0)})
    print(f"artifact={args.output}")
    print(f"status={report['status']}")
    return 0 if not errors else 1


if __name__ == "__main__":
    raise SystemExit(main())


