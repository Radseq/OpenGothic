#!/usr/bin/env python3
"""Check Step45 live world-AI, weapon-state and corpse-loot coverage.

This checker extends Step45 with weapon ready/holster, corpse loot, and NPC-vs-NPC combat/death coverage. It
summarizes which real gameplay domains reached the server boundary and, when a
MySQL URL is provided, what happened to matching outbox rows after the resolved
worker ran.

It does not pretend that all domains are production-authoritative yet. In
particular, drop_character_item, loot_npc_inventory and weapon-state events are capture-only in Step45 until canonical
MySQL drop/spawn procedure exists.
"""
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Any
from urllib.parse import unquote, urlparse

DOMAIN_KINDS: dict[str, set[str]] = {
    "dialog": {"set_known_dialog", "dialog_choice_executed", "dialog_choice_updated"},
    "quest": {"update_quest"},
    "script_progression": {"set_script_int", "adjust_progression", "apply_experience_reward"},
    "inventory_pickup": {"pickup_world_item"},
    "inventory_transfer": {"transfer_character_item", "take_container_item", "put_container_item"},
    "equipment": {"equip_character_item", "unequip_character_item"},
    "weapon_state": {"ready_weapon", "holster_weapon"},
    "drop": {"drop_character_item"},
    "corpse_loot": {"loot_npc_inventory"},
    "trade": {"trade_buy_from_npc", "trade_sell_to_npc"},
    "resource": {"consume_mana", "consume_item", "character_resource_delta"},
    "world_clock": {"world_time_changed"},
    "combat_damage": {"apply_character_damage", "apply_world_entity_damage"},
    "kill": {"mark_npc_dead"},
    "movement": {"movement_proposal", "character_checkpoint"},
}

DEFAULT_REQUIRED = ("dialog", "quest", "script_progression", "inventory_pickup", "equipment", "movement")

EXPECTED_DB_EVENT_BY_KIND = {
    "set_known_dialog": "character_dialog_known_set",
    "update_quest": "character_quest_updated",
    "set_script_int": "character_script_int_set",
    "adjust_progression": "character_progression_adjusted",
    "apply_experience_reward": "character_progression_adjusted",
    "pickup_world_item": "world_item_picked_up",
    "equip_character_item": "character_item_equipped",
    "unequip_character_item": "character_item_unequipped",
    "trade_buy_from_npc": "trade_buy_from_npc",
    "trade_sell_to_npc": "trade_sell_to_npc",
    "consume_mana": "character_mana_consumed",
    "consume_item": "character_item_consumed",
    "apply_character_damage": "character_damage_applied",
    "apply_world_entity_damage": "world_entity_damage_applied",
    "mark_npc_dead": "npc_marked_dead",
    "character_checkpoint": "character_position_checkpoint",
}

CAPTURE_ONLY_KINDS = {"drop_character_item", "loot_npc_inventory", "ready_weapon", "holster_weapon", "character_resource_delta", "world_time_changed"}

@dataclass(frozen=True)
class Target:
    host: str
    port: int
    user: str
    password: str
    database: str


def parse_mysql_url(url: str) -> Target:
    p = urlparse(url)
    if p.scheme not in {"mysql", "mysql+pymysql"}:
        raise SystemExit(f"unsupported mysql url scheme: {p.scheme!r}")
    if not p.hostname or not p.username or not p.path.strip("/"):
        raise SystemExit("mysql url must include user, host and database")
    return Target(p.hostname, p.port or 3306, unquote(p.username), unquote(p.password or ""), p.path.strip("/"))


def sql_literal(value: Any) -> str:
    if value is None:
        return "NULL"
    if isinstance(value, bool):
        return "TRUE" if value else "FALSE"
    if isinstance(value, int):
        return str(value)
    text = str(value)
    return "'" + text.replace("\\", "\\\\").replace("'", "''") + "'"


def mysql_cmd(target: Target) -> list[str]:
    exe = shutil.which("mysql")
    if exe is None:
        raise SystemExit("mysql executable was not found in PATH")
    cmd = [exe, "--default-character-set=utf8mb4", "--init-command=SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci", "--batch", "--raw", "--skip-column-names", "-h", target.host, "-P", str(target.port), "-u", target.user]
    if target.password:
        cmd.append(f"-p{target.password}")
    cmd.append(target.database)
    return cmd


