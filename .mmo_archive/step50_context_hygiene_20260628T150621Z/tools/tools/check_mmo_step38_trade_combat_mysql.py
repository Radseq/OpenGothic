#!/usr/bin/env python3
"""Check Step38 trade/combat/resource dispatch evidence in MySQL/outbox/journal."""
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Any
from urllib.parse import unquote, urlparse

STEP38_KINDS = {
    "trade_buy_from_npc",
    "trade_sell_to_npc",
    "consume_mana",
    "consume_item",
    "apply_character_damage",
    "apply_world_entity_damage",
    "mark_npc_dead",
}
EVENT_TYPES = {
    "trade_buy_from_npc",
    "trade_sell_to_npc",
    "character_mana_consumed",
    "character_item_consumed",
    "character_damage_applied",
    "world_entity_damage_applied",
    "npc_marked_dead",
}

KIND_EVENT_TYPES = {
    "trade_buy_from_npc": "trade_buy_from_npc",
    "trade_sell_to_npc": "trade_sell_to_npc",
    "consume_mana": "character_mana_consumed",
    "consume_item": "character_item_consumed",
    "apply_character_damage": "character_damage_applied",
    "apply_world_entity_damage": "world_entity_damage_applied",
    "mark_npc_dead": "npc_marked_dead",
}

@dataclass(frozen=True)
class Target:
    host: str
    port: int
    user: str
    password: str
    database: str


def parse_mysql_url(url: str) -> Target:
    p = urlparse(url)
    return Target(p.hostname or "localhost", p.port or 3306, unquote(p.username or ""), unquote(p.password or ""), (p.path or "/").lstrip("/"))


def mysql_cmd(target: Target) -> list[str]:
    exe = shutil.which("mysql")
    if exe is None:
        raise RuntimeError("mysql executable was not found in PATH")
    cmd = [exe, "--default-character-set=utf8mb4", "--batch", "--raw", "--skip-column-names", "-h", target.host, "-P", str(target.port), "-u", target.user]
    if target.password:
        cmd.append(f"-p{target.password}")
    cmd.append(target.database)
    return cmd


def run_mysql(target: Target, sql: str) -> str:
    proc = subprocess.run(mysql_cmd(target), input=sql, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if proc.returncode != 0:
        if proc.stderr:
            print(proc.stderr, file=sys.stderr, end="")
        raise RuntimeError(f"mysql exited with status {proc.returncode}")
    return proc.stdout.strip()


def q(v: Any) -> str:
    if v is None:
        return "NULL"
    if isinstance(v, bool):
        return "TRUE" if v else "FALSE"
    if isinstance(v, int):
        return str(v)
    return "'" + str(v).replace("\\", "\\\\").replace("'", "''") + "'"


def rows(out: str) -> list[list[str]]:
    return [ln.split("\t") for ln in out.splitlines() if ln.strip()]


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description="Check Step38 MySQL outbox/journal evidence")
    ap.add_argument("--url", required=True)
    ap.add_argument("--session-key", required=True)
    ap.add_argument("--require-kind", action="append", default=[])
    ap.add_argument("--require-no-failed", action="store_true")
    ap.add_argument("--output", type=Path, default=None)
    args = ap.parse_args(argv)

    target = parse_mysql_url(args.url)
    like = args.session_key + ":%"

    outbox = rows(run_mysql(target, f"""
        SELECT action_kind,status,COUNT(*)
          FROM mmo_server_action_outbox
         WHERE idempotency_key LIKE {q(like)}
           AND action_kind IN ({','.join(q(k) for k in sorted(STEP38_KINDS))})
         GROUP BY action_kind,status
         ORDER BY action_kind,status;
    """))
    outbox_counts: dict[str, dict[str, int]] = {}
    for kind, status, count in outbox:
        outbox_counts.setdefault(kind, {})[status] = int(count)

    journal = rows(run_mysql(target, f"""
        SELECT event_type,COUNT(*)
          FROM world_event_journal
         WHERE idempotency_key LIKE {q(like)}
           AND event_type IN ({','.join(q(k) for k in sorted(EVENT_TYPES))})
         GROUP BY event_type
         ORDER BY event_type;
    """))
    journal_counts = {event_type: int(count) for event_type, count in journal}

    failed_rows = rows(run_mysql(target, f"""
        SELECT BIN_TO_UUID(action_id,1), action_kind, status, COALESCE(last_error_code,''), COALESCE(last_error_message,'')
          FROM mmo_server_action_outbox
         WHERE idempotency_key LIKE {q(like)}
           AND action_kind IN ({','.join(q(k) for k in sorted(STEP38_KINDS))})
           AND status IN ('failed','dead_letter')
         ORDER BY updated_at DESC
         LIMIT 20;
    """))

    idempotency_dupes = rows(run_mysql(target, f"""
        SELECT idempotency_key, COUNT(*)
          FROM mmo_server_action_outbox
         WHERE idempotency_key LIKE {q(like)}
           AND action_kind IN ({','.join(q(k) for k in sorted(STEP38_KINDS))})
         GROUP BY idempotency_key
        HAVING COUNT(*) > 1
         LIMIT 20;
    """))

    checks: list[dict[str, Any]] = []
    def add(name: str, ok: bool, detail: Any) -> None:
        checks.append({"name": name, "ok": ok, "detail": detail})
        print(("OK" if ok else "ERROR") + f": {name}: {detail}")

    total_outbox = sum(sum(v.values()) for v in outbox_counts.values())
    add("step38 outbox present", total_outbox > 0, {"rows": total_outbox, "counts": outbox_counts})
    add("outbox idempotency unique", not idempotency_dupes, {"duplicates": idempotency_dupes})
    if args.require_no_failed:
        add("no failed Step38 rows", not failed_rows, {"failed": failed_rows})
    for kind in args.require_kind:
        applied = outbox_counts.get(kind, {}).get("applied", 0)
        add(f"kind applied: {kind}", applied > 0, {"applied": applied, "statuses": outbox_counts.get(kind, {})})
        event_type = KIND_EVENT_TYPES.get(kind)
        if event_type:
            add(f"journal event for kind: {kind}", journal_counts.get(event_type, 0) > 0, {"event_type": event_type, "count": journal_counts.get(event_type, 0)})
    add("journal Step38 event present", bool(journal_counts), journal_counts)

    ok = all(c["ok"] for c in checks)
    result = {
        "tool": "check_mmo_step38_trade_combat_mysql.py",
        "status": "passed" if ok else "failed",
        "session_key": args.session_key,
        "outbox_counts": outbox_counts,
        "journal_counts": journal_counts,
        "failed_rows": failed_rows,
        "idempotency_duplicates": idempotency_dupes,
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


