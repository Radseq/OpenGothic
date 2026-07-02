#!/usr/bin/env python3
"""Check Step68 drop/loot inventory bridge procedures."""
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
import uuid
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from urllib.parse import unquote, urlparse

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

REQUIRED_TABLES = [
    "server_sessions",
    "characters",
    "item_instances",
    "character_inventory",
    "world_inventory",
    "world_entity_state",
    "world_event_journal",
    "world_item_audit",
]

REQUIRED_ROUTINES = [
    "mmo_drop_character_item",
    "mmo_loot_npc_inventory",
]


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
        raise ValueError("expected mysql:// URL")
    return Target(
        host=p.hostname or "127.0.0.1",
        port=p.port or 3306,
        user=unquote(p.username or ""),
        password=unquote(p.password or ""),
        database=(p.path or "/").lstrip("/"),
    )


def mysql_cmd(target: Target) -> list[str]:
    exe = shutil.which("mysql")
    if exe is None:
        raise RuntimeError("mysql executable was not found in PATH")
    cmd = [
        exe,
        "--default-character-set=utf8mb4",
        "--init-command=SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci",
        "--batch",
        "--raw",
        "--skip-column-names",
        "-h",
        target.host,
        "-P",
        str(target.port),
        "-u",
        target.user,
    ]
    if target.password:
        cmd.append(f"-p{target.password}")
    cmd.append(target.database)
    return cmd


def sql_literal(value: object) -> str:
    if value is None:
        return "NULL"
    if isinstance(value, bool):
        return "TRUE" if value else "FALSE"
    if isinstance(value, int):
        return str(value)
    text = str(value)
    return "'" + text.replace("\\", "\\\\").replace("'", "''") + "'"


class SmokeSqlError(RuntimeError):
    def __init__(self, sql: str, returncode: int, stdout: str, stderr: str) -> None:
        super().__init__(f"mysql exited with status {returncode}")
        self.sql = sql
        self.returncode = returncode
        self.stdout = stdout
        self.stderr = stderr


