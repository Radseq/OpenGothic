#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from collections import Counter
from pathlib import Path
from typing import Any

STEP37_KINDS = {
    "set_script_int",
    "adjust_progression",
    "apply_experience_reward",
    "update_quest",
    "set_known_dialog",
}


def read_jsonl(path: Path, session_key: str | None) -> tuple[list[dict[str, Any]], list[str]]:
    rows: list[dict[str, Any]] = []
    errors: list[str] = []
    if not path.exists():
        return rows, [f"missing file: {path}"]
    with path.open("r", encoding="utf-8", errors="replace") as f:
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
                errors.append(f"line {line_no}: row is not an object")
                continue
            if session_key:
                idem = str(obj.get("idempotency_key") or "")
                if not idem.startswith(session_key + ":"):
                    continue
            rows.append(obj)
    return rows, errors


def payload(obj: dict[str, Any]) -> dict[str, Any]:
    p = obj.get("payload")
    return p if isinstance(p, dict) else {}


def field(obj: dict[str, Any], *names: str) -> Any:
    p = payload(obj)
    for name in names:
        if name in p:
            return p[name]
    for name in names:
        if name in obj:
            return obj[name]
    return None


def non_empty(value: Any) -> bool:
    return value is not None and value != ""


def validate_row(obj: dict[str, Any]) -> list[str]:
    kind = str(obj.get("action_kind") or "")
    missing: list[str] = []
    common = ["idempotency_key", "local_sequence", "target_key", "client_tick"]
    for name in common:
        if not non_empty(obj.get(name)):
            missing.append(name)
    if kind == "set_script_int":
        for name in ("script_key", "value_after"):
            if not non_empty(field(obj, name, "global_key", "symbol_name" if name == "script_key" else "value")):
                missing.append(name)
    elif kind == "adjust_progression":
        for name in ("experience_delta", "learning_points_delta"):
            if not non_empty(field(obj, name, "xp_delta" if name == "experience_delta" else "lp_delta")):
                missing.append(name)
    elif kind == "apply_experience_reward":
        if not non_empty(field(obj, "experience_delta", "xp_delta", "delta")):
            missing.append("experience_delta")
    elif kind == "update_quest":
        for name in ("quest_key", "status"):
            if not non_empty(field(obj, name, "topic" if name == "quest_key" else name)):
                missing.append(name)
    elif kind == "set_known_dialog":
        for name in ("npc_key", "info_key"):
            if not non_empty(field(obj, name, "npc_symbol_name" if name == "npc_key" else "info_symbol_name")):
                missing.append(name)
    return missing


def main() -> int:
    ap = argparse.ArgumentParser(description="Check Step37 script/progression semantic action JSONL emitted by OpenGothic C++ hooks.")
    ap.add_argument("--jsonl", required=True, help="OpenGothic -mmo-action-jsonl output")
    ap.add_argument("--session-key", default="", help="optional idempotency key prefix")
    ap.add_argument("--require-script-int", action="store_true", help="require at least one set_script_int action")
    ap.add_argument("--require-xp", action="store_true", help="require adjust_progression/apply_experience_reward with non-zero XP delta")
    ap.add_argument("--output", default="", help="optional JSON artifact path")
    args = ap.parse_args()

    rows, errors = read_jsonl(Path(args.jsonl), args.session_key or None)
    step_rows = [r for r in rows if str(r.get("action_kind") or "") in STEP37_KINDS]
    counts = Counter(str(r.get("action_kind") or "") for r in step_rows)
    idems = [str(r.get("idempotency_key") or "") for r in step_rows]
    duplicate_idems = sorted(k for k, v in Counter(idems).items() if k and v > 1)

    shape_errors: list[dict[str, Any]] = []
    for r in step_rows:
        missing = validate_row(r)
        if missing:
            shape_errors.append({
                "action_kind": r.get("action_kind"),
                "idempotency_key": r.get("idempotency_key"),
                "missing": missing,
            })

    xp_rows = []
    for r in step_rows:
        if str(r.get("action_kind") or "") not in {"adjust_progression", "apply_experience_reward"}:
            continue
        try:
            delta = int(field(r, "experience_delta", "xp_delta", "delta") or 0)
        except (TypeError, ValueError):
            delta = 0
        if delta != 0:
            xp_rows.append(r)

    checks = [
        ("json parsed", not errors, f"errors={len(errors)}"),
        ("step37 rows present", len(step_rows) > 0, f"rows={len(step_rows)}"),
        ("idempotency unique", not duplicate_idems, f"duplicates={len(duplicate_idems)}"),
        ("shape valid", not shape_errors, f"shape_errors={len(shape_errors)}"),
    ]
    if args.require_script_int:
        checks.append(("script int present", counts["set_script_int"] > 0, f"set_script_int={counts['set_script_int']}"))
    if args.require_xp:
        checks.append(("xp/progression present", len(xp_rows) > 0, f"xp_rows={len(xp_rows)}"))

    status = "passed" if all(ok for _, ok, _ in checks) else "failed"
    artifact = {
        "status": status,
        "jsonl": str(Path(args.jsonl)),
        "session_key": args.session_key,
        "rows_total_after_session_filter": len(rows),
        "step37_rows": len(step_rows),
        "counts": dict(sorted(counts.items())),
        "duplicate_idempotency_keys": duplicate_idems,
        "parse_errors": errors[:50],
        "shape_errors": shape_errors[:50],
        "xp_rows": len(xp_rows),
        "sample_step37": step_rows[:5],
        "interpretation": "Client JSONL evidence only. This proves C++ hooks emitted Step37 semantic actions; it does not prove server dispatch, MySQL projection, or restore parity.",
    }

    for name, ok, detail in checks:
        print(f"{'OK' if ok else 'FAIL'}: {name}: {detail}")
    print(f"status={status}")

    if args.output:
        out = Path(args.output)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(artifact, ensure_ascii=False, indent=2, sort_keys=True), encoding="utf-8")
        print(f"artifact={out}")

    return 0 if status == "passed" else 2


if __name__ == "__main__":
    raise SystemExit(main())
