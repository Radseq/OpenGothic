#!/usr/bin/env python3
"""Validate Step41/42 movement_proposal JSONL and optionally emit accepted checkpoints.

This is a dev server-authority harness: movement_proposal rows are client
proposals/intents. Accepted rows can be converted into bounded
character_checkpoint envelopes for the existing Step39 MySQL checkpoint chain.

Step42 adds fall-aware validation. Gothic/OpenGothic uses Y as height, so a
large negative Y delta may be valid only when the captured movement state says
that the character is airborne/falling/jumping/sliding/swimming/diving. Upward
movement remains strict, while downward fall has its own speed/delta envelope.
"""
from __future__ import annotations

import argparse
import copy
import json
import math
from collections import Counter
from pathlib import Path
from typing import Any

REQUIRED_PAYLOAD = (
    "from_tick", "to_tick", "delta_ms",
    "from_pos_x", "from_pos_y", "from_pos_z",
    "to_pos_x", "to_pos_y", "to_pos_z", "to_rotation_yaw",
)

AIRBORNE_KEYS = (
    "from_is_in_air", "from_is_falling", "from_is_falling_deep", "from_is_jump",
    "from_is_jump_up", "from_is_slide", "from_is_swim", "from_is_dive", "from_is_in_water",
    "to_is_in_air", "to_is_falling", "to_is_falling_deep", "to_is_jump",
    "to_is_jump_up", "to_is_slide", "to_is_swim", "to_is_dive", "to_is_in_water",
)


def fnum(value: Any) -> float | None:
    try:
        v = float(value)
    except (TypeError, ValueError):
        return None
    return v if math.isfinite(v) else None


def inum(value: Any) -> int | None:
    try:
        return int(value)
    except (TypeError, ValueError):
        return None


def bval(value: Any) -> bool:
    if isinstance(value, bool):
        return value
    if isinstance(value, (int, float)):
        return value != 0
    if isinstance(value, str):
        return value.strip().casefold() in {"1", "true", "yes", "y", "on"}
    return False


def dist3(a: tuple[float, float, float], b: tuple[float, float, float]) -> float:
    return math.sqrt((a[0]-b[0])**2 + (a[1]-b[1])**2 + (a[2]-b[2])**2)


def dist2(a: tuple[float, float, float], b: tuple[float, float, float]) -> float:
    return math.sqrt((a[0]-b[0])**2 + (a[2]-b[2])**2)


def within_bounds(pos: tuple[float, float, float], args: argparse.Namespace) -> bool:
    if args.bounds_min_x is not None and pos[0] < args.bounds_min_x: return False
    if args.bounds_max_x is not None and pos[0] > args.bounds_max_x: return False
    if args.bounds_min_y is not None and pos[1] < args.bounds_min_y: return False
    if args.bounds_max_y is not None and pos[1] > args.bounds_max_y: return False
    if args.bounds_min_z is not None and pos[2] < args.bounds_min_z: return False
    if args.bounds_max_z is not None and pos[2] > args.bounds_max_z: return False
    return True


def reject(row: dict[str, Any], reason: str, detail: str) -> dict[str, Any]:
    out = copy.deepcopy(row)
    payload = out.setdefault("payload", {})
    if not isinstance(payload, dict):
        payload = {}
        out["payload"] = payload
    payload["authority_reject_reason"] = reason
    payload["authority_reject_detail"] = detail
    payload["authority_model"] = "step42_movement_proposal_fall_aware_v1"
    return out


def health_drop(p: dict[str, Any]) -> int:
    before = inum(p.get("from_health_current"))
    after = inum(p.get("health_current"))
    if before is None or after is None:
        return 0
    return max(0, before - after)


def is_airborne_context(p: dict[str, Any]) -> bool:
    return any(bval(p.get(k)) for k in AIRBORNE_KEYS)


