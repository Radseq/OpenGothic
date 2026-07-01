#!/usr/bin/env python3
"""Validate Step38 trade/combat/resource/lifecycle semantic actions in OpenGothic JSONL."""
from __future__ import annotations

import argparse
import json
import sys
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any

STEP38_KINDS = {
    "trade_buy_from_npc",
    "trade_sell_to_npc",
    "consume_mana",
    "consume_item",
    "apply_character_damage",
    "apply_world_entity_damage",
    "mark_npc_dead",
}
TRADE_KINDS = {"trade_buy_from_npc", "trade_sell_to_npc"}
COMBAT_KINDS = {"apply_character_damage", "apply_world_entity_damage", "mark_npc_dead"}
RESOURCE_KINDS = {"consume_mana", "consume_item"}

REQUIRED: dict[str, tuple[str, ...]] = {
    "trade_buy_from_npc": ("npc_key", "item_symbol", "amount", "currency_key"),
    "trade_sell_to_npc": ("npc_key", "item_symbol", "amount", "currency_key"),
    "consume_mana": ("mana_amount",),
    "consume_item": ("item_symbol", "amount"),
    "apply_character_damage": ("target_character_key", "damage_amount"),
    "apply_world_entity_damage": ("target_key", "damage_amount"),
    "mark_npc_dead": ("target_key", "dead"),
}


def payload(row: dict[str, Any]) -> dict[str, Any]:
    p = row.get("payload")
    return p if isinstance(p, dict) else {}


def value(row: dict[str, Any], key: str) -> Any:
    p = payload(row)
    if key in p:
        return p[key]
    return row.get(key)


def present(v: Any) -> bool:
    return v is not None and v != ""


def validate_shape(row: dict[str, Any]) -> list[str]:
    kind = str(row.get("action_kind") or "")
    errs: list[str] = []
    if kind not in STEP38_KINDS:
        return errs
    if not present(row.get("idempotency_key")):
        errs.append("missing idempotency_key")
    if not present(row.get("local_sequence")):
        errs.append("missing local_sequence")
    if not present(row.get("client_tick")):
        errs.append("missing client_tick")
    if not isinstance(row.get("payload"), dict):
        errs.append("payload is not an object")
    for key in REQUIRED.get(kind, ()):
        if not present(value(row, key)):
            errs.append(f"missing {key}")
    if kind in TRADE_KINDS:
        amount = int(value(row, "amount") or 0)
        if amount <= 0:
            errs.append("amount must be positive")
    if kind in {"consume_mana", "consume_item", "apply_character_damage", "apply_world_entity_damage"}:
        nkey = "mana_amount" if kind == "consume_mana" else "damage_amount" if "damage" in kind else "amount"
        try:
            if int(value(row, nkey) or 0) <= 0:
                errs.append(f"{nkey} must be positive")
        except (TypeError, ValueError):
            errs.append(f"{nkey} must be integer")
    return errs


def load_rows(path: Path, session_key: str | None) -> tuple[list[dict[str, Any]], list[str]]:
    rows: list[dict[str, Any]] = []
    errors: list[str] = []
    with path.open("r", encoding="utf-8", errors="replace") as f:
        for line_no, line in enumerate(f, 1):
            text = line.strip()
            if not text:
                continue
            try:
                obj = json.loads(text)
            except json.JSONDecodeError as exc:
                errors.append(f"{path}:{line_no}: invalid JSON: {exc}")
                continue
            if not isinstance(obj, dict):
                errors.append(f"{path}:{line_no}: expected JSON object")
                continue
            if session_key and not str(obj.get("idempotency_key") or "").startswith(session_key + ":"):
                continue
            rows.append(obj)
    return rows, errors


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description="Validate Step38 trade/combat/resource semantic JSONL")
    ap.add_argument("--jsonl", required=True, type=Path)
    ap.add_argument("--session-key", default=None)
    ap.add_argument("--require-trade", action="store_true")
    ap.add_argument("--require-combat", action="store_true")
    ap.add_argument("--require-resource", action="store_true")
    ap.add_argument("--require-kind", action="append", default=[])
    ap.add_argument("--output", type=Path, default=None)
    args = ap.parse_args(argv)

    rows, parse_errors = load_rows(args.jsonl, args.session_key)
    step38 = [r for r in rows if str(r.get("action_kind") or "") in STEP38_KINDS]
    counts = Counter(str(r.get("action_kind") or "") for r in step38)
    idem_counts = Counter(str(r.get("idempotency_key") or "") for r in step38)
    duplicates = {k: v for k, v in idem_counts.items() if k and v > 1}
    shape_errors: dict[str, list[str]] = {}
    for idx, row in enumerate(step38):
        errs = validate_shape(row)
        if errs:
            shape_errors[str(idx)] = errs

    checks: list[dict[str, Any]] = []
    def add(name: str, ok: bool, detail: Any) -> None:
        checks.append({"name": name, "ok": ok, "detail": detail})
        print(("OK" if ok else "ERROR") + f": {name}: {detail}")

    add("json parsed", not parse_errors, {"errors": len(parse_errors)})
    add("step38 rows present", bool(step38), {"rows": len(step38)})
    add("idempotency unique", not duplicates, {"duplicates": duplicates})
    add("shape valid", not shape_errors, {"shape_errors": shape_errors})
    if args.require_trade:
        add("trade present", sum(counts[k] for k in TRADE_KINDS) > 0, dict(counts))
    if args.require_combat:
        add("combat present", sum(counts[k] for k in COMBAT_KINDS) > 0, dict(counts))
    if args.require_resource:
        add("resource present", sum(counts[k] for k in RESOURCE_KINDS) > 0, dict(counts))
    for kind in args.require_kind:
        add(f"kind present: {kind}", counts[kind] > 0, counts[kind])

    ok = all(c["ok"] for c in checks)
    result = {
        "tool": "check_mmo_step38_trade_combat_jsonl.py",
        "status": "passed" if ok else "failed",
        "jsonl": str(args.jsonl),
        "session_key": args.session_key,
        "rows_total": len(rows),
        "rows_step38": len(step38),
        "kind_counts": dict(sorted(counts.items())),
        "parse_errors": parse_errors[:50],
        "shape_errors": shape_errors,
        "checks": checks,
    }
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True), encoding="utf-8")
        print(f"artifact={args.output}")
    print("status=" + result["status"])
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