def run_mysql(target: Target, sql: str) -> str:
    proc = subprocess.run(mysql_cmd(target), input=sql, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if proc.returncode != 0:
        if proc.stderr:
            print(proc.stderr, file=sys.stderr, end="")
        raise SystemExit(f"mysql exited with status {proc.returncode}")
    return proc.stdout.strip()


def read_jsonl(path: Path | None) -> tuple[list[dict[str, Any]], int]:
    if path is None or not path.exists():
        return [], 0
    rows: list[dict[str, Any]] = []
    bad = 0
    with path.open("r", encoding="utf-8", errors="replace") as f:
        for line_no, line in enumerate(f, 1):
            text = line.strip()
            if not text:
                continue
            try:
                obj = json.loads(text)
            except json.JSONDecodeError:
                bad += 1
                continue
            if isinstance(obj, dict):
                rows.append(obj)
            else:
                bad += 1
    return rows, bad


def action_kind(row: dict[str, Any]) -> str:
    return str(row.get("action_kind") or row.get("kind") or "")


def filter_session(rows: list[dict[str, Any]], session_key: str | None) -> list[dict[str, Any]]:
    if not session_key:
        return rows
    prefix = session_key + ":"
    out: list[dict[str, Any]] = []
    for row in rows:
        idem = str(row.get("idempotency_key") or row.get("client_idempotency_key") or "")
        if idem.startswith(prefix):
            out.append(row)
    return out


def is_player_key(value: Any) -> bool:
    text = str(value or "")
    return text.startswith("character:PC_HERO") or text == "PC_HERO"


def payload_of(row: dict[str, Any]) -> dict[str, Any]:
    payload = row.get("payload")
    return payload if isinstance(payload, dict) else {}


def classify_world_ai(rows: list[dict[str, Any]]) -> dict[str, int]:
    counts = Counter()
    for row in rows:
        kind = action_kind(row)
        payload = payload_of(row)
        source_actor = payload.get("source_actor_key") or payload.get("actor_key")
        target_key = payload.get("target_key") or payload.get("target_npc_entity_key") or row.get("target_key")
        if kind == "apply_world_entity_damage":
            if source_actor and not is_player_key(source_actor):
                counts["npc_combat_damage"] += 1
            else:
                counts["player_or_unknown_world_damage"] += 1
        elif kind == "mark_npc_dead":
            if source_actor and not is_player_key(source_actor):
                counts["npc_kill"] += 1
            else:
                counts["player_or_unknown_kill"] += 1
        elif kind in {"ready_weapon", "holster_weapon"}:
            if not is_player_key(payload.get("actor_key")):
                counts["npc_weapon_state"] += 1
            else:
                counts["player_weapon_state"] += 1
        elif kind == "loot_npc_inventory":
            counts["corpse_loot"] += 1
        elif kind == "consume_item":
            counts["player_consumed_item"] += 1
        elif kind == "character_resource_delta":
            counts["player_resource_delta"] += 1
        elif kind == "world_time_changed":
            counts["world_time_changed"] += 1
        if target_key and not is_player_key(target_key):
            counts["world_entity_target_events"] += 1
    return dict(sorted(counts.items()))


def invalid_packet_samples(rejected: list[dict[str, Any]], limit: int = 5) -> list[dict[str, Any]]:
    samples = []
    for row in rejected:
        if str(row.get("error") or "") != "decode_or_json":
            continue
        samples.append({
            "message": row.get("message"),
            "remote": row.get("remote"),
            "bytes": row.get("bytes"),
            "raw_hex_prefix": row.get("raw_hex_prefix"),
            "raw_utf8_replace_preview": row.get("raw_utf8_replace_preview"),
            "raw_latin1_preview": row.get("raw_latin1_preview"),
            "raw_file": row.get("raw_file"),
        })
        if len(samples) >= limit:
            break
    return samples


def domain_summary(kind_counts: Counter[str]) -> dict[str, Any]:
    out: dict[str, Any] = {}
    for domain, kinds in DOMAIN_KINDS.items():
        present = {kind: kind_counts[kind] for kind in sorted(kinds) if kind_counts[kind] > 0}
        out[domain] = {"present": bool(present), "kinds": present, "total": sum(present.values())}
    return out


def mysql_outbox_summary(target: Target, session_key: str) -> dict[str, Any]:
    like = session_key + ":%"
    out = run_mysql(target, f"""
        SELECT action_kind, status, COUNT(*)
          FROM mmo_server_action_outbox
         WHERE idempotency_key LIKE {sql_literal(like)}
         GROUP BY action_kind, status
         ORDER BY action_kind, status;
    """)
    by_kind: dict[str, dict[str, int]] = defaultdict(dict)
    for line in out.splitlines():
        if not line.strip():
            continue
        kind, status, count = line.split("\t")[:3]
        by_kind[kind][status] = int(count)
    failures = run_mysql(target, f"""
        SELECT action_kind, status, COALESCE(last_error_code,''), LEFT(COALESCE(last_error_message,''),240), COUNT(*)
          FROM mmo_server_action_outbox
         WHERE idempotency_key LIKE {sql_literal(like)}
           AND status IN ('failed','dead_letter','claimed')
         GROUP BY action_kind, status, COALESCE(last_error_code,''), LEFT(COALESCE(last_error_message,''),240)
         ORDER BY action_kind, status, COUNT(*) DESC;
    """)
    failure_rows = []
    for line in failures.splitlines():
        if not line.strip():
            continue
        parts = line.split("\t")
        while len(parts) < 5:
            parts.append("")
        failure_rows.append({"action_kind": parts[0], "status": parts[1], "code": parts[2], "message": parts[3], "count": int(parts[4])})
    journal = run_mysql(target, f"""
        SELECT event_type, COUNT(*)
          FROM world_event_journal
         WHERE idempotency_key LIKE {sql_literal(like)}
         GROUP BY event_type
         ORDER BY event_type;
    """)
    journal_counts = {}
    for line in journal.splitlines():
        if not line.strip():
            continue
        kind, count = line.split("\t")[:2]
        journal_counts[kind] = int(count)
    return {"outbox_by_kind_status": dict(by_kind), "failure_rows": failure_rows, "journal_event_counts": journal_counts}


def detect_db_gaps(kind_counts: Counter[str], mysql_summary: dict[str, Any] | None) -> list[dict[str, Any]]:
    gaps: list[dict[str, Any]] = []
    if mysql_summary is None:
        return gaps
    outbox = mysql_summary.get("outbox_by_kind_status", {}) if isinstance(mysql_summary, dict) else {}
    journal = mysql_summary.get("journal_event_counts", {}) if isinstance(mysql_summary, dict) else {}
    for kind, count in sorted(kind_counts.items()):
        if count <= 0:
            continue
        if kind in CAPTURE_ONLY_KINDS:
            gaps.append({"action_kind": kind, "severity": "info", "reason": "capture_only_no_mysql_procedure_yet"})
            continue
        statuses = outbox.get(kind, {}) if isinstance(outbox, dict) else {}
        if statuses and statuses.get("failed", 0):
            gaps.append({"action_kind": kind, "severity": "error", "reason": "outbox_failed", "statuses": statuses})
        expected_event = EXPECTED_DB_EVENT_BY_KIND.get(kind)
        if expected_event and statuses.get("applied", 0) and journal.get(expected_event, 0) <= 0 and kind != "character_checkpoint":
            gaps.append({"action_kind": kind, "severity": "warning", "reason": "applied_without_expected_journal_event", "expected_event": expected_event})
    return gaps


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description="Check Step45 live world-AI/weapon/loot gameplay coverage from server JSONL/outbox artifacts.")
    ap.add_argument("--accepted-jsonl", required=True)
    ap.add_argument("--checkpoint-jsonl")
    ap.add_argument("--rejected-jsonl")
    ap.add_argument("--summary-json")
    ap.add_argument("--url", help="optional mysql:// URL for outbox/journal status")
    ap.add_argument("--session-key")
    ap.add_argument("--output", required=True)
    ap.add_argument("--require-domain", action="append", default=[], choices=sorted(DOMAIN_KINDS), help="domain that must be present; can be repeated")
    ap.add_argument("--require-default-domains", action="store_true", help="require dialog/quest/script_progression/inventory_pickup/equipment/movement")
    ap.add_argument("--require-world-ai-domains", action="store_true", help="require weapon_state, corpse_loot, combat_damage and kill domains")
    ap.add_argument("--fail-on-invalid-packets", action="store_true", help="fail if rejected JSONL contains undecodable/raw invalid UDP packet diagnostics")
    ap.add_argument("--fail-on-mysql-errors", action="store_true", help="fail if matching outbox has failed/dead-letter rows")
    args = ap.parse_args(argv)

    accepted, accepted_bad = read_jsonl(Path(args.accepted_jsonl))
    checkpoints, checkpoint_bad = read_jsonl(Path(args.checkpoint_jsonl) if args.checkpoint_jsonl else None)
    rejected, rejected_bad = read_jsonl(Path(args.rejected_jsonl) if args.rejected_jsonl else None)
    rejected_all = list(rejected)
    accepted = filter_session(accepted, args.session_key)
    checkpoints = filter_session(checkpoints, args.session_key)
    rejected = filter_session(rejected, args.session_key)

    kind_counts = Counter(action_kind(row) for row in accepted if action_kind(row))
    kind_counts.update(action_kind(row) for row in checkpoints if action_kind(row))
    domains = domain_summary(kind_counts)

    mysql_summary = None
    if args.url:
        if not args.session_key:
            raise SystemExit("--url requires --session-key")
        mysql_summary = mysql_outbox_summary(parse_mysql_url(args.url), args.session_key)

    required = list(args.require_domain)
    if args.require_default_domains:
        required.extend(DEFAULT_REQUIRED)
    if args.require_world_ai_domains:
        required.extend(("weapon_state", "corpse_loot", "combat_damage", "kill"))
    missing_domains = sorted({d for d in required if not domains.get(d, {}).get("present")})
    db_gaps = detect_db_gaps(kind_counts, mysql_summary)
    mysql_errors = [g for g in db_gaps if g.get("severity") == "error"]
    world_ai = classify_world_ai(accepted + checkpoints)
    invalid_samples = invalid_packet_samples(rejected_all)

    status = "passed"
    if accepted_bad or checkpoint_bad or rejected_bad:
        status = "failed"
    if missing_domains:
        status = "failed"
    if args.fail_on_invalid_packets and invalid_samples:
        status = "failed"
    if args.fail_on_mysql_errors and mysql_errors:
        status = "failed"

    manifest = {
        "status": status,
        "session_key": args.session_key,
        "accepted_rows": len(accepted),
        "checkpoint_rows": len(checkpoints),
        "rejected_rows": len(rejected),
        "bad_rows": {"accepted": accepted_bad, "checkpoint": checkpoint_bad, "rejected": rejected_bad},
        "kind_counts": dict(sorted(kind_counts.items())),
        "domains": domains,
        "world_ai_classification": world_ai,
        "invalid_packet_samples": invalid_samples,
        "required_domains": sorted(set(required)),
        "missing_domains": missing_domains,
        "mysql": mysql_summary,
        "db_gaps": db_gaps,
        "capture_only_kinds": sorted(CAPTURE_ONLY_KINDS),
    }
    if args.summary_json and Path(args.summary_json).exists():
        try:
            manifest["server_summary"] = json.loads(Path(args.summary_json).read_text(encoding="utf-8"))
        except Exception as exc:
            manifest["server_summary_error"] = str(exc)

    out = Path(args.output)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    print(f"OK: Step45 domains={ {k:v['total'] for k,v in domains.items() if v['present']} }")
    if world_ai:
        print(f"world_ai={world_ai}")
    if invalid_samples:
        print("Invalid packet samples:")
        for sample in invalid_samples:
            print("  " + json.dumps(sample, ensure_ascii=False, sort_keys=True))
    if missing_domains:
        print("MISSING domains: " + ", ".join(missing_domains), file=sys.stderr)
    if db_gaps:
        print("DB gaps:")
        for gap in db_gaps[:20]:
            print("  " + json.dumps(gap, ensure_ascii=False, sort_keys=True))
    print(f"artifact={out}")
    print(f"status={status}")
    return 0 if status == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))