def checkpoint_from_proposal(row: dict[str, Any], accepted_index: int) -> dict[str, Any]:
    p = row.get("payload") if isinstance(row.get("payload"), dict) else {}
    out = copy.deepcopy(row)
    old_idem = str(out.get("idempotency_key") or f"movement_proposal:{accepted_index}")
    out["action_kind"] = "character_checkpoint"
    out["event_type"] = "character_position_checkpoint"
    out["event_class"] = "character"
    out["procedure"] = "mmo_checkpoint_character_state"
    out["target_key"] = "character:PC_HERO:checkpoint"
    out["client_tick"] = int(p.get("to_tick") or out.get("client_tick") or 0)
    out["idempotency_key"] = f"{old_idem}:accepted_checkpoint"
    out["local_sequence"] = int(out.get("local_sequence") or accepted_index)

    payload: dict[str, Any] = {
        "source": "step42_server_accepted_movement_proposal",
        "actor_key": p.get("actor_key") or "character:PC_HERO",
        "character_key": p.get("character_key") or "PC_HERO",
        "target_key": "character:PC_HERO:checkpoint",
        "pos_x": p.get("to_pos_x"),
        "pos_y": p.get("to_pos_y"),
        "pos_z": p.get("to_pos_z"),
        "rotation_yaw": p.get("to_rotation_yaw"),
        "current_waypoint_key": p.get("current_waypoint_key", ""),
        "reason": "server_accepted_movement_proposal",
        "source_action_kind": "movement_proposal",
        "source_idempotency_key": old_idem,
        "source_from_tick": p.get("from_tick"),
        "source_to_tick": p.get("to_tick"),
        "source_delta_ms": p.get("delta_ms"),
        "source_vertical_axis": p.get("vertical_axis", "y"),
        "source_from_health_current": p.get("from_health_current"),
        "source_health_drop": health_drop(p),
        "source_from_is_in_air": p.get("from_is_in_air"),
        "source_to_is_in_air": p.get("to_is_in_air"),
        "source_from_is_falling": p.get("from_is_falling"),
        "source_to_is_falling": p.get("to_is_falling"),
        "authority_model": "step42_movement_proposal_fall_aware_v1",
        "level": p.get("level"),
        "experience": p.get("experience"),
        "experience_next": p.get("experience_next"),
        "learning_points": p.get("learning_points"),
        "health_current": p.get("health_current"),
        "health_max": p.get("health_max"),
        "mana_current": p.get("mana_current"),
        "mana_max": p.get("mana_max"),
        "strength": p.get("strength"),
        "dexterity": p.get("dexterity"),
        "guild": p.get("guild"),
        "true_guild": p.get("true_guild"),
        "permanent_attitude": p.get("permanent_attitude"),
        "temporary_attitude": p.get("temporary_attitude"),
        "world": p.get("world"),
        "client_tick": p.get("to_tick") or out.get("client_tick"),
        "actor_position": {
            "x": p.get("to_pos_x"),
            "y": p.get("to_pos_y"),
            "z": p.get("to_pos_z"),
        },
    }
    out["payload"] = {k: v for k, v in payload.items() if v is not None}
    return out


def write_jsonl(path: Path | None, rows: list[dict[str, Any]]) -> None:
    if path is None:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as f:
        for row in rows:
            f.write(json.dumps(row, ensure_ascii=False, separators=(",", ":")) + "\n")


