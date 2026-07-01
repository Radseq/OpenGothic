#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any


def read_json(path: Path | None) -> dict[str, Any] | None:
    if path is None or not path.exists():
        return None
    return json.loads(path.read_text(encoding="utf-8"))


def read_jsonl_count(path: Path | None) -> int:
    if path is None or not path.exists():
        return 0
    count = 0
    with path.open("r", encoding="utf-8", errors="replace") as f:
        for line in f:
            if line.strip():
                count += 1
    return count


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description="Build Step45 world-AI/weapon/loot manifest")
    ap.add_argument("--summary-json", required=True)
    ap.add_argument("--domain-check-json", required=True)
    ap.add_argument("--accepted-jsonl", required=True)
    ap.add_argument("--checkpoint-jsonl")
    ap.add_argument("--rejected-jsonl")
    ap.add_argument("--output", required=True)
    args = ap.parse_args(argv)

    summary = read_json(Path(args.summary_json))
    domain_check = read_json(Path(args.domain_check_json))
    if summary is None:
        raise SystemExit(f"missing summary json: {args.summary_json}")
    if domain_check is None:
        raise SystemExit(f"missing domain check json: {args.domain_check_json}")

    status = "passed" if summary.get("stats") and domain_check.get("status") == "passed" else "failed"
    stats = summary.get("stats", {}) if isinstance(summary.get("stats"), dict) else {}
    manifest = {
        "status": status,
        "step": 45,
        "tool": "build_mmo_step45_world_ai_manifest.py",
        "server_summary": summary,
        "domain_check": domain_check,
        "artifact_counts": {
            "accepted_jsonl": read_jsonl_count(Path(args.accepted_jsonl)),
            "checkpoint_jsonl": read_jsonl_count(Path(args.checkpoint_jsonl)) if args.checkpoint_jsonl else 0,
            "rejected_jsonl": read_jsonl_count(Path(args.rejected_jsonl)) if args.rejected_jsonl else 0,
        },
        "highlights": {
            "accepted": stats.get("accepted", 0),
            "invalid": stats.get("invalid", 0),
            "rejected": stats.get("rejected", 0),
            "enqueued": stats.get("enqueued", 0),
            "weapon_state": domain_check.get("domains", {}).get("weapon_state", {}).get("total", 0),
            "corpse_loot": domain_check.get("domains", {}).get("corpse_loot", {}).get("total", 0),
            "combat_damage": domain_check.get("domains", {}).get("combat_damage", {}).get("total", 0),
            "kill": domain_check.get("domains", {}).get("kill", {}).get("total", 0),
            "world_ai": domain_check.get("world_ai_classification", {}),
        },
    }

    out = Path(args.output)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"artifact={out}")
    print(f"status={status}")
    return 0 if status == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
