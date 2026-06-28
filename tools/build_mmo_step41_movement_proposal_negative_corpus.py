#!/usr/bin/env python3
"""Build hostile Step41/42 movement_proposal JSONL scenarios.

The source must be a valid movement proposal capture. Each generated file keeps
most rows intact and mutates one mid-stream proposal so the authority checker can
prove it rejects the hostile segment and then rejects dependent stale follow-up
proposals by continuity, without replaying them to MySQL.
"""
from __future__ import annotations

import argparse
import copy
import json
from pathlib import Path
from typing import Any

MOTION_KEYS = (
    "from_is_in_air", "from_is_falling", "from_is_falling_deep", "from_is_slide", "from_is_jump", "from_is_jump_up",
    "from_is_swim", "from_is_dive", "from_is_in_water",
    "to_is_in_air", "to_is_falling", "to_is_falling_deep", "to_is_slide", "to_is_jump", "to_is_jump_up",
    "to_is_swim", "to_is_dive", "to_is_in_water",
)


def load_rows(path: Path, session_key: str) -> list[dict[str, Any]]:
    out: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8", errors="replace") as f:
        for raw in f:
            raw = raw.strip()
            if not raw:
                continue
            obj = json.loads(raw)
            if not isinstance(obj, dict) or obj.get("action_kind") != "movement_proposal":
                continue
            idem = str(obj.get("idempotency_key") or "")
            if idem.startswith(session_key + ":"):
                out.append(obj)
    return out


def write_jsonl(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as f:
        for row in rows:
            f.write(json.dumps(row, ensure_ascii=False, separators=(",", ":")) + "\n")


def p(row: dict[str, Any]) -> dict[str, Any]:
    payload = row.setdefault("payload", {})
    if not isinstance(payload, dict):
        row["payload"] = {}
    return row["payload"]


def mark(row: dict[str, Any], scenario: str) -> None:
    p(row)["negative_scenario"] = scenario
    row["idempotency_key"] = str(row.get("idempotency_key") or "movement_proposal") + f":negative:{scenario}"


def clone_with_mutation(rows: list[dict[str, Any]], mutate_index: int, scenario: str, fn) -> list[dict[str, Any]]:
    out = copy.deepcopy(rows)
    row = out[mutate_index]
    mark(row, scenario)
    fn(row)
    return out


def clear_motion_flags(payload: dict[str, Any]) -> None:
    for key in MOTION_KEYS:
        payload[key] = False


def set_fall_flags(payload: dict[str, Any]) -> None:
    payload["from_is_in_air"] = True
    payload["from_is_falling"] = True
    payload["to_is_in_air"] = True
    payload["to_is_falling"] = True


def main() -> int:
    ap = argparse.ArgumentParser(description="Build Step41/42 movement proposal negative corpus")
    ap.add_argument("--jsonl", required=True, type=Path)
    ap.add_argument("--session-key", required=True)
    ap.add_argument("--output-dir", required=True, type=Path)
    ap.add_argument("--output", type=Path, default=None, help="Manifest path; defaults to output-dir/manifest.json")
    ap.add_argument("--mutate-index", type=int, default=-1, help="0-based row index to mutate; defaults to middle after at least two clean accepted rows")
    ap.add_argument("--teleport-distance", type=float, default=10000.0)
    ap.add_argument("--upward-delta", type=float, default=5000.0)
    ap.add_argument("--drop-delta", type=float, default=5000.0)
    ap.add_argument("--impossible-fall-delta", type=float, default=30000.0)
    args = ap.parse_args()

    rows = load_rows(args.jsonl, args.session_key)
    if len(rows) < 5:
        raise SystemExit(f"Need at least 5 movement_proposal rows, got {len(rows)}")
    mutate_index = args.mutate_index if args.mutate_index >= 0 else max(2, len(rows) // 2)
    if mutate_index >= len(rows):
        raise SystemExit(f"mutate-index {mutate_index} outside rows={len(rows)}")

    scenarios: dict[str, tuple[str, list[dict[str, Any]]]] = {}

    def teleport(row: dict[str, Any]) -> None:
        payload = p(row)
        payload["to_pos_x"] = float(payload.get("to_pos_x") or 0.0) + args.teleport_distance

    def fly_upward(row: dict[str, Any]) -> None:
        payload = p(row)
        payload["to_pos_y"] = float(payload.get("from_pos_y") or payload.get("to_pos_y") or 0.0) + args.upward_delta
        clear_motion_flags(payload)

    def unmarked_drop(row: dict[str, Any]) -> None:
        payload = p(row)
        payload["to_pos_y"] = float(payload.get("from_pos_y") or payload.get("to_pos_y") or 0.0) - args.drop_delta
        clear_motion_flags(payload)

    def impossible_fall(row: dict[str, Any]) -> None:
        payload = p(row)
        payload["to_pos_y"] = float(payload.get("from_pos_y") or payload.get("to_pos_y") or 0.0) - args.impossible_fall_delta
        set_fall_flags(payload)

    def time_reversal(row: dict[str, Any]) -> None:
        payload = p(row)
        payload["to_tick"] = int(payload.get("from_tick") or 0) - 1
        payload["delta_ms"] = -1

    def invalid_position(row: dict[str, Any]) -> None:
        payload = p(row)
        payload["to_pos_y"] = "nan"

    builders = {
        "teleport_xz": ("horizontal_speed_too_large", teleport),
        "fly_upward": ("upward_delta_too_large", fly_upward),
        "unmarked_downward_drop": ("unexpected_downward_drop", unmarked_drop),
        "impossible_fall": ("fall_delta_too_large", impossible_fall),
        "time_reversal": ("non_positive_delta", time_reversal),
        "invalid_position": ("invalid_position", invalid_position),
    }

    args.output_dir.mkdir(parents=True, exist_ok=True)
    manifest_scenarios: dict[str, Any] = {}
    for name, (expected, fn) in builders.items():
        scenario_rows = clone_with_mutation(rows, mutate_index, name, fn)
        path = args.output_dir / f"{name}.jsonl"
        write_jsonl(path, scenario_rows)
        manifest_scenarios[name] = {
            "jsonl": str(path),
            "expected_reject_reason": expected,
            "mutate_index": mutate_index,
        }

    manifest = {
        "tool": "build_mmo_step41_movement_proposal_negative_corpus.py",
        "status": "passed",
        "source_jsonl": str(args.jsonl),
        "session_key": args.session_key,
        "source_rows": len(rows),
        "mutate_index": mutate_index,
        "scenarios": manifest_scenarios,
    }
    out = args.output or (args.output_dir / "manifest.json")
    out.write_text(json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True), encoding="utf-8")
    print("OK: Step42 movement proposal negative corpus:", {"source_rows": len(rows), "scenarios": len(manifest_scenarios), "mutate_index": mutate_index})
    print(f"manifest={out}")
    print("status=passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
