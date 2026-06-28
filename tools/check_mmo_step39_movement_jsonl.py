#!/usr/bin/env python3
"""Validate Step39 OpenGothic character_checkpoint JSONL capture.

Step39 v2 adds movement-budget diagnostics. The checker still accepts the
original v1 checkpoint stream, but it can now prove that capture was coalesced
on distance/yaw/stat deltas and that the resulting checkpoints contain a sane
movement trail instead of unbounded per-frame/idle spam.
"""
from __future__ import annotations

import argparse
import json
import math
from collections import Counter
from pathlib import Path
from typing import Any


def nested_payload(row: dict[str, Any]) -> dict[str, Any]:
    payload = row.get("payload")
    return payload if isinstance(payload, dict) else {}


def as_float(value: Any) -> float | None:
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def as_int(value: Any) -> int | None:
    try:
        return int(value)
    except (TypeError, ValueError):
        return None


def distance(a: tuple[float, float, float], b: tuple[float, float, float]) -> float:
    return math.sqrt((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2)


def main() -> int:
    ap = argparse.ArgumentParser(description="Check Step39 character checkpoint JSONL evidence")
    ap.add_argument("--jsonl", required=True, type=Path)
    ap.add_argument("--session-key", required=True)
    ap.add_argument("--min-rows", type=int, default=2)
    ap.add_argument("--require-position-change", action="store_true")
    ap.add_argument("--min-total-distance", type=float, default=0.0, help="Minimum cumulative movement distance in Gothic world units.")
    ap.add_argument("--max-stationary-ratio", type=float, default=1.0, help="Fail when too many adjacent checkpoint pairs are effectively stationary.")
    ap.add_argument("--stationary-epsilon", type=float, default=0.01)
    ap.add_argument("--max-step-distance", type=float, default=0.0, help="Optional upper bound for a single checkpoint jump.")
    ap.add_argument("--min-tick-delta", type=int, default=0, help="Optional lower bound for adjacent checkpoint tick deltas.")
    ap.add_argument("--max-tick-delta", type=int, default=0, help="Optional upper bound for adjacent checkpoint tick deltas.")
    ap.add_argument("--require-reasons", nargs="*", default=[], help="Require at least one checkpoint with each listed payload.reason value.")
    ap.add_argument("--output", type=Path, default=Path("runtime/mmo_step39_movement_jsonl_check.json"))
    args = ap.parse_args()

    errors: list[str] = []
    rows: list[dict[str, Any]] = []
    if not args.jsonl.exists():
        errors.append(f"missing jsonl: {args.jsonl}")
    else:
        with args.jsonl.open("r", encoding="utf-8", errors="replace") as f:
            for line_no, line in enumerate(f, 1):
                text = line.strip()
                if not text:
                    continue
                try:
                    obj = json.loads(text)
                except json.JSONDecodeError as exc:
                    errors.append(f"line {line_no}: invalid json: {exc}")
                    continue
                if not isinstance(obj, dict):
                    continue
                if obj.get("action_kind") == "character_checkpoint":
                    rows.append(obj)

    shape_errors: Counter[str] = Counter()
    idems = Counter(str(r.get("idempotency_key") or "") for r in rows)
    positions: list[tuple[float, float, float]] = []
    ticks: list[int] = []
    reasons: Counter[str] = Counter()
    stat_rows = 0

    for row in rows:
        payload = nested_payload(row)
        idem = str(row.get("idempotency_key") or "")
        if not idem.startswith(args.session_key + ":"):
            shape_errors["session_prefix"] += 1
        if str(row.get("target_key") or "") != "character:PC_HERO:checkpoint":
            shape_errors["target_key"] += 1
        for key in ("pos_x", "pos_y", "pos_z", "rotation_yaw", "health_current", "health_max"):
            if key not in payload:
                shape_errors[f"missing_payload_{key}"] += 1
        x = as_float(payload.get("pos_x"))
        y = as_float(payload.get("pos_y"))
        z = as_float(payload.get("pos_z"))
        if x is None or y is None or z is None:
            shape_errors["invalid_position"] += 1
        else:
            positions.append((x, y, z))
        tick = as_int(row.get("client_tick") or payload.get("client_tick") or 0)
        if tick is None:
            shape_errors["invalid_tick"] += 1
        else:
            ticks.append(tick)
        reason = str(payload.get("reason") or "")
        if reason:
            reasons[reason] += 1
        if all(k in payload for k in ("level", "experience", "experience_next", "learning_points", "health_current", "health_max", "mana_current", "mana_max")):
            stat_rows += 1

    duplicate_idem = {k: v for k, v in idems.items() if k and v > 1}
    if len(rows) < args.min_rows:
        errors.append(f"too few character_checkpoint rows: {len(rows)} < {args.min_rows}")
    if duplicate_idem:
        errors.append("duplicate idempotency keys: " + json.dumps(duplicate_idem, sort_keys=True))
    if shape_errors:
        errors.append("shape errors: " + json.dumps(dict(shape_errors), sort_keys=True))

    changed = False
    total_distance = 0.0
    max_step_distance = 0.0
    stationary_pairs = 0
    step_distances: list[float] = []
    if len(positions) >= 2:
        first = positions[0]
        for prev, cur in zip(positions, positions[1:]):
            d = distance(prev, cur)
            step_distances.append(d)
            total_distance += d
            max_step_distance = max(max_step_distance, d)
            if d <= args.stationary_epsilon:
                stationary_pairs += 1
        changed = any(distance(first, p) > args.stationary_epsilon for p in positions[1:])

    tick_deltas: list[int] = []
    if len(ticks) >= 2:
        tick_deltas = [b - a for a, b in zip(ticks, ticks[1:])]
        if any(d < 0 for d in tick_deltas):
            errors.append("client_tick is not monotonic")
        if args.min_tick_delta > 0 and any(d < args.min_tick_delta for d in tick_deltas):
            errors.append(f"checkpoint tick delta below {args.min_tick_delta}: min={min(tick_deltas)}")
        if args.max_tick_delta > 0 and any(d > args.max_tick_delta for d in tick_deltas):
            errors.append(f"checkpoint tick delta above {args.max_tick_delta}: max={max(tick_deltas)}")

    stationary_ratio = 0.0
    if len(positions) >= 2:
        stationary_ratio = stationary_pairs / max(1, len(positions) - 1)
    if args.require_position_change and not changed:
        errors.append("no material position change found")
    if args.min_total_distance > 0 and total_distance < args.min_total_distance:
        errors.append(f"total movement distance too low: {total_distance:.3f} < {args.min_total_distance:.3f}")
    if args.max_stationary_ratio < 1.0 and stationary_ratio > args.max_stationary_ratio:
        errors.append(f"stationary checkpoint ratio too high: {stationary_ratio:.3f} > {args.max_stationary_ratio:.3f}")
    if args.max_step_distance > 0 and max_step_distance > args.max_step_distance:
        errors.append(f"single checkpoint jump too large: {max_step_distance:.3f} > {args.max_step_distance:.3f}")
    for required_reason in args.require_reasons:
        if reasons.get(required_reason, 0) <= 0:
            errors.append(f"missing checkpoint reason: {required_reason}")
    if rows and stat_rows != len(rows):
        errors.append(f"checkpoint stat payload incomplete: {stat_rows}/{len(rows)} rows")

    bbox = None
    if positions:
        xs, ys, zs = zip(*positions)
        bbox = {
            "min_x": min(xs), "max_x": max(xs),
            "min_y": min(ys), "max_y": max(ys),
            "min_z": min(zs), "max_z": max(zs),
        }

    result = {
        "tool": "check_mmo_step39_movement_jsonl.py",
        "status": "passed" if not errors else "failed",
        "jsonl": str(args.jsonl),
        "session_key": args.session_key,
        "rows": len(rows),
        "shape_errors": dict(shape_errors),
        "duplicates": duplicate_idem,
        "position_changed": changed,
        "total_distance": round(total_distance, 6),
        "max_step_distance": round(max_step_distance, 6),
        "stationary_pairs": stationary_pairs,
        "stationary_ratio": round(stationary_ratio, 6),
        "tick_min": min(ticks) if ticks else None,
        "tick_max": max(ticks) if ticks else None,
        "tick_delta_min": min(tick_deltas) if tick_deltas else None,
        "tick_delta_max": max(tick_deltas) if tick_deltas else None,
        "reasons": dict(sorted(reasons.items())),
        "bbox": bbox,
        "errors": errors,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True), encoding="utf-8")
    for error in errors:
        print("ERROR:", error)
    if not errors:
        print("OK: step39 character checkpoints present:", {"rows": len(rows), "position_changed": changed, "total_distance": round(total_distance, 3), "stationary_ratio": round(stationary_ratio, 3)})
    print(f"artifact={args.output}")
    print(f"status={result['status']}")
    return 0 if not errors else 1


if __name__ == "__main__":
    raise SystemExit(main())
