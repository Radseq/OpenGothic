#!/usr/bin/env python3
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


REQUIRED_ROUTINES = (
    "mmo_equip_character_item",
    "mmo_unequip_character_item",
    "mmo_transfer_character_item",
)

REQUIRED_TABLES = (
    "character_equipment",
    "character_inventory",
    "character_inventory_audit",
)


@dataclass(frozen=True)
class Target:
    host: str
    port: int
    user: str
    password: str
    database: str


class MysqlError(RuntimeError):
    def __init__(self, returncode: int, stdout: str, stderr: str, sql: str) -> None:
        super().__init__(f"mysql exited with status {returncode}")
        self.returncode = returncode
        self.stdout = stdout
        self.stderr = stderr
        self.sql = sql


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


def run_mysql(target: Target, sql: str) -> str:
    proc = subprocess.run(mysql_cmd(target) + ["--execute", sql], text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    if proc.returncode != 0:
        if proc.stderr:
            print(proc.stderr, file=sys.stderr, end="")
        raise MysqlError(proc.returncode, proc.stdout, proc.stderr, sql)
    return proc.stdout.strip()


def sql_literal(value: object) -> str:
    if value is None:
        return "NULL"
    if isinstance(value, bool):
        return "TRUE" if value else "FALSE"
    if isinstance(value, (int, float)):
        return str(value)
    text = str(value)
    return "'" + text.replace("\\", "\\\\").replace("'", "''") + "'"


def line_set(raw: str) -> set[str]:
    return {line.strip() for line in raw.splitlines() if line.strip()}


def last_row(raw: str) -> list[str]:
    return (raw.splitlines()[-1] if raw else "").split("\t")


def check_objects(target: Target) -> dict[str, object]:
    db = target.database.replace("'", "''")
    tables = line_set(run_mysql(target, f"SELECT table_name FROM information_schema.tables WHERE table_schema='{db}' AND table_type='BASE TABLE';"))
    routines = line_set(run_mysql(target, f"SELECT routine_name FROM information_schema.routines WHERE routine_schema='{db}';"))
    return {
        "missing_tables": sorted(set(REQUIRED_TABLES) - tables),
        "missing_routines": sorted(set(REQUIRED_ROUTINES) - routines),
    }


def smoke_phase(result: dict[str, object], name: str, target: Target, sql: str) -> str | None:
    phases = result.setdefault("phases", [])
    assert isinstance(phases, list)
    phase: dict[str, object] = {"name": name}
    try:
        raw = run_mysql(target, sql)
    except MysqlError as exc:
        phase.update(
            {
                "ok": False,
                "error": str(exc),
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
    session_key = f"step60-smoke:{suffix}"
    item_key = f"import:smoke:step60:character-item:{suffix}"
    bag_index = 1_600_000_000 + (int(suffix[:8], 16) % 300_000_000)
    result: dict[str, object] = {"session_key": session_key, "item_key": item_key, "bag_index": bag_index, "ok": False}

    login_raw = smoke_phase(
        result,
        "login",
        target,
        f"""
    SET @session_id=NULL;
    CALL mmo_login_character({sql_literal(account_name)}, {sql_literal(character_key)}, {sql_literal(session_key)}, 'step60-smoke', 'local', JSON_OBJECT('tool','check_mmo_step60'), @session_id);
    SELECT BIN_TO_UUID(@session_id,1);
    """,
    )
    if login_raw is None:
        return result
    session_uuid = last_row(login_raw)[0]
    result["session_uuid"] = session_uuid

    fixture_raw = smoke_phase(
        result,
        "fixture_inventory_item",
        target,
        f"""
    SET @character_id=(SELECT character_id FROM characters WHERE character_key={sql_literal(character_key)} LIMIT 1);
    SET @realm_id=(SELECT realm_id FROM characters WHERE character_key={sql_literal(character_key)} LIMIT 1);
    SET @content_revision_id=(SELECT active_content_revision_id FROM realm_realms WHERE realm_id=@realm_id LIMIT 1);
    SET @item_template_id=(SELECT item_template_id FROM content_item_templates WHERE content_revision_id=@content_revision_id ORDER BY created_at DESC, item_template_key DESC LIMIT 1);
    SET @item_template_id=COALESCE(@item_template_id, (SELECT item_template_id FROM content_item_templates ORDER BY created_at DESC, item_template_key DESC LIMIT 1));

    INSERT INTO item_instances(realm_id, item_template_id, item_instance_key, owner_type, owner_id, quantity, lifecycle_state, raw_payload)
    VALUES(@realm_id, @item_template_id, {sql_literal(item_key)}, 'character', @character_id, 1, 'active', JSON_OBJECT('tool','check_mmo_step60','smoke',true))
    ON DUPLICATE KEY UPDATE owner_type=VALUES(owner_type), owner_id=VALUES(owner_id), quantity=VALUES(quantity), lifecycle_state=VALUES(lifecycle_state), raw_payload=VALUES(raw_payload);

    SET @item_id=(SELECT item_instance_id FROM item_instances WHERE item_instance_key={sql_literal(item_key)} LIMIT 1);
    INSERT INTO character_inventory(character_id, item_instance_id, bag_index, amount, source_amount, source_iterator_count)
    VALUES(@character_id, @item_id, {bag_index}, 1, 1, 1)
    ON DUPLICATE KEY UPDATE bag_index=VALUES(bag_index), amount=VALUES(amount), source_amount=VALUES(source_amount), source_iterator_count=VALUES(source_iterator_count);

    DELETE FROM character_equipment WHERE character_id=@character_id AND (equipment_slot='torch' OR item_instance_id=@item_id);
    SELECT BIN_TO_UUID(@item_id,1);
    """,
    )
    if fixture_raw is None:
        return result
    item_uuid = last_row(fixture_raw)[0]
    result["item_instance_uuid"] = item_uuid

    equip_key = f"step60:equip:{suffix}"
    equip_raw = smoke_phase(
        result,
        "equip",
        target,
        f"""
    SET @event_id=NULL;
    CALL mmo_equip_character_item(UUID_TO_BIN({sql_literal(session_uuid)},1), UUID_TO_BIN({sql_literal(item_uuid)},1), 'torch', 6001, JSON_OBJECT('smoke',true), {sql_literal(equip_key)}, @event_id);
    SELECT BIN_TO_UUID(@event_id,1),
           (SELECT COUNT(*) FROM world_event_journal WHERE idempotency_key={sql_literal(equip_key)}),
           (SELECT COUNT(*) FROM character_inventory_audit WHERE idempotency_key={sql_literal(equip_key)}),
           (SELECT COUNT(*) FROM character_equipment WHERE item_instance_id=UUID_TO_BIN({sql_literal(item_uuid)},1) AND equipment_slot='torch');
    """,
    )
    if equip_raw is None:
        return result
    equip_event, equip_event_count, equip_audit_count, equipped_count = last_row(equip_raw)
    result.update(
        {
            "equip_event_uuid": equip_event,
            "equip_event_count": int(equip_event_count or 0),
            "equip_audit_count": int(equip_audit_count or 0),
            "equipped_count": int(equipped_count or 0),
        }
    )

    retry_raw = smoke_phase(
        result,
        "equip_idempotent_retry",
        target,
        f"""
    SET @event_id=NULL;
    CALL mmo_equip_character_item(UUID_TO_BIN({sql_literal(session_uuid)},1), UUID_TO_BIN({sql_literal(item_uuid)},1), 'torch', 6002, JSON_OBJECT('smoke',true,'retry',true), {sql_literal(equip_key)}, @event_id);
    SELECT BIN_TO_UUID(@event_id,1),
           (SELECT COUNT(*) FROM world_event_journal WHERE idempotency_key={sql_literal(equip_key)}),
           (SELECT COUNT(*) FROM character_inventory_audit WHERE idempotency_key={sql_literal(equip_key)});
    """,
    )
    if retry_raw is None:
        return result
    retry_event, retry_event_count, retry_audit_count = last_row(retry_raw)
    result.update(
        {
            "equip_retry_event_uuid": retry_event,
            "equip_retry_event_count": int(retry_event_count or 0),
            "equip_retry_audit_count": int(retry_audit_count or 0),
        }
    )

    unequip_key = f"step60:unequip:{suffix}"
    unequip_raw = smoke_phase(
        result,
        "unequip",
        target,
        f"""
    SET @event_id=NULL; SET @item_id=NULL;
    CALL mmo_unequip_character_item(UUID_TO_BIN({sql_literal(session_uuid)},1), 'torch', 6003, JSON_OBJECT('smoke',true), {sql_literal(unequip_key)}, @event_id, @item_id);
    SELECT BIN_TO_UUID(@event_id,1), BIN_TO_UUID(@item_id,1),
           (SELECT COUNT(*) FROM world_event_journal WHERE idempotency_key={sql_literal(unequip_key)}),
           (SELECT COUNT(*) FROM character_inventory_audit WHERE idempotency_key={sql_literal(unequip_key)}),
           (SELECT COUNT(*) FROM character_equipment WHERE item_instance_id=UUID_TO_BIN({sql_literal(item_uuid)},1) AND equipment_slot='torch');
    """,
    )
    if unequip_raw is None:
        return result
    unequip_event, unequip_item, unequip_event_count, unequip_audit_count, still_equipped = last_row(unequip_raw)
    result.update(
        {
            "unequip_event_uuid": unequip_event,
            "unequip_item_instance_uuid": unequip_item,
            "unequip_event_count": int(unequip_event_count or 0),
            "unequip_audit_count": int(unequip_audit_count or 0),
            "still_equipped_count": int(still_equipped or 0),
        }
    )

    result["ok"] = (
        bool(result.get("equip_event_uuid"))
        and result.get("equip_event_uuid") == result.get("equip_retry_event_uuid")
        and result.get("equip_event_count") == 1
        and result.get("equip_audit_count") == 1
        and result.get("equip_retry_event_count") == 1
        and result.get("equip_retry_audit_count") == 1
        and result.get("equipped_count") == 1
        and bool(result.get("unequip_event_uuid"))
        and result.get("unequip_item_instance_uuid") == item_uuid
        and result.get("unequip_event_count") == 1
        and result.get("unequip_audit_count") == 1
        and result.get("still_equipped_count") == 0
    )
    return result


def main() -> int:
    ap = argparse.ArgumentParser(description="Check Step60 clean-DB equipment bridge.")
    ap.add_argument("--url", required=True)
    ap.add_argument("--account-name", default="local-import")
    ap.add_argument("--character-key", default="PC_HERO")
    ap.add_argument("--smoke", action="store_true")
    ap.add_argument("--output", default="")
    args = ap.parse_args()

    target = parse_mysql_url(args.url)
    result: dict[str, object] = {
        "step": "60_clean_db_equipment_bridge",
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
