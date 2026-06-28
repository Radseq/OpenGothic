#!/usr/bin/env python3
"""Build hostile Step40 movement-authority JSONL scenarios from a clean capture.

The generated files are deterministic negative fixtures. They are not gameplay
captures and must never be replayed into MySQL directly. They prove the server
movement authority gate rejects impossible checkpoint proposals before
persistence.
"""
from __future__ import annotations

import argparse
import copy
import json
import math
from pathlib import Path
from typing import Any

STEP_KIND = "character_checkpoint"


def load_rows(path: Path, session_key: str) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    if not path.exists():
        raise SystemExit(f"source jsonl missing: {path}")
    with path.open("r", encoding="utf-8", errors="replace") as f:
        for line_no, raw in enumerate(f, 1):
            text = raw.strip()
            if not text:
                continue
            try:
                obj = json.loads(text)
            except json.JSONDecodeError as exc:
                raise SystemExit(f"line {line_no}: invalid json: {exc}") from exc
            if not isinstance(obj, dict) or obj.get("action_kind") != STEP_KIND:
                continue
            idem = str(obj.get("idempotency_key") or "")
            if session_key and not idem.startswith(session_key + ":"):
                continue
            payload = obj.get("payload")
            if isinstance(payload, dict) and all(k in payload for k in ("pos_x", "pos_y", "pos_z")):
                rows.append(obj)
    if len(rows) < 4:
        raise SystemExit(f"need at least 4 checkpoint rows for negative corpus, got {len(rows)}")
    return rows


def write_jsonl(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as f:
        for row in rows:
            f.write(json.dumps(row, ensure_ascii=False, separators=(",", ":")) + "\n")


def payload(row: dict[str, Any]) -> dict[str, Any]:
    p = row.get("payload")
    if not isinstance(p, dict):
        p = {}
        row["payload"] = p
    return p


def as_float(value: Any, fallback: float = 0.0) -> float:
    try:
        f = float(value)
    except (TypeError, ValueError):
        return fallback
    return f if math.isfinite(f) else fallback


def as_int(value: Any, fallback: int = 0) -> int:
    try:
        return int(value)
    except (TypeError, ValueError):
        return fallback


def add_marker(row: dict[str, Any], scenario: str, expected: str) -> None:
    p = payload(row)
    p["authority_negative_scenario"] = scenario
    p["authority_expected_reject_reason"] = expected


def scenario_teleport(rows: list[dict[str, Any]], amount: float) -> list[dict[str, Any]]:
    out = copy.deepcopy(rows)
    idx = max(1, len(out) // 2)
    p = payload(out[idx])
    p["pos_x"] = as_float(p.get("pos_x")) + amount
    p["pos_z"] = as_float(p.get("pos_z")) + amount
    add_marker(out[idx], "teleport_xz", "step_distance_too_large")
    return out


def scenario_vertical_spike(rows: list[dict[str, Any]], amount: float) -> list[dict[str, Any]]:
    out = copy.deepcopy(rows)
    idx = max(1, len(out) // 2)
    p = payload(out[idx])
    p["pos_y"] = as_float(p.get("pos_y")) + amount
    add_marker(out[idx], "vertical_spike", "vertical_delta_too_large")
    return out


def scenario_time_reversal(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    out = copy.deepcopy(rows)
    idx = max(2, len(out) // 2)
    prev_tick = as_int(out[idx - 1].get("client_tick", payload(out[idx - 1]).get("client_tick")), 0)
    bad_tick = max(0, prev_tick - 1)
    out[idx]["client_tick"] = bad_tick
    payload(out[idx])["client_tick"] = bad_tick
    add_marker(out[idx], "time_reversal", "non_monotonic_tick")
    return out


def scenario_out_of_bounds(rows: list[dict[str, Any]], amount: float) -> list[dict[str, Any]]:
    out = copy.deepcopy(rows)
    idx = max(1, len(out) // 2)
    p = payload(out[idx])
    p["pos_x"] = as_float(p.get("pos_x")) + amount
    add_marker(out[idx], "outside_world_bounds", "outside_world_bounds")
    return out


def scenario_invalid_position(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    out = copy.deepcopy(rows)
    idx = max(1, len(out) // 2)
    p = payload(out[idx])
    p["pos_x"] = None
    add_marker(out[idx], "invalid_position", "invalid_position")
    return out


def scenario_duplicate_idempotency(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    out = copy.deepcopy(rows)
    idx = max(1, len(out) // 2)
    out[idx]["idempotency_key"] = str(out[idx - 1].get("idempotency_key") or "")
    add_marker(out[idx], "duplicate_idempotency", "duplicate_idempotency_source_error")
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description="Build Step40 movement-authority negative JSONL corpus")
    ap.add_argument("--jsonl", required=True, type=Path)
    ap.add_argument("--session-key", required=True)
    ap.add_argument("--output-dir", type=Path, default=Path("runtime/step40_movement_negative_corpus"))
    ap.add_argument("--manifest", type=Path, default=Path("runtime/step40_movement_negative_corpus/manifest.json"))
    ap.add_argument("--teleport-distance", type=float, default=10000.0)
    ap.add_argument("--vertical-spike", type=float, default=1800.0)
    ap.add_argument("--bounds-offset", type=float, default=1000000.0)
    ap.add_argument("--include-duplicate-idempotency", action="store_true")
    args = ap.parse_args()

    source = load_rows(args.jsonl, args.session_key)
    args.output_dir.mkdir(parents=True, exist_ok=True)

    scenarios: list[dict[str, Any]] = []
    definitions: list[tuple[str, str, str, list[dict[str, Any]]]] = [
        ("teleport_xz", "must_reject_and_pass", "step_distance_too_large", scenario_teleport(source, args.teleport_distance)),
        ("vertical_spike", "must_reject_and_pass", "vertical_delta_too_large", scenario_vertical_spike(source, args.vertical_spike)),
        ("time_reversal", "must_reject_and_pass", "non_monotonic_tick", scenario_time_reversal(source)),
        ("outside_world_bounds", "must_reject_and_pass", "outside_world_bounds", scenario_out_of_bounds(source, args.bounds_offset)),
        ("invalid_position", "must_reject_and_pass", "invalid_position", scenario_invalid_position(source)),
    ]
    if args.include_duplicate_idempotency:
        definitions.append(("duplicate_idempotency", "must_fail", "duplicate_idempotency_source_error", scenario_duplicate_idempotency(source)))

    for name, expectation, expected_reason, rows in definitions:
        path = args.output_dir / f"{name}.jsonl"
        write_jsonl(path, rows)
        scenarios.append({
            "name": name,
            "expectation": expectation,
            "expected_reject_reason": expected_reason,
            "jsonl": str(path),
            "rows": len(rows),
        })

    manifest = {
        "tool": "build_mmo_step40_movement_negative_corpus.py",
        "status": "passed",
        "source_jsonl": str(args.jsonl),
        "session_key": args.session_key,
        "source_rows": len(source),
        "output_dir": str(args.output_dir),
        "scenarios": scenarios,
    }
    args.manifest.parent.mkdir(parents=True, exist_ok=True)
    args.manifest.write_text(json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True), encoding="utf-8")
    print("OK: Step40 movement negative corpus:", {"source_rows": len(source), "scenarios": len(scenarios)})
    print(f"manifest={args.manifest}")
    print("status=passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