def run_mysql(target: Target, sql: str) -> str:
    proc = subprocess.run(mysql_cmd(target), input=sql, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    if proc.returncode != 0:
        if proc.stderr:
            print(proc.stderr, file=sys.stderr, end="")
        raise SmokeSqlError(sql, proc.returncode, proc.stdout, proc.stderr)
    return proc.stdout.strip()


def last_row(raw: str) -> list[str]:
    lines = [line for line in raw.splitlines() if line.strip()]
    return [] if not lines else lines[-1].split("\t")


def check_objects(target: Target) -> dict[str, object]:
    table_rows = run_mysql(
        target,
        """
        SELECT table_name
          FROM information_schema.tables
         WHERE table_schema = DATABASE()
           AND table_type = 'BASE TABLE'
           AND table_name IN ('server_sessions','characters','item_instances','character_inventory','world_inventory','world_entity_state','world_event_journal','world_item_audit')
         ORDER BY table_name;
        """,
    )
    routine_rows = run_mysql(
        target,
        """
        SELECT routine_name
          FROM information_schema.ROUTINES
         WHERE routine_schema = DATABASE()
           AND routine_name IN ('mmo_drop_character_item','mmo_loot_npc_inventory')
         ORDER BY routine_name;
        """,
    )
    tables = {line.strip() for line in table_rows.splitlines() if line.strip()}
    routines = {line.strip() for line in routine_rows.splitlines() if line.strip()}
    return {
        "present_tables": sorted(tables),
        "missing_tables": [name for name in REQUIRED_TABLES if name not in tables],
        "present_routines": sorted(routines),
        "missing_routines": [name for name in REQUIRED_ROUTINES if name not in routines],
    }


def smoke_phase(result: dict[str, object], name: str, target: Target, sql: str) -> str | None:
    phases = result.setdefault("phases", [])
    phase: dict[str, object] = {"name": name}
    try:
        raw = run_mysql(target, sql)
    except SmokeSqlError as exc:
        phase.update(
            {
                "ok": False,
                "error": str(exc),
                "returncode": exc.returncode,
                "stderr_tail": exc.stderr[-4000:],
                "stdout_tail": exc.stdout[-4000:],
                "sql_head": "\n".join(line.rstrip() for line in exc.sql.strip().splitlines()[:24]),
            }
        )
        phases.append(phase)
        result["ok"] = False
        return None
    phase.update({"ok": True, "stdout_tail": raw[-4000:]})
    phases.append(phase)
    return raw


def run_smoke(target: Target, account_name: str, character_key: str) -> dict[str, object]:
    suffix = uuid.uuid4().hex
    session_key = f"step68-smoke:{suffix}"
    drop_idem = f"step68:drop:{suffix}"
    loot_idem = f"step68:loot:{suffix}"
    drop_entity_key = f"smoke:step68:drop:{suffix[:24]}"
    npc_entity_key = f"smoke:step68:npc:{suffix[:24]}"
    drop_bag_index = 680000
    loot_bag_index = 680001
    result: dict[str, object] = {
        "session_key": session_key,
        "drop_entity_key": drop_entity_key,
        "npc_entity_key": npc_entity_key,
        "ok": False,
    }

    login_raw = smoke_phase(
        result,
        "login",
        target,
        f"""
    SET @session_id=NULL;
    CALL mmo_login_character({sql_literal(account_name)}, {sql_literal(character_key)}, {sql_literal(session_key)}, 'step68-smoke', 'local', JSON_OBJECT('tool','check_mmo_step68'), @session_id);
    SELECT BIN_TO_UUID(@session_id,1);
    """,
    )
    if login_raw is None:
        return result
    session_uuid = last_row(login_raw)[0]
    result["session_uuid"] = session_uuid

    fixture_raw = smoke_phase(
        result,
        "fixture",
        target,
        f"""
    SET @realm_id=(SELECT realm_id FROM characters WHERE character_key={sql_literal(character_key)} LIMIT 1);
    SET @world_id=(SELECT current_world_instance_id FROM characters WHERE character_key={sql_literal(character_key)} LIMIT 1);
    SET @character_id=(SELECT character_id FROM characters WHERE character_key={sql_literal(character_key)} LIMIT 1);
    SET @content_revision_id=(SELECT active_content_revision_id FROM realm_realms WHERE realm_id=@realm_id LIMIT 1);
    SET @item_template_id=(SELECT item_template_id FROM content_item_templates WHERE content_revision_id=@content_revision_id ORDER BY symbol_index ASC, item_template_key ASC LIMIT 1);
    SET @drop_item_id=UUID_TO_BIN(UUID(),1);
    SET @loot_item_id=UUID_TO_BIN(UUID(),1);
    DELETE FROM character_inventory WHERE character_id=@character_id AND bag_index IN ({drop_bag_index},{loot_bag_index});
    INSERT INTO item_instances(item_instance_id, realm_id, item_template_id, item_instance_key, owner_type, owner_id, quantity, lifecycle_state, raw_payload)
    VALUES(@drop_item_id, @realm_id, @item_template_id, {sql_literal('smoke:step68:drop:' + suffix)}, 'character', @character_id, 3, 'active', JSON_OBJECT('smoke','step68-drop'));
    INSERT INTO character_inventory(character_id, item_instance_id, bag_index, amount, source_amount, source_iterator_count)
    VALUES(@character_id, @drop_item_id, {drop_bag_index}, 3, 3, 3);
    INSERT INTO world_entity_state(world_instance_id, entity_key, entity_kind, lifecycle_state, state_json, row_version)
    VALUES(@world_id, {sql_literal(npc_entity_key)}, 'npc', 'dead', JSON_OBJECT('smoke','step68-npc'), 1)
    ON DUPLICATE KEY UPDATE lifecycle_state='dead', state_json=VALUES(state_json), row_version=world_entity_state.row_version + 1;
    INSERT INTO item_instances(item_instance_id, realm_id, item_template_id, item_instance_key, owner_type, owner_id, quantity, lifecycle_state, raw_payload)
    VALUES(@loot_item_id, @realm_id, @item_template_id, {sql_literal('smoke:step68:loot:' + suffix)}, 'container', NULL, 2, 'active', JSON_OBJECT('smoke','step68-loot'));
    INSERT INTO world_inventory(world_instance_id, owner_entity_key, item_instance_id, amount, source_amount, source_iterator_count)
    VALUES(@world_id, {sql_literal(npc_entity_key)}, @loot_item_id, 2, 2, 2);
    SELECT BIN_TO_UUID(@drop_item_id,1), BIN_TO_UUID(@loot_item_id,1);
    """,
    )
    if fixture_raw is None:
        return result
    drop_item_uuid, loot_item_uuid = last_row(fixture_raw)
    result["drop_item_uuid"] = drop_item_uuid
    result["loot_item_uuid"] = loot_item_uuid

    drop_raw = smoke_phase(
        result,
        "drop_character_item",
        target,
        f"""
    SET @event_id=NULL; SET @amount_remaining=NULL; SET @amount_dropped=NULL;
    CALL mmo_drop_character_item(UUID_TO_BIN({sql_literal(session_uuid)},1), UUID_TO_BIN({sql_literal(drop_item_uuid)},1), 1, {sql_literal(drop_entity_key)}, 1.0, 2.0, 3.0, 6801, JSON_OBJECT('smoke',true), {sql_literal(drop_idem)}, @event_id, @amount_remaining, @amount_dropped);
    SELECT BIN_TO_UUID(@event_id,1), @amount_remaining, @amount_dropped,
           (SELECT COUNT(*) FROM world_event_journal WHERE idempotency_key={sql_literal(drop_idem)} AND event_type='character_item_dropped' AND event_class='inventory'),
           (SELECT COUNT(*) FROM world_item_audit WHERE idempotency_key={sql_literal(drop_idem)} AND audit_type='drop'),
           (SELECT amount FROM character_inventory WHERE item_instance_id=UUID_TO_BIN({sql_literal(drop_item_uuid)},1) LIMIT 1),
           (SELECT COUNT(*) FROM world_inventory WHERE owner_entity_key={sql_literal(drop_entity_key)} AND amount=1);
    """,
    )
    if drop_raw is None:
        return result
    drop_event, drop_remaining, drop_amount, drop_events, drop_audits, char_amount_after, dropped_world_rows = last_row(drop_raw)
    result["drop"] = {
        "event_uuid": drop_event,
        "amount_remaining": int(drop_remaining or 0),
        "amount_dropped": int(drop_amount or 0),
        "event_count": int(drop_events or 0),
        "audit_count": int(drop_audits or 0),
        "character_amount_after": int(char_amount_after or 0),
        "world_rows": int(dropped_world_rows or 0),
    }

    loot_raw = smoke_phase(
        result,
        "loot_npc_inventory",
        target,
        f"""
    SET @event_id=NULL; SET @source_amount_remaining=NULL; SET @amount_looted=NULL;
    CALL mmo_loot_npc_inventory(UUID_TO_BIN({sql_literal(session_uuid)},1), {sql_literal(npc_entity_key)}, UUID_TO_BIN({sql_literal(loot_item_uuid)},1), 1, {loot_bag_index}, 6802, JSON_OBJECT('smoke',true), {sql_literal(loot_idem)}, @event_id, @source_amount_remaining, @amount_looted);
    SELECT BIN_TO_UUID(@event_id,1), @source_amount_remaining, @amount_looted,
           (SELECT COUNT(*) FROM world_event_journal WHERE idempotency_key={sql_literal(loot_idem)} AND event_type='npc_inventory_looted' AND event_class='inventory'),
           (SELECT COUNT(*) FROM world_item_audit WHERE idempotency_key={sql_literal(loot_idem)} AND audit_type='loot_npc_inventory'),
           (SELECT amount FROM world_inventory WHERE owner_entity_key={sql_literal(npc_entity_key)} AND item_instance_id=UUID_TO_BIN({sql_literal(loot_item_uuid)},1) LIMIT 1),
           (SELECT COUNT(*) FROM character_inventory WHERE character_id=(SELECT character_id FROM characters WHERE character_key={sql_literal(character_key)} LIMIT 1) AND bag_index={loot_bag_index} AND amount=1);
    """,
    )
    if loot_raw is None:
        return result
    loot_event, source_remaining, amount_looted, loot_events, loot_audits, source_amount_after, character_loot_rows = last_row(loot_raw)
    result["loot"] = {
        "event_uuid": loot_event,
        "source_amount_remaining": int(source_remaining or 0),
        "amount_looted": int(amount_looted or 0),
        "event_count": int(loot_events or 0),
        "audit_count": int(loot_audits or 0),
        "source_amount_after": int(source_amount_after or 0),
        "character_rows": int(character_loot_rows or 0),
    }

    drop_ok = (
        bool(drop_event)
        and result["drop"]["amount_remaining"] == 2
        and result["drop"]["amount_dropped"] == 1
        and result["drop"]["event_count"] == 1
        and result["drop"]["audit_count"] == 1
        and result["drop"]["character_amount_after"] == 2
        and result["drop"]["world_rows"] == 1
    )
    loot_ok = (
        bool(loot_event)
        and result["loot"]["source_amount_remaining"] == 1
        and result["loot"]["amount_looted"] == 1
        and result["loot"]["event_count"] == 1
        and result["loot"]["audit_count"] == 1
        and result["loot"]["source_amount_after"] == 1
        and result["loot"]["character_rows"] == 1
    )
    result["ok"] = bool(drop_ok and loot_ok)
    return result


def main() -> int:
    ap = argparse.ArgumentParser(description="Check Step68 drop/loot inventory bridge.")
    ap.add_argument("--url", required=True)
    ap.add_argument("--account-name", default="local-import")
    ap.add_argument("--character-key", default="PC_HERO")
    ap.add_argument("--smoke", action="store_true")
    ap.add_argument("--output", default="")
    args = ap.parse_args()

    target = parse_mysql_url(args.url)
    result: dict[str, object] = {
        "step": "68_drop_loot_inventory_bridge",
        "checked_at": datetime.now(timezone.utc).isoformat(),
        "database": target.database,
    }
    result.update(check_objects(target))
    if args.smoke and not result["missing_tables"] and not result["missing_routines"]:
        result["smoke"] = run_smoke(target, args.account_name, args.character_key)

    errors = []
    if result["missing_tables"]:
        errors.append("missing_tables")
    if result["missing_routines"]:
        errors.append("missing_routines")
    if args.smoke and not result.get("smoke", {}).get("ok"):
        errors.append("smoke")
    result["status"] = "failed" if errors else "passed"
    result["errors"] = errors

    if args.output:
        out = Path(args.output)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print("status=" + result["status"])
    return 0 if result["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
