#!/usr/bin/env python3
"""Server-side Step40 movement authority validator for Step39 checkpoint streams.

This is intentionally conservative and offline/dev-first. It treats captured
``character_checkpoint`` envelopes as movement proposals and builds an accepted
stream that can be replayed through the existing receiver -> outbox -> worker ->
MySQL checkpoint chain. Impossible jumps are written to a rejection JSONL instead
of being silently persisted.

The production model stays server-authoritative. This tool is the validation
contract/proof harness before a real online movement RPC exists.
"""
from __future__ import annotations

import argparse
import json
import math
from collections import Counter
from dataclasses import dataclass, asdict
from pathlib import Path
from typing import Any

STEP_KIND = "character_checkpoint"


@dataclass(frozen=True)
class Position:
    x: float
    y: float
    z: float


@dataclass
class Decision:
    index: int
    line_no: int
    idempotency_key: str
    client_tick: int | None
    accepted: bool
    reason: str
    segment_distance: float | None = None
    horizontal_distance: float | None = None
    vertical_delta: float | None = None
    tick_delta: int | None = None
    horizontal_speed: float | None = None
    vertical_speed: float | None = None


def payload_of(row: dict[str, Any]) -> dict[str, Any]:
    payload = row.get("payload")
    return payload if isinstance(payload, dict) else {}


def as_float(value: Any) -> float | None:
    try:
        f = float(value)
    except (TypeError, ValueError):
        return None
    return f if math.isfinite(f) else None


def as_int(value: Any) -> int | None:
    try:
        return int(value)
    except (TypeError, ValueError):
        return None


def position_of(row: dict[str, Any]) -> Position | None:
    payload = payload_of(row)
    x = as_float(payload.get("pos_x"))
    y = as_float(payload.get("pos_y"))
    z = as_float(payload.get("pos_z"))
    if x is None or y is None or z is None:
        return None
    return Position(x, y, z)


def tick_of(row: dict[str, Any]) -> int | None:
    payload = payload_of(row)
    return as_int(row.get("client_tick") if row.get("client_tick") is not None else payload.get("client_tick"))


def dist(a: Position, b: Position) -> float:
    return math.sqrt((a.x - b.x) ** 2 + (a.y - b.y) ** 2 + (a.z - b.z) ** 2)


def hdist(a: Position, b: Position) -> float:
    return math.sqrt((a.x - b.x) ** 2 + (a.z - b.z) ** 2)


def within_bounds(p: Position, bounds: tuple[float, float, float, float, float, float] | None) -> bool:
    if bounds is None:
        return True
    min_x, max_x, min_y, max_y, min_z, max_z = bounds
    return min_x <= p.x <= max_x and min_y <= p.y <= max_y and min_z <= p.z <= max_z


def parse_bounds(values: list[float] | None) -> tuple[float, float, float, float, float, float] | None:
    if values is None:
        return None
    if len(values) != 6:
        raise argparse.ArgumentTypeError("bounds require exactly 6 numbers: min_x max_x min_y max_y min_z max_z")
    min_x, max_x, min_y, max_y, min_z, max_z = values
    if min_x > max_x or min_y > max_y or min_z > max_z:
        raise argparse.ArgumentTypeError("bounds min values must be <= max values")
    return (min_x, max_x, min_y, max_y, min_z, max_z)


def load_rows(path: Path, session_key: str | None) -> tuple[list[tuple[int, dict[str, Any]]], list[str]]:
    errors: list[str] = []
    rows: list[tuple[int, dict[str, Any]]] = []
    if not path.exists():
        return rows, [f"missing jsonl: {path}"]
    with path.open("r", encoding="utf-8", errors="replace") as f:
        for line_no, raw in enumerate(f, 1):
            text = raw.strip()
            if not text:
                continue
            try:
                obj = json.loads(text)
            except json.JSONDecodeError as exc:
                errors.append(f"line {line_no}: invalid json: {exc}")
                continue
            if not isinstance(obj, dict):
                continue
            if obj.get("action_kind") != STEP_KIND:
                continue
            if session_key:
                idem = str(obj.get("idempotency_key") or "")
                if not idem.startswith(session_key + ":"):
                    continue
            rows.append((line_no, obj))
    return rows, errors


