#!/usr/bin/env python3
"""Materialize Step53 typed MMO server read-model tables in MySQL.

The read model is deliberately boring: physical InnoDB tables, typed columns,
indexes and no JSON. It sits beside the existing authority-bridge schema so all
old tests keep working while the future C++/server runtime gets a sane bootstrap
surface.
"""
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any
from urllib.parse import unquote, urlparse

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SQL = ROOT / "server" / "sql" / "step53_server_read_model_v1.sql"
READ_MODEL_TABLES = [
    "mmo_server_read_model_meta",
    "mmo_server_character_read_model",
    "mmo_server_character_inventory_read_model",
    "mmo_server_character_quest_read_model",
    "mmo_server_known_dialog_read_model",
    "mmo_server_world_entity_read_model",
    "mmo_server_world_inventory_read_model",
    "mmo_server_interactive_read_model",
    "mmo_server_script_int_read_model",
    "mmo_server_world_clock_read_model",
    "mmo_server_waypoint_read_model",
    "mmo_server_waypoint_edge_read_model",
]


@dataclass(frozen=True)
class Target:
    host: str
    port: int
    user: str
    password: str
    database: str


def parse_mysql_url(url: str) -> Target:
    parsed = urlparse(url)
    if parsed.scheme not in {"mysql", "mysql+pymysql"}:
        raise ValueError("expected mysql:// URL")
    database = (parsed.path or "/").lstrip("/")
    if not database:
        raise ValueError("database is missing in mysql URL")
    return Target(
        host=parsed.hostname or "127.0.0.1",
        port=parsed.port or 3306,
        user=unquote(parsed.username or ""),
        password=unquote(parsed.password or ""),
        database=database,
    )