def main() -> int:
    ap = argparse.ArgumentParser(description="Validate Step41/42 movement_proposal JSONL")
    ap.add_argument("--jsonl", required=True, type=Path)
    ap.add_argument("--session-key", required=True)
    ap.add_argument("--output", required=True, type=Path)
    ap.add_argument("--accepted-jsonl", type=Path)
    ap.add_argument("--rejected-jsonl", type=Path)
    ap.add_argument("--accepted-checkpoint-jsonl", type=Path)
    ap.add_argument("--min-accepted", type=int, default=1)
    ap.add_argument("--min-rejected", type=int, default=0)
    ap.add_argument("--max-rejected", type=int, default=-1)
    ap.add_argument("--require-position-change", action="store_true")
    ap.add_argument("--require-reject-reason", action="append", default=[])
    ap.add_argument("--max-step-distance", type=float, default=2500.0, help="Max 3D segment distance for non-fall movement; fall uses horizontal limit plus fall-specific limits.")
    ap.add_argument("--max-horizontal-speed", type=float, default=2500.0)
    ap.add_argument("--max-vertical-speed", type=float, default=2500.0, help="Legacy symmetric vertical speed limit used for small non-fall vertical movement.")
    ap.add_argument("--max-vertical-delta", type=float, default=1600.0, help="Legacy symmetric vertical delta limit used for small non-fall vertical movement.")
    ap.add_argument("--max-upward-speed", type=float, default=-1.0)
    ap.add_argument("--max-upward-delta", type=float, default=-1.0)
    ap.add_argument("--max-fall-speed", type=float, default=9000.0)
    ap.add_argument("--max-fall-delta", type=float, default=12000.0)
    ap.add_argument("--large-fall-delta", type=float, default=800.0)
    ap.add_argument("--allow-unmarked-small-down-step", type=float, default=250.0)
    ap.add_argument("--continuity-epsilon", type=float, default=1.0)
    ap.add_argument("--disable-continuity-check", action="store_true")
    ap.add_argument("--require-motion-state-for-large-fall", action="store_true")
    ap.add_argument("--require-fall-state-for-health-drop", action="store_true")
    ap.add_argument("--bounds-min-x", type=float)
    ap.add_argument("--bounds-max-x", type=float)
    ap.add_argument("--bounds-min-y", type=float)
    ap.add_argument("--bounds-max-y", type=float)
    ap.add_argument("--bounds-min-z", type=float)
    ap.add_argument("--bounds-max-z", type=float)
    args = ap.parse_args()

    max_upward_speed = args.max_vertical_speed if args.max_upward_speed < 0 else args.max_upward_speed
    max_upward_delta = args.max_vertical_delta if args.max_upward_delta < 0 else args.max_upward_delta

    rows: list[dict[str, Any]] = []
    errors: list[str] = []
    with args.jsonl.open("r", encoding="utf-8", errors="replace") as f:
        for line_no, raw in enumerate(f, 1):
            text = raw.strip()
            if not text:
                continue
            try:
                obj = json.loads(text)
            except json.JSONDecodeError as exc:
                errors.append(f"line {line_no}: invalid JSON: {exc}")
                continue
            if not isinstance(obj, dict) or obj.get("action_kind") != "movement_proposal":
                continue
            idem = str(obj.get("idempotency_key") or "")
            if not idem.startswith(args.session_key + ":"):
                continue
            rows.append(obj)

    accepted: list[dict[str, Any]] = []
    rejected: list[dict[str, Any]] = []
    accepted_checkpoints: list[dict[str, Any]] = []
    reject_reasons: Counter[str] = Counter()
    reason_counts: Counter[str] = Counter()
    seen_idem: set[str] = set()
    last_to_tick: int | None = None
    last_accepted_to_pos: tuple[float, float, float] | None = None
    continuity_rejects = 0
    total_distance = 0.0
    max_step = 0.0
    max_hspeed = 0.0
    max_vspeed = 0.0
    max_up_speed_observed = 0.0
    max_fall_speed_observed = 0.0
    max_up_delta_observed = 0.0
    max_fall_delta_observed = 0.0
    fall_segments = 0
    airborne_segments = 0
    health_drop_segments = 0
    position_changed = False

    def add_reject(row: dict[str, Any], reason: str, detail: str) -> None:
        rejected.append(reject(row, reason, detail))
        reject_reasons[reason] += 1

    for row in rows:
        p = row.get("payload") if isinstance(row.get("payload"), dict) else {}
        idem = str(row.get("idempotency_key") or "")
        if idem in seen_idem:
            add_reject(row, "duplicate_idempotency", idem)
            continue
        seen_idem.add(idem)

        missing = [k for k in REQUIRED_PAYLOAD if k not in p]
        if missing:
            add_reject(row, "missing_payload_fields", ",".join(missing))
            continue
        from_tick = inum(p.get("from_tick")); to_tick = inum(p.get("to_tick")); delta_ms = inum(p.get("delta_ms"))
        fp = (fnum(p.get("from_pos_x")), fnum(p.get("from_pos_y")), fnum(p.get("from_pos_z")))
        tp = (fnum(p.get("to_pos_x")), fnum(p.get("to_pos_y")), fnum(p.get("to_pos_z")))
        if from_tick is None or to_tick is None or delta_ms is None or any(v is None for v in fp + tp):
            add_reject(row, "invalid_position", "non-finite or non-numeric tick/position")
            continue
        from_pos = (float(fp[0]), float(fp[1]), float(fp[2]))
        to_pos = (float(tp[0]), float(tp[1]), float(tp[2]))
        if to_tick <= from_tick or delta_ms <= 0:
            add_reject(row, "non_positive_delta", f"from_tick={from_tick} to_tick={to_tick} delta_ms={delta_ms}")
            continue
        if last_to_tick is not None and to_tick <= last_to_tick:
            add_reject(row, "non_monotonic_tick", f"last_to_tick={last_to_tick} to_tick={to_tick}")
            continue
        if not args.disable_continuity_check and last_to_tick is not None:
            if from_tick != last_to_tick:
                continuity_rejects += 1
                add_reject(row, "from_tick_mismatch", f"from_tick={from_tick} expected={last_to_tick}")
                continue
            if last_accepted_to_pos is not None:
                continuity_gap = dist3(from_pos, last_accepted_to_pos)
                if continuity_gap > args.continuity_epsilon:
                    continuity_rejects += 1
                    add_reject(row, "from_position_mismatch", f"gap={continuity_gap:.3f} max={args.continuity_epsilon:.3f}")
                    continue
        if not within_bounds(to_pos, args):
            add_reject(row, "outside_world_bounds", f"to={to_pos}")
            continue

        step = dist3(from_pos, to_pos)
        hstep = dist2(from_pos, to_pos)
        signed_vdelta = to_pos[1] - from_pos[1]
        upward_delta = max(0.0, signed_vdelta)
        fall_delta = max(0.0, -signed_vdelta)
        seconds = max(delta_ms, 1) / 1000.0
        hspeed = hstep / seconds
        vspeed = abs(signed_vdelta) / seconds
        up_speed = upward_delta / seconds
        fall_speed = fall_delta / seconds
        airborne = is_airborne_context(p)
        if airborne:
            airborne_segments += 1
        if fall_delta > 0.01:
            fall_segments += 1
        if health_drop(p) > 0:
            health_drop_segments += 1

        is_marked_fall = fall_delta > 0.01 and (airborne or fall_delta <= args.allow_unmarked_small_down_step)

        # Horizontal speed is always authoritative. Falling may be fast vertically,
        # but it must not grant impossible lateral motion.
        if hspeed > args.max_horizontal_speed:
            add_reject(row, "horizontal_speed_too_large", f"hspeed={hspeed:.3f} max={args.max_horizontal_speed:.3f}")
            continue

        if upward_delta > 0.01:
            if upward_delta > max_upward_delta:
                add_reject(row, "upward_delta_too_large", f"upward_delta={upward_delta:.3f} max={max_upward_delta:.3f}")
                continue
            if up_speed > max_upward_speed:
                add_reject(row, "upward_speed_too_large", f"upward_speed={up_speed:.3f} max={max_upward_speed:.3f}")
                continue
            if step > args.max_step_distance:
                add_reject(row, "step_distance_too_large", f"step={step:.3f} max={args.max_step_distance:.3f}")
                continue
        elif fall_delta > 0.01:
            if not is_marked_fall and fall_delta > args.max_vertical_delta:
                add_reject(row, "unexpected_downward_drop", f"fall_delta={fall_delta:.3f} without airborne/fall state")
                continue
            if args.require_motion_state_for_large_fall and fall_delta > args.large_fall_delta and not airborne:
                add_reject(row, "fall_without_motion_state", f"fall_delta={fall_delta:.3f} large_fall_delta={args.large_fall_delta:.3f}")
                continue
            if is_marked_fall:
                if fall_delta > args.max_fall_delta:
                    add_reject(row, "fall_delta_too_large", f"fall_delta={fall_delta:.3f} max={args.max_fall_delta:.3f}")
                    continue
                if fall_speed > args.max_fall_speed:
                    add_reject(row, "fall_speed_too_large", f"fall_speed={fall_speed:.3f} max={args.max_fall_speed:.3f}")
                    continue
            else:
                if step > args.max_step_distance:
                    add_reject(row, "step_distance_too_large", f"step={step:.3f} max={args.max_step_distance:.3f}")
                    continue
                if fall_delta > args.max_vertical_delta:
                    add_reject(row, "vertical_delta_too_large", f"vertical_delta={fall_delta:.3f} max={args.max_vertical_delta:.3f}")
                    continue
                if vspeed > args.max_vertical_speed:
                    add_reject(row, "vertical_speed_too_large", f"vspeed={vspeed:.3f} max={args.max_vertical_speed:.3f}")
                    continue
        else:
            if step > args.max_step_distance:
                add_reject(row, "step_distance_too_large", f"step={step:.3f} max={args.max_step_distance:.3f}")
                continue

        if args.require_fall_state_for_health_drop and health_drop(p) > 0 and not airborne:
            add_reject(row, "health_drop_without_motion_state", f"health_drop={health_drop(p)}")
            continue

        accepted.append(row)
        accepted_checkpoints.append(checkpoint_from_proposal(row, len(accepted)))
        reason_counts[str(p.get("reason") or "")] += 1
        last_to_tick = to_tick
        last_accepted_to_pos = to_pos
        total_distance += step
        max_step = max(max_step, step)
        max_hspeed = max(max_hspeed, hspeed)
        max_vspeed = max(max_vspeed, vspeed)
        max_up_speed_observed = max(max_up_speed_observed, up_speed)
        max_fall_speed_observed = max(max_fall_speed_observed, fall_speed)
        max_up_delta_observed = max(max_up_delta_observed, upward_delta)
        max_fall_delta_observed = max(max_fall_delta_observed, fall_delta)
        if step > 0.01:
            position_changed = True

    for required in args.require_reject_reason:
        if reject_reasons[required] <= 0:
            errors.append(f"required reject reason not observed: {required}")
    if len(accepted) < args.min_accepted:
        errors.append(f"accepted rows {len(accepted)} < min {args.min_accepted}")
    if len(rejected) < args.min_rejected:
        errors.append(f"rejected rows {len(rejected)} < min {args.min_rejected}")
    if args.max_rejected >= 0 and len(rejected) > args.max_rejected:
        errors.append(f"rejected rows {len(rejected)} > max {args.max_rejected}")
    if args.require_position_change and not position_changed:
        errors.append("accepted proposals did not change position")

    write_jsonl(args.accepted_jsonl, accepted)
    write_jsonl(args.rejected_jsonl, rejected)
    write_jsonl(args.accepted_checkpoint_jsonl, accepted_checkpoints)

    report = {
        "tool": "check_mmo_step41_movement_proposal_jsonl.py",
        "authority_model": "step42_movement_proposal_fall_aware_v1",
        "status": "failed" if errors else "passed",
        "source_jsonl": str(args.jsonl),
        "session_key": args.session_key,
        "rows": len(rows),
        "accepted_rows": len(accepted),
        "rejected_rows": len(rejected),
        "accepted_checkpoint_rows": len(accepted_checkpoints),
        "position_changed": position_changed,
        "total_distance": round(total_distance, 3),
        "max_step_distance": round(max_step, 3),
        "max_horizontal_speed": round(max_hspeed, 3),
        "max_vertical_speed": round(max_vspeed, 3),
        "max_upward_speed": round(max_up_speed_observed, 3),
        "max_fall_speed": round(max_fall_speed_observed, 3),
        "max_upward_delta": round(max_up_delta_observed, 3),
        "max_fall_delta": round(max_fall_delta_observed, 3),
        "fall_segments": fall_segments,
        "airborne_segments": airborne_segments,
        "health_drop_segments": health_drop_segments,
        "continuity_rejects": continuity_rejects,
        "limits": {
            "max_step_distance": args.max_step_distance,
            "max_horizontal_speed": args.max_horizontal_speed,
            "max_vertical_speed": args.max_vertical_speed,
            "max_vertical_delta": args.max_vertical_delta,
            "max_upward_speed": max_upward_speed,
            "max_upward_delta": max_upward_delta,
            "max_fall_speed": args.max_fall_speed,
            "max_fall_delta": args.max_fall_delta,
            "large_fall_delta": args.large_fall_delta,
            "allow_unmarked_small_down_step": args.allow_unmarked_small_down_step,
            "continuity_epsilon": args.continuity_epsilon,
            "continuity_check": not args.disable_continuity_check,
        },
        "reasons": dict(sorted(reason_counts.items())),
        "reject_reasons": dict(sorted(reject_reasons.items())),
        "accepted_jsonl": None if args.accepted_jsonl is None else str(args.accepted_jsonl),
        "rejected_jsonl": None if args.rejected_jsonl is None else str(args.rejected_jsonl),
        "accepted_checkpoint_jsonl": None if args.accepted_checkpoint_jsonl is None else str(args.accepted_checkpoint_jsonl),
        "errors": errors,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True), encoding="utf-8")
    if errors:
        for e in errors:
            print("ERROR:", e)
    else:
        print("OK: Step42 movement proposals:", {"accepted": len(accepted), "rejected": len(rejected), "distance": report["total_distance"], "max_hspeed": report["max_horizontal_speed"], "fall_segments": fall_segments})
    print(f"artifact={args.output}")
    print(f"status={report['status']}")
    return 0 if not errors else 1


if __name__ == "__main__":
    raise SystemExit(main())