def dump_jsonl(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as f:
        for row in rows:
            f.write(json.dumps(row, ensure_ascii=False, separators=(",", ":")) + "\n")


def annotated_rejection(row: dict[str, Any], decision: Decision) -> dict[str, Any]:
    out = dict(row)
    payload = dict(payload_of(row))
    payload["authority_rejection"] = {
        "reason": decision.reason,
        "segment_distance": decision.segment_distance,
        "horizontal_distance": decision.horizontal_distance,
        "vertical_delta": decision.vertical_delta,
        "tick_delta": decision.tick_delta,
        "horizontal_speed": decision.horizontal_speed,
        "vertical_speed": decision.vertical_speed,
    }
    out["payload"] = payload
    return out


def bbox(positions: list[Position]) -> dict[str, float] | None:
    if not positions:
        return None
    return {
        "min_x": min(p.x for p in positions),
        "max_x": max(p.x for p in positions),
        "min_y": min(p.y for p in positions),
        "max_y": max(p.y for p in positions),
        "min_z": min(p.z for p in positions),
        "max_z": max(p.z for p in positions),
    }


def main() -> int:
    ap = argparse.ArgumentParser(description="Validate Step40 server-side movement authority from character_checkpoint JSONL")
    ap.add_argument("--jsonl", required=True, type=Path)
    ap.add_argument("--session-key", default="", help="Source capture session key prefix. Empty means all character_checkpoint rows.")
    ap.add_argument("--accepted-jsonl", type=Path, default=Path("runtime/mmo_step40_movement_authority.accepted.jsonl"))
    ap.add_argument("--rejected-jsonl", type=Path, default=Path("runtime/mmo_step40_movement_authority.rejected.jsonl"))
    ap.add_argument("--output", type=Path, default=Path("runtime/mmo_step40_movement_authority.json"))
    ap.add_argument("--min-accepted", type=int, default=2)
    ap.add_argument("--require-position-change", action="store_true")
    ap.add_argument("--allow-rejections", action="store_true", help="Do not fail the report when rejected proposals exist.")
    ap.add_argument("--min-rejected", type=int, default=0, help="Require at least this many rejected proposals. Useful for hostile/negative authority tests.")
    ap.add_argument("--max-rejected", type=int, default=-1, help="Require no more than this many rejected proposals. -1 disables this check.")
    ap.add_argument("--require-reject-reason", action="append", default=[], help="Require a specific rejection reason to appear. Can be repeated.")
    ap.add_argument("--stationary-epsilon", type=float, default=0.01)
    ap.add_argument("--tick-rate", type=float, default=1000.0, help="Client ticks per second. Step39 checkpoint ticks are millisecond-like in current evidence.")
    ap.add_argument("--min-tick-delta", type=int, default=1)
    ap.add_argument("--max-tick-delta", type=int, default=15000)
    ap.add_argument("--max-step-distance", type=float, default=2500.0)
    ap.add_argument("--max-horizontal-speed", type=float, default=2500.0, help="World units per second.")
    ap.add_argument("--max-vertical-speed", type=float, default=2500.0, help="World units per second.")
    ap.add_argument("--max-vertical-delta", type=float, default=1600.0)
    ap.add_argument("--bounds", type=float, nargs=6, metavar=("MIN_X", "MAX_X", "MIN_Y", "MAX_Y", "MIN_Z", "MAX_Z"), help="Optional world AABB accepted by this server shard.")
    ap.add_argument("--allow-reason", action="append", default=[], help="Allowed payload.reason. Can be repeated. Empty means all reasons accepted.")
    args = ap.parse_args()

    bounds = parse_bounds(args.bounds)
    source_rows, errors = load_rows(args.jsonl, args.session_key or None)
    allowed_reasons = set(args.allow_reason)
    idempotency = Counter(str(row.get("idempotency_key") or "") for _, row in source_rows)
    dupes = {k: v for k, v in idempotency.items() if k and v > 1}
    if dupes:
        errors.append("duplicate idempotency keys in source: " + json.dumps(dupes, sort_keys=True))

    accepted: list[dict[str, Any]] = []
    rejected: list[dict[str, Any]] = []
    accepted_positions: list[Position] = []
    all_positions: list[Position] = []
    decisions: list[Decision] = []
    reject_reasons: Counter[str] = Counter()
    input_reasons: Counter[str] = Counter()

    last_pos: Position | None = None
    last_tick: int | None = None
    total_distance = 0.0
    max_step = 0.0
    max_hspeed = 0.0
    max_vspeed = 0.0

    for idx, (line_no, row) in enumerate(source_rows):
        payload = payload_of(row)
        reason = str(payload.get("reason") or "")
        if reason:
            input_reasons[reason] += 1
        idem = str(row.get("idempotency_key") or "")
        pos = position_of(row)
        tick = tick_of(row)
        if pos is not None:
            all_positions.append(pos)

        reject = ""
        d = hd = vd = hspeed = vspeed = None
        tick_delta = None

        if pos is None:
            reject = "invalid_position"
        elif tick is None:
            reject = "invalid_tick"
        elif allowed_reasons and reason not in allowed_reasons:
            reject = "reason_not_allowed"
        elif not within_bounds(pos, bounds):
            reject = "outside_world_bounds"
        elif last_pos is not None and last_tick is not None:
            tick_delta = tick - last_tick
            d = dist(last_pos, pos)
            hd = hdist(last_pos, pos)
            vd = abs(pos.y - last_pos.y)
            if tick_delta <= 0:
                reject = "non_monotonic_tick"
            elif tick_delta < args.min_tick_delta:
                reject = "tick_delta_too_small"
            elif args.max_tick_delta > 0 and tick_delta > args.max_tick_delta:
                reject = "tick_delta_too_large"
            else:
                seconds = tick_delta / max(args.tick_rate, 1e-9)
                hspeed = hd / seconds if seconds > 0 else math.inf
                vspeed = vd / seconds if seconds > 0 else math.inf
                if d > args.max_step_distance:
                    reject = "step_distance_too_large"
                elif hspeed > args.max_horizontal_speed:
                    reject = "horizontal_speed_too_large"
                elif vd > args.max_vertical_delta:
                    reject = "vertical_delta_too_large"
                elif vspeed > args.max_vertical_speed:
                    reject = "vertical_speed_too_large"
        # First valid row seeds the server authority state.
        if reject:
            decision = Decision(idx, line_no, idem, tick, False, reject, d, hd, vd, tick_delta, hspeed, vspeed)
            decisions.append(decision)
            reject_reasons[reject] += 1
            rejected.append(annotated_rejection(row, decision))
            continue

        decision = Decision(idx, line_no, idem, tick, True, "accepted", d, hd, vd, tick_delta, hspeed, vspeed)
        decisions.append(decision)
        accepted.append(row)
        if d is not None:
            max_step = max(max_step, d)
        if hspeed is not None and math.isfinite(hspeed):
            max_hspeed = max(max_hspeed, hspeed)
        if vspeed is not None and math.isfinite(vspeed):
            max_vspeed = max(max_vspeed, vspeed)
        if pos is not None:
            if last_pos is not None:
                total_distance += dist(last_pos, pos)
            accepted_positions.append(pos)
            last_pos = pos
        last_tick = tick

    changed = False
    if len(accepted_positions) >= 2:
        first = accepted_positions[0]
        changed = any(dist(first, p) > args.stationary_epsilon for p in accepted_positions[1:])

    if len(accepted) < args.min_accepted:
        errors.append(f"too few accepted movement proposals: {len(accepted)} < {args.min_accepted}")
    if args.require_position_change and not changed:
        errors.append("accepted movement path has no material position change")
    if rejected and not args.allow_rejections:
        errors.append("movement authority rejected proposals: " + json.dumps(dict(sorted(reject_reasons.items())), sort_keys=True))
    if args.min_rejected > 0 and len(rejected) < args.min_rejected:
        errors.append(f"too few rejected movement proposals: {len(rejected)} < {args.min_rejected}")
    if args.max_rejected >= 0 and len(rejected) > args.max_rejected:
        errors.append(f"too many rejected movement proposals: {len(rejected)} > {args.max_rejected}")
    for required_reason in args.require_reject_reason:
        if reject_reasons[required_reason] <= 0:
            errors.append(f"required rejection reason not found: {required_reason}")

    dump_jsonl(args.accepted_jsonl, accepted)
    dump_jsonl(args.rejected_jsonl, rejected)

    report = {
        "tool": "check_mmo_step40_movement_authority.py",
        "status": "passed" if not errors else "failed",
        "source_jsonl": str(args.jsonl),
        "session_key": args.session_key,
        "accepted_jsonl": str(args.accepted_jsonl),
        "rejected_jsonl": str(args.rejected_jsonl),
        "input_rows": len(source_rows),
        "accepted_rows": len(accepted),
        "rejected_rows": len(rejected),
        "reject_reasons": dict(sorted(reject_reasons.items())),
        "input_reasons": dict(sorted(input_reasons.items())),
        "position_changed": changed,
        "accepted_total_distance": round(total_distance, 6),
        "accepted_max_step_distance": round(max_step, 6),
        "accepted_max_horizontal_speed": round(max_hspeed, 6),
        "accepted_max_vertical_speed": round(max_vspeed, 6),
        "accepted_bbox": bbox(accepted_positions),
        "source_bbox": bbox(all_positions),
        "limits": {
            "tick_rate": args.tick_rate,
            "min_tick_delta": args.min_tick_delta,
            "max_tick_delta": args.max_tick_delta,
            "max_step_distance": args.max_step_distance,
            "max_horizontal_speed": args.max_horizontal_speed,
            "max_vertical_speed": args.max_vertical_speed,
            "max_vertical_delta": args.max_vertical_delta,
            "bounds": None if bounds is None else list(bounds),
            "allow_reasons": sorted(allowed_reasons),
            "min_rejected": args.min_rejected,
            "max_rejected": args.max_rejected,
            "require_reject_reasons": sorted(args.require_reject_reason),
        },
        "sample_decisions": [asdict(d) for d in decisions[:50]],
        "errors": errors,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True), encoding="utf-8")

    if errors:
        for err in errors:
            print("ERROR:", err)
    else:
        print("OK: Step40 movement authority:", {"accepted": len(accepted), "rejected": len(rejected), "distance": round(total_distance, 3), "max_hspeed": round(max_hspeed, 3)})
    print(f"accepted_jsonl={args.accepted_jsonl}")
    print(f"rejected_jsonl={args.rejected_jsonl}")
    print(f"artifact={args.output}")
    print(f"status={report['status']}")
    return 0 if not errors else 1


if __name__ == "__main__":
    raise SystemExit(main())