def mysql_cmd(target: Target) -> list[str]:
    exe = shutil.which("mysql")
    if exe is None:
        raise RuntimeError("mysql executable was not found in PATH")
    cmd = [
        exe,
        "--default-character-set=utf8mb4", "--init-command=SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci",
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


def run_mysql(target: Target, sql: str, *, echo_stdout: bool = False) -> str:
    proc = subprocess.run(
        mysql_cmd(target),
        input=sql,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if echo_stdout and proc.stdout:
        print(proc.stdout, end="")
    if proc.returncode != 0:
        if proc.stderr:
            print(proc.stderr, file=sys.stderr, end="")
        raise RuntimeError(f"mysql exited with status {proc.returncode}")
    return proc.stdout


def sql_literal(value: Any) -> str:
    if value is None:
        return "NULL"
    text = str(value)
    return "'" + text.replace("\\", "\\\\").replace("'", "''") + "'"


def qident(name: str) -> str:
    return "`" + name.replace("`", "``") + "`"


@dataclass(frozen=True)
class ColumnInfo:
    name: str
    data_type: str
    column_type: str
    character_maximum_length: str


class Schema:
    def __init__(self, target: Target) -> None:
        self.target = target
        self._columns: dict[str, dict[str, ColumnInfo]] = {}
        self._tables: set[str] | None = None

    def tables(self) -> set[str]:
        if self._tables is None:
            out = run_mysql(
                self.target,
                """
                SELECT table_name
                  FROM information_schema.tables
                 WHERE table_schema=DATABASE() AND table_type='BASE TABLE';
                """,
            )
            self._tables = {line.strip() for line in out.splitlines() if line.strip()}
        return self._tables

    def has_table(self, table: str) -> bool:
        return table in self.tables()

    def columns(self, table: str) -> dict[str, ColumnInfo]:
        if table not in self._columns:
            if not self.has_table(table):
                self._columns[table] = {}
                return self._columns[table]
            out = run_mysql(
                self.target,
                f"""
                SELECT column_name, data_type, column_type, COALESCE(character_maximum_length,'')
                  FROM information_schema.columns
                 WHERE table_schema=DATABASE() AND table_name={sql_literal(table)}
                 ORDER BY ordinal_position;
                """,
            )
            cols: dict[str, ColumnInfo] = {}
            for line in out.splitlines():
                if not line.strip():
                    continue
                parts = line.split("\t")
                while len(parts) < 4:
                    parts.append("")
                cols[parts[0]] = ColumnInfo(parts[0], parts[1].lower(), parts[2].lower(), parts[3])
            self._columns[table] = cols
        return self._columns[table]

    def has_col(self, table: str, column: str) -> bool:
        return column in self.columns(table)

    def first_col(self, table: str, candidates: list[str]) -> str | None:
        cols = self.columns(table)
        for col in candidates:
            if col in cols:
                return col
        return None

    def uuid_expr(self, alias: str, table: str, candidates: list[str], default_sql: str = "NULL") -> str:
        col = self.first_col(table, candidates)
        if col is None:
            return default_sql
        info = self.columns(table)[col]
        ref = f"{alias}.{qident(col)}"
        if info.data_type in {"binary", "varbinary"} and ("(16)" in info.column_type or info.character_maximum_length == "16"):
            return f"BIN_TO_UUID({ref},1)"
        return f"CAST({ref} AS CHAR)"

    def expr(self, alias: str, table: str, candidates: list[str], default_sql: str = "NULL", *, cast: str | None = None) -> str:
        col = self.first_col(table, candidates)
        if col is None:
            return default_sql
        ref = f"{alias}.{qident(col)}"
        return f"CAST({ref} AS {cast})" if cast else ref


def execute_sql_file(target: Target, path: Path) -> None:
    if not path.exists():
        raise FileNotFoundError(path)
    run_mysql(target, path.read_text(encoding="utf-8"), echo_stdout=True)


def count_rows(target: Target, table: str) -> int:
    out = run_mysql(target, f"SELECT COUNT(*) FROM {qident(table)};").strip()
    return int((out or "0").splitlines()[-1])


def insert_result(result: dict[str, Any], name: str, status: str, rows: int = 0, reason: str = "") -> None:
    result["materializers"].append({"name": name, "status": status, "rows": rows, "reason": reason})


def materialize_characters(target: Target, schema: Schema, result: dict[str, Any]) -> None:
    if not schema.has_table("characters"):
        insert_result(result, "characters", "skipped", reason="source table characters missing")
        return
    c_key = schema.expr("c", "characters", ["character_key", "key", "name"], "CAST(c.id AS CHAR)" if schema.has_col("characters", "id") else "'UNKNOWN_CHARACTER'")
    realm = schema.expr("c", "characters", ["realm_key", "realm", "realm_name"], "'default'")
    account = schema.expr("c", "characters", ["account_key", "account_name", "account_id"], "NULL")
    display = schema.expr("c", "characters", ["display_name", "name", "character_name"], "NULL")
    lifecycle = schema.expr("c", "characters", ["lifecycle_state", "state"], "'active'")
    updated = schema.expr("c", "characters", ["updated_at", "captured_at", "created_at"], "NULL")

    joins: list[str] = []
    if schema.has_table("character_positions") and schema.has_col("character_positions", "character_key"):
        joins.append(f"LEFT JOIN character_positions p ON p.character_key = {c_key}")
        world = schema.expr("p", "character_positions", ["world_name", "world_key"], schema.expr("c", "characters", ["world_name", "world_key"], "'UNKNOWN'"))
        pos_x = schema.expr("p", "character_positions", ["pos_x", "x"], "NULL")
        pos_y = schema.expr("p", "character_positions", ["pos_y", "y"], "NULL")
        pos_z = schema.expr("p", "character_positions", ["pos_z", "z"], "NULL")
        angle_y = schema.expr("p", "character_positions", ["angle_y", "rotation_y", "yaw"], "NULL")
    else:
        world = schema.expr("c", "characters", ["world_name", "world_key"], "'UNKNOWN'")
        pos_x = pos_y = pos_z = angle_y = "NULL"

    if schema.has_table("character_stats") and schema.has_col("character_stats", "character_key"):
        joins.append(f"LEFT JOIN character_stats s ON s.character_key = {c_key}")
        hp = schema.expr("s", "character_stats", ["health_current", "hp_current", "health", "hp"], "NULL", cast="SIGNED")
        hp_max = schema.expr("s", "character_stats", ["health_max", "hp_max"], "NULL", cast="SIGNED")
        mana = schema.expr("s", "character_stats", ["mana_current", "mp_current", "mana", "mp"], "NULL", cast="SIGNED")
        mana_max = schema.expr("s", "character_stats", ["mana_max", "mp_max"], "NULL", cast="SIGNED")
        level = schema.expr("s", "character_stats", ["level", "level_value"], "NULL", cast="SIGNED")
        exp = schema.expr("s", "character_stats", ["experience", "experience_value"], "NULL", cast="SIGNED")
        exp_next = schema.expr("s", "character_stats", ["experience_next"], "NULL", cast="SIGNED")
        lp = schema.expr("s", "character_stats", ["learning_points", "lp"], "NULL", cast="SIGNED")
    else:
        hp = hp_max = mana = mana_max = level = exp = exp_next = lp = "NULL"

    wallet_join = ""
    gold = "NULL"
    if schema.has_table("character_wallets") and schema.has_col("character_wallets", "character_key"):
        amount_col = schema.first_col("character_wallets", ["amount", "gold", "value"])
        if amount_col:
            currency_filter = ""
            if schema.has_col("character_wallets", "currency_key"):
                currency_filter = " WHERE currency_key IN ('gold','GOLD','ItMi_Gold','ITMI_GOLD')"
            wallet_join = f"LEFT JOIN (SELECT character_key, MAX({qident(amount_col)}) AS amount FROM character_wallets{currency_filter} GROUP BY character_key) w ON w.character_key = {c_key}"
            joins.append(wallet_join)
            gold = "w.amount"

    sql = f"""
    DELETE FROM mmo_server_character_read_model;
    INSERT INTO mmo_server_character_read_model
      (realm_key, character_key, account_key, display_name, world_name, pos_x, pos_y, pos_z, angle_y,
       health_current, health_max, mana_current, mana_max, level_value, experience_value, experience_next,
       learning_points, gold_amount, lifecycle_state, source_updated_at)
    SELECT DISTINCT
       COALESCE(CAST({realm} AS CHAR), 'default'),
       CAST({c_key} AS CHAR),
       CAST({account} AS CHAR),
       CAST({display} AS CHAR),
       COALESCE(CAST({world} AS CHAR), 'UNKNOWN'),
       {pos_x}, {pos_y}, {pos_z}, {angle_y},
       {hp}, {hp_max}, {mana}, {mana_max}, {level}, {exp}, {exp_next}, {lp}, {gold},
       COALESCE(CAST({lifecycle} AS CHAR), 'active'),
       {updated}
      FROM characters c
      {' '.join(joins)};
    """
    run_mysql(target, sql)
    insert_result(result, "characters", "materialized", count_rows(target, "mmo_server_character_read_model"))


def materialize_character_inventory(target: Target, schema: Schema, result: dict[str, Any]) -> None:
    if not schema.has_table("character_inventory"):
        insert_result(result, "character_inventory", "skipped", reason="source table character_inventory missing")
        return
    table = "character_inventory"
    realm = schema.expr("i", table, ["realm_key", "realm"], "'default'")
    char = schema.expr("i", table, ["character_key"], "'UNKNOWN_CHARACTER'")
    instance = schema.uuid_expr("i", table, ["item_instance_id", "item_instance_uuid", "item_instance_key", "item_key", "id"], "CONCAT('row:', CAST(i.id AS CHAR))" if schema.has_col(table, "id") else "UUID()")
    template = schema.expr("i", table, ["item_template_key", "template_key", "symbol_name", "item_symbol", "item_key"], "NULL")
    display = schema.expr("i", table, ["display_name", "name"], "NULL")
    amount = schema.expr("i", table, ["amount", "quantity", "iterator_count", "count"], "1", cast="SIGNED")
    equipped = schema.expr("i", table, ["equipped", "is_equipped"], "0", cast="SIGNED")
    slot = schema.expr("i", table, ["slot_key", "slot", "equip_slot"], "NULL")
    bind = schema.expr("i", table, ["bind_state"], "NULL")
    lifecycle = schema.expr("i", table, ["lifecycle_state", "state"], "'active'")
    sql = f"""
    DELETE FROM mmo_server_character_inventory_read_model;
    INSERT INTO mmo_server_character_inventory_read_model
      (realm_key, character_key, item_instance_key, item_template_key, display_name, amount, equipped, slot_key, bind_state, lifecycle_state)
    SELECT
      COALESCE(CAST({realm} AS CHAR),'default'),
      COALESCE(CAST({char} AS CHAR),'UNKNOWN_CHARACTER'),
      COALESCE(CAST({instance} AS CHAR), UUID()),
      CAST({template} AS CHAR),
      CAST({display} AS CHAR),
      COALESCE({amount},1),
      IF(COALESCE({equipped},0) <> 0, 1, 0),
      CAST({slot} AS CHAR),
      CAST({bind} AS CHAR),
      COALESCE(CAST({lifecycle} AS CHAR),'active')
    FROM character_inventory i;
    """
    run_mysql(target, sql)
    insert_result(result, "character_inventory", "materialized", count_rows(target, "mmo_server_character_inventory_read_model"))


def materialize_character_quests(target: Target, schema: Schema, result: dict[str, Any]) -> None:
    if not schema.has_table("character_quests"):
        insert_result(result, "character_quests", "skipped", reason="source table character_quests missing")
        return
    table = "character_quests"
    realm = schema.expr("q", table, ["realm_key", "realm"], "'default'")
    char = schema.expr("q", table, ["character_key"], "'UNKNOWN_CHARACTER'")
    quest = schema.expr("q", table, ["quest_key", "quest_id", "symbol_name", "name"], "CAST(q.id AS CHAR)" if schema.has_col(table, "id") else "'UNKNOWN_QUEST'")
    name = schema.expr("q", table, ["quest_name", "name", "display_name"], "NULL")
    status = schema.expr("q", table, ["status_key", "status", "state"], "'unknown'")
    entries = schema.expr("q", table, ["entry_count", "entries_count"], "0", cast="SIGNED")
    updated = schema.expr("q", table, ["updated_at", "captured_at", "created_at"], "NULL")
    sql = f"""
    DELETE FROM mmo_server_character_quest_read_model;
    INSERT INTO mmo_server_character_quest_read_model
      (realm_key, character_key, quest_key, quest_name, status_key, entry_count, updated_at)
    SELECT
      COALESCE(CAST({realm} AS CHAR),'default'),
      COALESCE(CAST({char} AS CHAR),'UNKNOWN_CHARACTER'),
      COALESCE(CAST({quest} AS CHAR), UUID()),
      CAST({name} AS CHAR),
      COALESCE(CAST({status} AS CHAR),'unknown'),
      COALESCE({entries},0),
      {updated}
    FROM character_quests q;
    """
    run_mysql(target, sql)
    insert_result(result, "character_quests", "materialized", count_rows(target, "mmo_server_character_quest_read_model"))


def materialize_known_dialogs(target: Target, schema: Schema, result: dict[str, Any]) -> None:
    if not schema.has_table("character_known_dialogs"):
        insert_result(result, "known_dialogs", "skipped", reason="source table character_known_dialogs missing")
        return
    table = "character_known_dialogs"
    realm = schema.expr("d", table, ["realm_key", "realm"], "'default'")
    char = schema.expr("d", table, ["character_key"], "'UNKNOWN_CHARACTER'")
    npc = schema.expr("d", table, ["npc_symbol_name", "npc_key", "npc_symbol"], "'UNKNOWN_NPC'")
    info = schema.expr("d", table, ["info_symbol_name", "info_key", "dialog_key"], "'UNKNOWN_INFO'")
    state = schema.expr("d", table, ["availability_state", "state", "status"], "NULL")
    tick = schema.expr("d", table, ["first_seen_tick", "tick_count", "seen_tick"], "NULL", cast="SIGNED")
    sql = f"""
    DELETE FROM mmo_server_known_dialog_read_model;
    INSERT IGNORE INTO mmo_server_known_dialog_read_model
      (realm_key, character_key, npc_symbol_name, info_symbol_name, availability_state, first_seen_tick)
    SELECT
      COALESCE(CAST({realm} AS CHAR),'default'),
      COALESCE(CAST({char} AS CHAR),'UNKNOWN_CHARACTER'),
      COALESCE(CAST({npc} AS CHAR),'UNKNOWN_NPC'),
      COALESCE(CAST({info} AS CHAR), UUID()),
      CAST({state} AS CHAR),
      {tick}
    FROM character_known_dialogs d;
    """
    run_mysql(target, sql)
    insert_result(result, "known_dialogs", "materialized", count_rows(target, "mmo_server_known_dialog_read_model"))


def materialize_world_entities(target: Target, schema: Schema, result: dict[str, Any]) -> None:
    if not schema.has_table("world_entity_state"):
        insert_result(result, "world_entity_state", "skipped", reason="source table world_entity_state missing")
        return
    table = "world_entity_state"
    realm = schema.expr("e", table, ["realm_key", "realm"], "'default'")
    world = schema.expr("e", table, ["world_name", "world_key"], "'UNKNOWN'")
    entity = schema.expr("e", table, ["entity_key", "vob_key", "npc_key", "spawn_key"], "CAST(e.id AS CHAR)" if schema.has_col(table, "id") else "UUID()")
    kind = schema.expr("e", table, ["entity_kind", "kind", "entity_type", "type"], "'unknown'")
    template = schema.expr("e", table, ["template_key", "entity_template_key", "npc_template_key", "item_template_key"], "NULL")
    symbol = schema.expr("e", table, ["script_symbol_name", "symbol_name", "npc_symbol_name"], "NULL")
    display = schema.expr("e", table, ["display_name", "name"], "NULL")
    active = schema.expr("e", table, ["active", "exists_in_world", "enabled"], "1", cast="SIGNED")
    dead = schema.expr("e", table, ["dead", "is_dead"], "0", cast="SIGNED")
    hp = schema.expr("e", table, ["health_current", "hp_current", "health", "hp"], "NULL", cast="SIGNED")
    hpmax = schema.expr("e", table, ["health_max", "hp_max"], "NULL", cast="SIGNED")
    px = schema.expr("e", table, ["pos_x", "x"], "NULL")
    py = schema.expr("e", table, ["pos_y", "y"], "NULL")
    pz = schema.expr("e", table, ["pos_z", "z"], "NULL")
    ay = schema.expr("e", table, ["angle_y", "rotation_y", "yaw"], "NULL")
    wpk = schema.expr("e", table, ["current_waypoint_key", "waypoint_key"], "NULL")
    wpn = schema.expr("e", table, ["current_waypoint_name", "waypoint_name"], "NULL")
    lifecycle = schema.expr("e", table, ["lifecycle_state", "state"], "'active'")
    updated = schema.expr("e", table, ["updated_at", "captured_at", "created_at"], "NULL")
    sql = f"""
    DELETE FROM mmo_server_world_entity_read_model;
    INSERT INTO mmo_server_world_entity_read_model
      (realm_key, world_name, entity_key, entity_kind, template_key, script_symbol_name, display_name,
       active, dead, health_current, health_max, pos_x, pos_y, pos_z, angle_y,
       current_waypoint_key, current_waypoint_name, lifecycle_state, source_updated_at)
    SELECT
      COALESCE(CAST({realm} AS CHAR),'default'),
      COALESCE(CAST({world} AS CHAR),'UNKNOWN'),
      COALESCE(CAST({entity} AS CHAR), UUID()),
      COALESCE(CAST({kind} AS CHAR),'unknown'),
      CAST({template} AS CHAR),
      CAST({symbol} AS CHAR),
      CAST({display} AS CHAR),
      IF(COALESCE({active},1) <> 0, 1, 0),
      IF(COALESCE({dead},0) <> 0, 1, 0),
      {hp}, {hpmax}, {px}, {py}, {pz}, {ay},
      CAST({wpk} AS CHAR), CAST({wpn} AS CHAR),
      COALESCE(CAST({lifecycle} AS CHAR),'active'),
      {updated}
    FROM world_entity_state e;
    """
    run_mysql(target, sql)
    insert_result(result, "world_entity_state", "materialized", count_rows(target, "mmo_server_world_entity_read_model"))


def materialize_world_inventory(target: Target, schema: Schema, result: dict[str, Any]) -> None:
    if not schema.has_table("world_inventory"):
        insert_result(result, "world_inventory", "skipped", reason="source table world_inventory missing")
        return
    table = "world_inventory"
    realm = schema.expr("i", table, ["realm_key", "realm"], "'default'")
    world = schema.expr("i", table, ["world_name", "world_key"], "'UNKNOWN'")
    owner = schema.expr("i", table, ["owner_key", "container_key", "world_entity_key", "entity_key"], "'world'")
    owner_kind = schema.expr("i", table, ["owner_kind", "container_kind"], "'world'")
    instance = schema.uuid_expr("i", table, ["item_instance_id", "item_instance_uuid", "item_instance_key", "item_key", "id"], "CONCAT('world-row:', CAST(i.id AS CHAR))" if schema.has_col(table, "id") else "UUID()")
    template = schema.expr("i", table, ["item_template_key", "template_key", "symbol_name", "item_symbol", "item_key"], "NULL")
    display = schema.expr("i", table, ["display_name", "name"], "NULL")
    amount = schema.expr("i", table, ["amount", "quantity", "iterator_count", "count"], "1", cast="SIGNED")
    px = schema.expr("i", table, ["pos_x", "x"], "NULL")
    py = schema.expr("i", table, ["pos_y", "y"], "NULL")
    pz = schema.expr("i", table, ["pos_z", "z"], "NULL")
    lifecycle = schema.expr("i", table, ["lifecycle_state", "state"], "'active'")
    sql = f"""
    DELETE FROM mmo_server_world_inventory_read_model;
    INSERT INTO mmo_server_world_inventory_read_model
      (realm_key, world_name, owner_key, owner_kind, item_instance_key, item_template_key, display_name, amount, pos_x, pos_y, pos_z, lifecycle_state)
    SELECT
      COALESCE(CAST({realm} AS CHAR),'default'),
      COALESCE(CAST({world} AS CHAR),'UNKNOWN'),
      COALESCE(CAST({owner} AS CHAR),'world'),
      COALESCE(CAST({owner_kind} AS CHAR),'world'),
      COALESCE(CAST({instance} AS CHAR), UUID()),
      CAST({template} AS CHAR),
      CAST({display} AS CHAR),
      COALESCE({amount},1),
      {px}, {py}, {pz},
      COALESCE(CAST({lifecycle} AS CHAR),'active')
    FROM world_inventory i;
    """
    run_mysql(target, sql)
    insert_result(result, "world_inventory", "materialized", count_rows(target, "mmo_server_world_inventory_read_model"))


def materialize_script_ints(target: Target, schema: Schema, result: dict[str, Any]) -> None:
    inserts: list[str] = ["DELETE FROM mmo_server_script_int_read_model"]
    any_source = False
    if schema.has_table("character_script_state"):
        table = "character_script_state"
        realm = schema.expr("s", table, ["realm_key", "realm"], "'default'")
        owner = schema.expr("s", table, ["character_key", "owner_key"], "''")
        sym = schema.expr("s", table, ["symbol_name", "global_key", "script_key"], "CAST(s.id AS CHAR)" if schema.has_col(table, "id") else "'UNKNOWN_SYMBOL'")
        value = schema.expr("s", table, ["int_value", "value_int", "value", "value_number"], "NULL", cast="SIGNED")
        cat = schema.expr("s", table, ["category_key", "category"], "NULL")
        upd = schema.expr("s", table, ["updated_at", "captured_at", "created_at"], "NULL")
        inserts.append(f"""
        INSERT IGNORE INTO mmo_server_script_int_read_model
          (realm_key, scope_key, owner_key, symbol_name, int_value, category_key, updated_at)
        SELECT COALESCE(CAST({realm} AS CHAR),'default'), 'character', COALESCE(CAST({owner} AS CHAR),''), COALESCE(CAST({sym} AS CHAR), UUID()), {value}, CAST({cat} AS CHAR), {upd}
          FROM character_script_state s
        """)
        any_source = True
    if schema.has_table("world_script_state"):
        table = "world_script_state"
        realm = schema.expr("s", table, ["realm_key", "realm"], "'default'")
        owner = schema.expr("s", table, ["world_name", "world_key", "owner_key"], "''")
        sym = schema.expr("s", table, ["symbol_name", "global_key", "script_key"], "CAST(s.id AS CHAR)" if schema.has_col(table, "id") else "'UNKNOWN_SYMBOL'")
        value = schema.expr("s", table, ["int_value", "value_int", "value", "value_number"], "NULL", cast="SIGNED")
        cat = schema.expr("s", table, ["category_key", "category"], "NULL")
        upd = schema.expr("s", table, ["updated_at", "captured_at", "created_at"], "NULL")
        inserts.append(f"""
        INSERT IGNORE INTO mmo_server_script_int_read_model
          (realm_key, scope_key, owner_key, symbol_name, int_value, category_key, updated_at)
        SELECT COALESCE(CAST({realm} AS CHAR),'default'), 'world', COALESCE(CAST({owner} AS CHAR),''), COALESCE(CAST({sym} AS CHAR), UUID()), {value}, CAST({cat} AS CHAR), {upd}
          FROM world_script_state s
        """)
        any_source = True
    if not any_source:
        insert_result(result, "script_ints", "skipped", reason="character_script_state/world_script_state missing")
        return
    run_mysql(target, ";\n".join(inserts) + ";")
    insert_result(result, "script_ints", "materialized", count_rows(target, "mmo_server_script_int_read_model"))


def materialize_clock(target: Target, schema: Schema, result: dict[str, Any]) -> None:
    source = "mmo_world_clock_state_current" if schema.has_table("mmo_world_clock_state_current") else "world_clock_state"
    if not schema.has_table(source):
        insert_result(result, "world_clock", "skipped", reason="no world clock source table")
        return
    realm = schema.expr("c", source, ["realm_key", "realm"], "'default'")
    world = schema.expr("c", source, ["world_name", "world_key"], "'UNKNOWN'")
    day = schema.expr("c", source, ["day_value", "day", "world_day"], "NULL", cast="SIGNED")
    hour = schema.expr("c", source, ["hour_value", "hour", "world_hour"], "NULL", cast="SIGNED")
    minute = schema.expr("c", source, ["minute_value", "minute", "world_minute"], "NULL", cast="SIGNED")
    absolute = schema.expr("c", source, ["absolute_minute", "total_minutes"], "NULL", cast="SIGNED")
    reason = schema.expr("c", source, ["source_reason", "reason"], "NULL")
    updated = schema.expr("c", source, ["updated_at", "created_at", "changed_at"], "NULL")
    sql = f"""
    DELETE FROM mmo_server_world_clock_read_model;
    INSERT INTO mmo_server_world_clock_read_model
      (realm_key, world_name, day_value, hour_value, minute_value, absolute_minute, source_reason, updated_at)
    SELECT COALESCE(CAST({realm} AS CHAR),'default'), COALESCE(CAST({world} AS CHAR),'UNKNOWN'), {day}, {hour}, {minute}, {absolute}, CAST({reason} AS CHAR), {updated}
      FROM {qident(source)} c;
    """
    run_mysql(target, sql)
    insert_result(result, "world_clock", "materialized", count_rows(target, "mmo_server_world_clock_read_model"))


def materialize_waynet(target: Target, schema: Schema, result: dict[str, Any]) -> None:
    if schema.has_table("runtime_waypoints"):
        table = "runtime_waypoints"
        realm = schema.expr("w", table, ["realm_key", "realm"], "'default'")
        world = schema.expr("w", table, ["world_name", "world_key"], "'UNKNOWN'")
        key = schema.expr("w", table, ["waypoint_key", "key"], "CAST(w.id AS CHAR)" if schema.has_col(table, "id") else "'UNKNOWN_WAYPOINT'")
        name = schema.expr("w", table, ["waypoint_name", "name"], "NULL")
        kind = schema.expr("w", table, ["kind_key", "kind"], "NULL")
        px = schema.expr("w", table, ["pos_x", "x"], "NULL")
        py = schema.expr("w", table, ["pos_y", "y"], "NULL")
        pz = schema.expr("w", table, ["pos_z", "z"], "NULL")
        sql = f"""
        DELETE FROM mmo_server_waypoint_read_model;
        INSERT INTO mmo_server_waypoint_read_model
          (realm_key, world_name, waypoint_key, waypoint_name, kind_key, pos_x, pos_y, pos_z)
        SELECT COALESCE(CAST({realm} AS CHAR),'default'), COALESCE(CAST({world} AS CHAR),'UNKNOWN'), COALESCE(CAST({key} AS CHAR), UUID()), CAST({name} AS CHAR), CAST({kind} AS CHAR), {px}, {py}, {pz}
          FROM runtime_waypoints w;
        """
        run_mysql(target, sql)
        insert_result(result, "waypoints", "materialized", count_rows(target, "mmo_server_waypoint_read_model"))
    else:
        insert_result(result, "waypoints", "skipped", reason="source table runtime_waypoints missing in MySQL; SQLite still has this context")

    if schema.has_table("runtime_waypoint_edges"):
        table = "runtime_waypoint_edges"
        realm = schema.expr("e", table, ["realm_key", "realm"], "'default'")
        world = schema.expr("e", table, ["world_name", "world_key"], "'UNKNOWN'")
        key = schema.expr("e", table, ["edge_key", "key"], "CAST(e.id AS CHAR)" if schema.has_col(table, "id") else "UUID()")
        frm = schema.expr("e", table, ["from_waypoint_key", "from_key"], "''")
        to = schema.expr("e", table, ["to_waypoint_key", "to_key"], "''")
        dist = schema.expr("e", table, ["distance_value", "distance", "len"], "NULL")
        sql = f"""
        DELETE FROM mmo_server_waypoint_edge_read_model;
        INSERT INTO mmo_server_waypoint_edge_read_model
          (realm_key, world_name, edge_key, from_waypoint_key, to_waypoint_key, distance_value)
        SELECT COALESCE(CAST({realm} AS CHAR),'default'), COALESCE(CAST({world} AS CHAR),'UNKNOWN'), COALESCE(CAST({key} AS CHAR), UUID()), COALESCE(CAST({frm} AS CHAR),''), COALESCE(CAST({to} AS CHAR),''), {dist}
          FROM runtime_waypoint_edges e;
        """
        run_mysql(target, sql)
        insert_result(result, "waypoint_edges", "materialized", count_rows(target, "mmo_server_waypoint_edge_read_model"))
    else:
        insert_result(result, "waypoint_edges", "skipped", reason="source table runtime_waypoint_edges missing in MySQL; SQLite still has this context")


NUMERIC_SQL_TYPES = {"tinyint", "smallint", "mediumint", "int", "integer", "bigint", "decimal", "numeric"}


def source_event_max_sequence(schema: Schema) -> tuple[str | None, str | None]:
    """Return a safe MAX(...) expression for world_event_journal metadata.

    Older Step53 code assumed a SQLite-style integer `id` column. The MySQL
    bridge schema commonly uses UUID/binary event identifiers instead, so blindly
    querying MAX(id) prints a noisy ERROR 1054 even though materialization can
    otherwise pass. Keep this metadata optional and numeric-only.
    """
    table = "world_event_journal"
    if not schema.has_table(table):
        return None, None
    columns = schema.columns(table)
    for column in ("id", "event_sequence", "event_seq", "journal_sequence", "journal_seq", "sequence_no", "seq"):
        info = columns.get(column)
        if info is not None and info.data_type in NUMERIC_SQL_TYPES:
            return f"COALESCE(MAX({qident(column)}),0)", column
    return None, None


def refresh_meta(target: Target, schema: Schema, result: dict[str, Any]) -> None:
    event_count = 0
    max_id = "NULL"
    max_column: str | None = None
    if schema.has_table("world_event_journal"):
        try:
            event_count = count_rows(target, "world_event_journal")
            max_expr, max_column = source_event_max_sequence(schema)
            if max_expr is not None:
                out = run_mysql(target, f"SELECT {max_expr} FROM world_event_journal;").strip()
                max_id = out.splitlines()[-1] if out else "0"
        except Exception:
            event_count = 0
            max_id = "NULL"
            max_column = None
    notes = "Step53 typed physical read model; no JSON columns; not final production DB yet"
    run_mysql(
        target,
        f"""
        REPLACE INTO mmo_server_read_model_meta
          (model_key, model_version, source_database, rebuilt_at, source_event_count, source_max_event_id, notes)
        VALUES
          ('server_read_model_v1', 1, DATABASE(), CURRENT_TIMESTAMP(6), {int(event_count)}, {max_id}, {sql_literal(notes)});
        """,
    )
    result["meta"] = {
        "source_event_count": event_count,
        "source_max_event_id": None if max_id == "NULL" else max_id,
        "source_max_event_column": max_column,
    }


def inspect_read_model_tables(target: Target) -> dict[str, int]:
    counts: dict[str, int] = {}
    for table in READ_MODEL_TABLES:
        try:
            counts[table] = count_rows(target, table)
        except Exception:
            counts[table] = -1
    return counts


def main() -> int:
    parser = argparse.ArgumentParser(description="Materialize Step53 typed MMO server read-model tables.")
    parser.add_argument("--url", required=True, help="mysql://user:password@host:port/database")
    parser.add_argument("--sql", default=str(DEFAULT_SQL), help="SQL create-table patch path")
    parser.add_argument("--output", help="Optional JSON report path")
    parser.add_argument("--create-only", action="store_true", help="Only install read-model tables, do not rebuild rows")
    args = parser.parse_args()

    target = parse_mysql_url(args.url)
    sql_path = Path(args.sql)
    result: dict[str, Any] = {
        "step": 53,
        "database": target.database,
        "status": "started",
        "sql": str(sql_path),
        "materializers": [],
        "read_model_tables": READ_MODEL_TABLES,
    }

    execute_sql_file(target, sql_path)
    if args.create_only:
        result["status"] = "created"
    else:
        schema = Schema(target)
        materialize_characters(target, schema, result)
        materialize_character_inventory(target, schema, result)
        materialize_character_quests(target, schema, result)
        materialize_known_dialogs(target, schema, result)
        materialize_world_entities(target, schema, result)
        materialize_world_inventory(target, schema, result)
        materialize_script_ints(target, schema, result)
        materialize_clock(target, schema, result)
        materialize_waynet(target, schema, result)
        refresh_meta(target, schema, result)
        result["counts"] = inspect_read_model_tables(target)
        materialized = [m for m in result["materializers"] if m["status"] == "materialized"]
        result["status"] = "materialized" if materialized else "created_no_sources"

    if args.output:
        out = Path(args.output)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print(f"artifact={out}")

    print("Step53 server read-model materialization")
    print(f"database={target.database}")
    for item in result.get("materializers", []):
        extra = f" rows={item['rows']}" if item.get("rows") else ""
        reason = f" reason={item['reason']}" if item.get("reason") else ""
        print(f"  {item['name']}: {item['status']}{extra}{reason}")
    print("status=" + result["status"])
    return 0 if result["status"] in {"created", "materialized", "created_no_sources"} else 1


if __name__ == "__main__":
    raise SystemExit(main())

