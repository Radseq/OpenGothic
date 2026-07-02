#!/usr/bin/env python3
"""Dev MMO semantic action receiver.

Receives OpenGothic semantic action envelopes over UDP, validates them,
de-duplicates idempotency keys, writes accepted actions to JSONL, and can
optionally enqueue the accepted envelopes into the MySQL mmo_server_action_outbox.

This is a server-boundary/dev bridge, not final networking and not direct client
DB access. The game thread only snapshots/enqueues; this separate process owns
MySQL interaction when --enqueue-outbox is enabled.

Step37 extends the accepted shape for script/progression actions used by the
bookstand/bookshelf one-shot XP slice: set_script_int, adjust_progression,
apply_experience_reward, update_quest and set_known_dialog. Step38 adds
trade/combat/resource/lifecycle actions. Step39 adds character_checkpoint for bounded movement/checkpoint evidence.
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import signal
import socket
import subprocess
import sys
import time
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Any
from urllib.parse import unquote, urlparse

REQUIRED_FIELDS = (
    "version",
    "action_kind",
    "event_type",
    "event_class",
    "procedure",
    "local_sequence",
    "client_tick",
    "target_key",
    "idempotency_key",
    "payload",
)

DB_BRIDGE_VERSION = 2


@dataclass
class Stats:
    received: int = 0
    accepted: int = 0
    duplicate: int = 0
    invalid: int = 0
    rejected: int = 0
    enqueued: int = 0
    enqueue_failed: int = 0
    bytes_received: int = 0


@dataclass(frozen=True)
class MySqlTarget:
    host: str
    port: int
    user: str
    password: str
    database: str


@dataclass(frozen=True)
class EnqueueResult:
    action_uuid: str
    status: str


def parse_bind(value: str) -> tuple[str, int]:
    if ":" not in value:
        raise argparse.ArgumentTypeError("expected host:port")
    host, port_text = value.rsplit(":", 1)
    if not host:
        raise argparse.ArgumentTypeError("missing host")
    try:
        port = int(port_text)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("invalid port") from exc
    if port <= 0 or port > 65535:
        raise argparse.ArgumentTypeError("port out of range")
    return host, port


def parse_mysql_url(url: str) -> MySqlTarget:
    parsed = urlparse(url)
    if parsed.scheme not in {"mysql", "mysql+pymysql"}:
        raise ValueError("use mysql://user:password@host:port/database")
    database = parsed.path.lstrip("/")
    if not database:
        raise ValueError("database name is missing in MySQL URL")
    return MySqlTarget(
        host=parsed.hostname or "localhost",
        port=int(parsed.port or 3306),
        user=unquote(parsed.username or "root"),
        password=unquote(parsed.password or ""),
        database=database,
    )


def mysql_cmd(target: MySqlTarget) -> list[str]:
    exe = shutil.which("mysql")
    if exe is None:
        raise RuntimeError("mysql executable was not found in PATH")
    cmd = [
        exe,
        f"--host={target.host}",
        f"--port={target.port}",
        f"--user={target.user}",
        "--default-character-set=utf8mb4", "--init-command=SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci",
        "--batch",
        "--raw",
        "--skip-column-names",
    ]
    if target.password:
        cmd.append(f"--password={target.password}")
    cmd.append(target.database)
    return cmd


def run_mysql(target: MySqlTarget, sql: str) -> str:
    proc = subprocess.run(
        mysql_cmd(target) + ["--execute", sql],
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if proc.returncode != 0:
        if proc.stderr:
            print(proc.stderr, file=sys.stderr, end="")
        raise RuntimeError(f"mysql exited with status {proc.returncode}")
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


def json_sql(value: Any) -> str:
    return f"CAST({sql_literal(json.dumps(value, ensure_ascii=False, separators=(',', ':')))} AS JSON)"


def scalar(target: MySqlTarget, sql: str) -> str:
    raw = run_mysql(target, sql)
    return raw.splitlines()[-1].strip() if raw else ""


def db_login(target: MySqlTarget, account_name: str, character_key: str, session_key: str, remote_addr: str) -> str:
    value = scalar(
        target,
        f"""
        SET @session_id = NULL;
        CALL mmo_login_character(
          {sql_literal(account_name)},
          {sql_literal(character_key)},
          {sql_literal(session_key)},
          'dev-action-receiver',
          {sql_literal(remote_addr)},
          JSON_OBJECT('tool','run_mmo_action_receiver','db_bridge_version',{DB_BRIDGE_VERSION}),
          @session_id
        );
        SELECT BIN_TO_UUID(@session_id, 1);
        """,
    )
    if not value or value.upper() == "NULL":
        raise RuntimeError("mmo_login_character returned no session id")
    return value


def validate_action(obj: Any, require_session: str | None) -> str | None:
    if not isinstance(obj, dict):
        return "not an object"
    for field in REQUIRED_FIELDS:
        if field not in obj:
            return f"missing field: {field}"
    if obj.get("version") != 1:
        return "unsupported version"
    if not isinstance(obj.get("payload"), dict):
        return "payload is not an object"
    if not str(obj.get("idempotency_key") or ""):
        return "empty idempotency_key"
    if not str(obj.get("action_kind") or ""):
        return "empty action_kind"
    if not str(obj.get("procedure") or ""):
        return "empty procedure"
    if require_session and not str(obj.get("idempotency_key", "")).startswith(require_session + ":"):
        return "session key mismatch"
    return None


def equipment_slot_name(value: Any) -> str | None:
    """Map current OpenGothic numeric equipment slots to DB slot names.

    The current engine hook exposes Inventory::slotId(): 1=melee, 2=ranged,
    3.. are quick/numeric slots. The MySQL first equipment slice has explicit
    semantic slots. Unknown is accepted by the DB but should stay visible.
    """
    try:
        slot = int(value)
    except (TypeError, ValueError):
        return None
    if slot == 1:
        return "weapon_melee"
    if slot == 2:
        return "weapon_ranged"
    if slot == 255:
        return "unknown"
    return "unknown"


def required_shape_for_dispatch(action_kind: str, normalized_payload: dict[str, Any]) -> tuple[bool, list[str]]:
    """Cheap diagnostic only; DB outbox can store resolver-needed payloads.

    v1 marked many actions dispatch_ready=false because the game envelope had
    engine keys rather than MySQL UUIDs. v2 separates two states:
    - dispatch_ready: direct stored-procedure payload exists now;
    - resolver_ready: enough engine identity exists for the DB worker to resolve.
    """
    required_by_kind: dict[str, tuple[str, ...]] = {
        "pickup_world_item": ("world_item_entity_key", "server_tick"),
        "remove_world_item": ("world_item_entity_key", "server_tick"),
        "equip_character_item": ("equipment_slot", "item_symbol", "server_tick"),
        "unequip_character_item": ("equipment_slot", "server_tick"),
        "transfer_character_item": ("item_symbol", "server_tick"),
        # Step37 script/progression slice. These are resolver-ready when the
        # envelope carries stable script/progression keys. Direct DB UUIDs are
        # intentionally not required on the client side.
        "set_script_int": ("script_key", "value_after", "server_tick"),
        "adjust_progression": ("experience_delta", "learning_points_delta", "server_tick"),
        "apply_experience_reward": ("experience_delta", "server_tick"),
        "update_quest": ("quest_key", "status", "server_tick"),
        "set_known_dialog": ("npc_key", "info_key", "known", "server_tick"),
        "trade_buy_from_npc": ("npc_key", "item_symbol", "amount", "server_tick"),
        "trade_sell_to_npc": ("npc_key", "item_symbol", "amount", "server_tick"),
        "consume_mana": ("mana_amount", "server_tick"),
        "consume_item": ("item_symbol", "amount", "server_tick"),
        "apply_character_damage": ("target_character_key", "damage_amount", "server_tick"),
        "apply_world_entity_damage": ("target_key", "damage_amount", "server_tick"),
        "mark_npc_dead": ("target_key", "server_tick"),
        "trigger_event": ("trigger_key", "event_type_name", "server_tick"),
        "mover_state_changed": ("mover_key", "state_after", "server_tick"),
        "ready_weapon": ("actor_key", "new_weapon_state", "server_tick"),
        "holster_weapon": ("actor_key", "new_weapon_state", "server_tick"),
        "character_resource_delta": ("resource_key", "delta_amount", "server_tick"),
        "world_time_changed": ("world_time_after_ms", "server_tick"),
        "spend_learning_points": ("stat_key", "learning_points_cost", "server_tick"),
        "character_teleport": ("pos_x", "pos_y", "pos_z", "server_tick"),
        "world_transition": ("pos_x", "pos_y", "pos_z", "server_tick"),
        "respawn_world_item": ("respawn_policy_key", "world_item_entity_key", "server_tick"),
        "respawn_container_item": ("respawn_policy_key", "owner_entity_key", "server_tick"),
        "npc_reaction_started": ("actor_npc_key", "reaction_kind", "server_tick"),
        "npc_dialog_initiated": ("actor_npc_key", "target_character_key", "server_tick"),
        "client_bootstrap_request": ("character_key", "world", "server_tick"),
        "character_checkpoint": ("pos_x", "pos_y", "pos_z", "rotation_yaw", "server_tick"),
    }
    required = required_by_kind.get(action_kind, ("server_tick",))
    missing = [key for key in required if normalized_payload.get(key) in (None, "")]
    return len(missing) == 0, missing


def normalized_db_payload(obj: dict[str, Any], remote: tuple[str, int]) -> dict[str, Any]:
    payload = obj.get("payload") if isinstance(obj.get("payload"), dict) else {}
    action_kind = str(obj.get("action_kind", ""))
    server_tick = int(obj.get("client_tick") or payload.get("client_tick") or 0)

    normalized: dict[str, Any] = {
        "server_tick": server_tick,
        "client_tick": int(obj.get("client_tick") or 0),
        "client_local_sequence": int(obj.get("local_sequence") or 0),
        "client_idempotency_key": str(obj.get("idempotency_key") or ""),
        "client_target_key": str(obj.get("target_key") or ""),
        "client_action_kind": action_kind,
        "client_event_type": str(obj.get("event_type") or ""),
        "client_event_class": str(obj.get("event_class") or ""),
        "client_procedure": str(obj.get("procedure") or ""),
        "client_payload": payload,
        "metadata": {
            "source": "run_mmo_action_receiver",
            "transport": "udp-jsonl",
            "remote": f"{remote[0]}:{remote[1]}",
            "db_bridge_version": DB_BRIDGE_VERSION,
            "received_unix_ms": int(time.time() * 1000),
        },
    }

    # Best-effort aliases used by the resolver/dispatcher. We preserve the raw
    # client envelope and add both direct DB fields and engine identity fields.
    normalized["actor_key"] = payload.get("actor_key")
    normalized["world"] = payload.get("world")
    normalized["item_symbol"] = payload.get("item_symbol") or payload.get("inventory_item_symbol")
    normalized["item_template_key"] = payload.get("item_template_key")
    normalized["item_persistent_id"] = payload.get("item_persistent_id") or payload.get("source_world_item_persistent_id")
    normalized["amount"] = payload.get("amount", 1)

    if action_kind == "client_bootstrap_request":
        normalized["character_key"] = payload.get("character_key") or "PC_HERO"
        normalized["server_endpoint"] = payload.get("server_endpoint")
        normalized["server_bound_client_mode"] = payload.get("server_bound_client_mode", True)
        normalized["reason"] = payload.get("reason", "client_bootstrap_request")
    elif action_kind in {"pickup_world_item", "remove_world_item"}:
        normalized["world_item_entity_key"] = payload.get("target_key") or obj.get("target_key")
        normalized["engine_world_item_key"] = payload.get("target_key") or obj.get("target_key")
        normalized["source_world_item_persistent_id"] = payload.get("source_world_item_persistent_id")
        normalized["bag_index"] = payload.get("bag_index")
        normalized["reason"] = payload.get("reason", "semantic_action")
    elif action_kind == "equip_character_item":
        normalized["item_instance_id"] = payload.get("item_instance_id")
        normalized["equipment_slot"] = equipment_slot_name(payload.get("slot"))
        normalized["engine_equipment_slot"] = payload.get("slot")
    elif action_kind == "unequip_character_item":
        normalized["equipment_slot"] = equipment_slot_name(payload.get("slot"))
        normalized["engine_equipment_slot"] = payload.get("slot")
        normalized["target_bag_index"] = payload.get("target_bag_index")
    elif action_kind == "transfer_character_item":
        normalized["target_character_key"] = payload.get("target_character_key")
        normalized["item_instance_id"] = payload.get("item_instance_id")
        normalized["target_bag_index"] = payload.get("target_bag_index")
    elif action_kind == "set_script_int":
        normalized["script_key"] = payload.get("script_key") or payload.get("global_key") or payload.get("symbol_name") or obj.get("target_key")
        normalized["symbol_index"] = payload.get("symbol_index")
        normalized["value_index"] = payload.get("value_index", 0)
        normalized["value_before"] = payload.get("value_before")
        normalized["value_after"] = payload.get("value_after", payload.get("value"))
        normalized["reason"] = payload.get("reason", "script_int_changed")
    elif action_kind in {"adjust_progression", "apply_experience_reward"}:
        normalized["experience_delta"] = payload.get("experience_delta", payload.get("xp_delta", payload.get("delta", 0)))
        normalized["learning_points_delta"] = payload.get("learning_points_delta", payload.get("lp_delta", 0))
        normalized["reason"] = payload.get("reason", "script_progression")
    elif action_kind == "update_quest":
        normalized["quest_key"] = payload.get("quest_key") or payload.get("topic") or obj.get("target_key")
        normalized["quest_name"] = payload.get("quest_name") or payload.get("name")
        normalized["status"] = payload.get("status", "running")
        normalized["entry_count"] = payload.get("entry_count", 0)
        normalized["entries"] = payload.get("entries", [])
    elif action_kind == "set_known_dialog":
        normalized["npc_key"] = payload.get("npc_key") or payload.get("npc_symbol_name")
        normalized["info_key"] = payload.get("info_key") or payload.get("info_symbol_name") or obj.get("target_key")
        normalized["known"] = payload.get("known", True)
        normalized["removed"] = payload.get("removed", False)
        normalized["reason"] = payload.get("reason", "script_dialog_known")
    elif action_kind in {"trade_buy_from_npc", "trade_sell_to_npc"}:
        normalized["npc_key"] = payload.get("npc_entity_key") or payload.get("npc_key") or payload.get("target_npc_entity_key")
        normalized["npc_symbol"] = payload.get("npc_symbol")
        normalized["npc_persistent_id"] = payload.get("npc_persistent_id")
        normalized["item_persistent_id"] = payload.get("item_persistent_id") or payload.get("seller_item_persistent_id") or payload.get("vendor_item_persistent_id")
        normalized["unit_price"] = payload.get("unit_price", 0)
        normalized["price_total"] = payload.get("price_total", 0)
        normalized["currency_key"] = payload.get("currency_key", "g2notr:gold")
        normalized["wallet_before"] = payload.get("wallet_before")
        normalized["wallet_after"] = payload.get("wallet_after")
        normalized["reason"] = payload.get("reason", action_kind)
    elif action_kind == "consume_mana":
        normalized["mana_amount"] = payload.get("mana_amount") or payload.get("amount") or payload.get("delta")
        normalized["resource_key"] = payload.get("resource_key", "mana")
        normalized["reason"] = payload.get("reason", "resource_delta")
    elif action_kind == "consume_item":
        normalized["item_persistent_id"] = payload.get("item_persistent_id") or payload.get("source_item_persistent_id")
        normalized["reason"] = payload.get("reason", "consume_item")
    elif action_kind == "apply_character_damage":
        normalized["target_character_key"] = payload.get("target_character_key") or payload.get("character_key") or "PC_HERO"
        normalized["damage_amount"] = payload.get("damage_amount") or payload.get("amount") or payload.get("delta")
        normalized["source_actor_key"] = payload.get("source_actor_key")
        normalized["reason"] = payload.get("reason", "character_damage")
    elif action_kind == "apply_world_entity_damage":
        normalized["target_key"] = payload.get("target_key") or payload.get("target_npc_entity_key") or obj.get("target_key")
        normalized["target_npc_entity_key"] = payload.get("target_npc_entity_key") or payload.get("target_key") or obj.get("target_key")
        normalized["damage_amount"] = payload.get("damage_amount") or payload.get("amount") or payload.get("delta")
        normalized["fatal"] = payload.get("fatal", False)
        normalized["reason"] = payload.get("reason", "world_entity_damage")
    elif action_kind == "mark_npc_dead":
        normalized["target_key"] = payload.get("target_key") or payload.get("target_npc_entity_key") or obj.get("target_key")
        normalized["target_npc_entity_key"] = payload.get("target_npc_entity_key") or payload.get("target_key") or obj.get("target_key")
        normalized["dead"] = payload.get("dead", True)
        normalized["reason"] = payload.get("reason", "npc_no_health")
    elif action_kind == "trigger_event":
        normalized["trigger_key"] = payload.get("trigger_key") or payload.get("target_key") or obj.get("target_key")
        normalized["trigger_vob_id"] = payload.get("trigger_vob_id")
        normalized["trigger_name"] = payload.get("trigger_name")
        normalized["trigger_target"] = payload.get("trigger_target")
        normalized["event_target"] = payload.get("event_target")
        normalized["event_emitter"] = payload.get("event_emitter")
        normalized["event_type_name"] = payload.get("event_type_name") or payload.get("event_type") or "trigger"
        normalized["capture_cause"] = payload.get("capture_cause")
        normalized["player_caused"] = payload.get("player_caused")
        normalized["reason"] = payload.get("reason", "world_trigger_event")
    elif action_kind == "mover_state_changed":
        normalized["mover_key"] = payload.get("mover_key") or payload.get("target_key") or obj.get("target_key")
        normalized["mover_vob_id"] = payload.get("mover_vob_id")
        normalized["mover_name"] = payload.get("mover_name")
        normalized["state_before"] = payload.get("state_before")
        normalized["state_after"] = payload.get("state_after")
        normalized["state_before_name"] = payload.get("state_before_name")
        normalized["state_after_name"] = payload.get("state_after_name")
        normalized["frame"] = payload.get("frame")
        normalized["target_frame"] = payload.get("target_frame")
        normalized["capture_cause"] = payload.get("capture_cause")
        normalized["player_caused"] = payload.get("player_caused")
        normalized["reason"] = payload.get("reason", "mover_state_changed")
    elif action_kind in {"ready_weapon", "holster_weapon"}:
        normalized["actor_key"] = payload.get("actor_key") or payload.get("actor_entity_key") or obj.get("target_key")
        normalized["actor_entity_key"] = payload.get("actor_entity_key")
        normalized["previous_weapon_state"] = payload.get("previous_weapon_state")
        normalized["new_weapon_state"] = payload.get("new_weapon_state") or payload.get("weapon_state") or action_kind
        normalized["ready"] = payload.get("ready", action_kind == "ready_weapon")
        normalized["reason"] = payload.get("reason", action_kind)
    elif action_kind == "character_resource_delta":
        normalized["target_character_key"] = payload.get("target_character_key") or payload.get("character_key") or "PC_HERO"
        normalized["resource_key"] = payload.get("resource_key") or payload.get("stat_key")
        normalized["delta_amount"] = payload.get("delta_amount") or payload.get("amount") or payload.get("delta")
        normalized["value_before"] = payload.get("value_before")
        normalized["value_after"] = payload.get("value_after")
        normalized["reason"] = payload.get("reason", "character_resource_delta")
    elif action_kind == "world_time_changed":
        normalized["world_time_before_ms"] = payload.get("world_time_before_ms")
        normalized["world_time_after_ms"] = payload.get("world_time_after_ms") or payload.get("time_after_ms")
        normalized["world_day_before"] = payload.get("world_day_before")
        normalized["world_day_after"] = payload.get("world_day_after")
        normalized["time_delta_ms"] = payload.get("time_delta_ms")
        normalized["reason"] = payload.get("reason", "world_time_changed")
    elif action_kind == "spend_learning_points":
        normalized["stat_key"] = payload.get("stat_key") or payload.get("talent_key") or payload.get("attribute_key") or obj.get("target_key")
        normalized["learning_points_cost"] = payload.get("learning_points_cost") or payload.get("lp_cost") or payload.get("learning_points_delta")
        normalized["value_before"] = payload.get("value_before")
        normalized["value_after"] = payload.get("value_after")
        normalized["gold_cost"] = payload.get("gold_cost", 0)
        normalized["trainer_key"] = payload.get("trainer_key") or payload.get("npc_key")
        normalized["reason"] = payload.get("reason", "trainer_learning_points_spent")
    elif action_kind in {"character_teleport", "world_transition"}:
        pos = payload.get("position") if isinstance(payload.get("position"), dict) else {}
        normalized["target_world_instance_key"] = payload.get("target_world_instance_key") or payload.get("world_instance_key")
        normalized["pos_x"] = payload.get("pos_x", pos.get("x"))
        normalized["pos_y"] = payload.get("pos_y", pos.get("y"))
        normalized["pos_z"] = payload.get("pos_z", pos.get("z"))
        normalized["rotation_yaw"] = payload.get("rotation_yaw", payload.get("yaw", 0))
        normalized["reason"] = payload.get("reason", action_kind)
    elif action_kind == "respawn_world_item":
        normalized["respawn_policy_key"] = payload.get("respawn_policy_key") or payload.get("policy_key") or obj.get("target_key")
        normalized["world_item_entity_key"] = payload.get("world_item_entity_key") or payload.get("target_key")
        normalized["amount"] = payload.get("amount", 1)
        normalized["reason"] = payload.get("reason", "scheduled_world_item_respawn")
    elif action_kind == "respawn_container_item":
        normalized["respawn_policy_key"] = payload.get("respawn_policy_key") or payload.get("policy_key") or obj.get("target_key")
        normalized["owner_entity_key"] = payload.get("owner_entity_key") or payload.get("container_key")
        normalized["item_instance_key"] = payload.get("item_instance_key")
        normalized["amount"] = payload.get("amount", 1)
        normalized["reason"] = payload.get("reason", "scheduled_container_item_respawn")
    elif action_kind == "npc_reaction_started":
        normalized["actor_npc_key"] = payload.get("actor_npc_key") or payload.get("actor_key") or obj.get("target_key")
        normalized["target_key"] = payload.get("target_key") or payload.get("target_character_key")
        normalized["reaction_kind"] = payload.get("reaction_kind") or payload.get("reason") or "attention"
        normalized["reason"] = payload.get("reason", "npc_reaction_started")
    elif action_kind == "npc_dialog_initiated":
        normalized["actor_npc_key"] = payload.get("actor_npc_key") or payload.get("actor_key") or obj.get("target_key")
        normalized["target_character_key"] = payload.get("target_character_key") or payload.get("character_key") or "PC_HERO"
        normalized["dialog_info_key"] = payload.get("dialog_info_key") or payload.get("info_key")
        normalized["reason"] = payload.get("reason", "npc_dialog_initiated")
    elif action_kind == "character_checkpoint":
        pos = payload.get("position") if isinstance(payload.get("position"), dict) else {}
        normalized["character_key"] = payload.get("character_key", "PC_HERO")
        normalized["pos_x"] = payload.get("pos_x", pos.get("x"))
        normalized["pos_y"] = payload.get("pos_y", pos.get("y"))
        normalized["pos_z"] = payload.get("pos_z", pos.get("z"))
        normalized["rotation_yaw"] = payload.get("rotation_yaw", payload.get("yaw", 0))
        normalized["current_waypoint_key"] = payload.get("current_waypoint_key") or payload.get("waypoint_key")
        normalized["level"] = payload.get("level")
        normalized["experience"] = payload.get("experience")
        normalized["experience_next"] = payload.get("experience_next")
        normalized["learning_points"] = payload.get("learning_points")
        normalized["health_current"] = payload.get("health_current")
        normalized["health_max"] = payload.get("health_max")
        normalized["mana_current"] = payload.get("mana_current")
        normalized["mana_max"] = payload.get("mana_max")
        normalized["strength"] = payload.get("strength")
        normalized["dexterity"] = payload.get("dexterity")
        normalized["guild"] = payload.get("guild")
        normalized["true_guild"] = payload.get("true_guild")
        normalized["permanent_attitude"] = payload.get("permanent_attitude")
        normalized["temporary_attitude"] = payload.get("temporary_attitude")
        normalized["reason"] = payload.get("reason", "character_checkpoint")
        normalized["checkpoint_interval_ms"] = payload.get("checkpoint_interval_ms")
        normalized["checkpoint_min_distance"] = payload.get("checkpoint_min_distance")
        normalized["checkpoint_min_yaw_deg"] = payload.get("checkpoint_min_yaw_deg")
        normalized["checkpoint_force_interval_ms"] = payload.get("checkpoint_force_interval_ms")

    resolver_ready, missing = required_shape_for_dispatch(action_kind, normalized)
    normalized["resolver_ready"] = resolver_ready
    normalized["resolver_missing_fields"] = missing

    direct_required_by_kind: dict[str, tuple[str, ...]] = {
        "pickup_world_item": ("world_item_entity_key", "server_tick"),
        "remove_world_item": ("world_item_entity_key", "server_tick"),
        "equip_character_item": ("item_instance_id", "equipment_slot", "server_tick"),
        "unequip_character_item": ("equipment_slot", "server_tick"),
        "set_script_int": ("script_key", "value_after", "server_tick"),
        "adjust_progression": ("experience_delta", "learning_points_delta", "server_tick"),
        "apply_experience_reward": ("experience_delta", "server_tick"),
        "update_quest": ("quest_key", "status", "server_tick"),
        "set_known_dialog": ("npc_key", "info_key", "server_tick"),
        "trade_buy_from_npc": ("npc_key", "item_symbol", "amount", "server_tick"),
        "trade_sell_to_npc": ("npc_key", "item_symbol", "amount", "server_tick"),
        "consume_mana": ("mana_amount", "server_tick"),
        "consume_item": ("item_symbol", "amount", "server_tick"),
        "apply_character_damage": ("target_character_key", "damage_amount", "server_tick"),
        "apply_world_entity_damage": ("target_key", "damage_amount", "server_tick"),
        "mark_npc_dead": ("target_key", "server_tick"),
        "trigger_event": ("trigger_key", "event_type_name", "server_tick"),
        "mover_state_changed": ("mover_key", "state_after", "server_tick"),
        "ready_weapon": ("actor_key", "new_weapon_state", "server_tick"),
        "holster_weapon": ("actor_key", "new_weapon_state", "server_tick"),
        "character_resource_delta": ("resource_key", "delta_amount", "server_tick"),
        "world_time_changed": ("world_time_after_ms", "server_tick"),
        "spend_learning_points": ("stat_key", "learning_points_cost", "server_tick"),
        "character_teleport": ("pos_x", "pos_y", "pos_z", "server_tick"),
        "world_transition": ("pos_x", "pos_y", "pos_z", "server_tick"),
        "respawn_world_item": ("respawn_policy_key", "world_item_entity_key", "server_tick"),
        "respawn_container_item": ("respawn_policy_key", "owner_entity_key", "server_tick"),
        "npc_reaction_started": ("actor_npc_key", "reaction_kind", "server_tick"),
        "npc_dialog_initiated": ("actor_npc_key", "target_character_key", "server_tick"),
        "client_bootstrap_request": ("character_key", "world", "server_tick"),
        "character_checkpoint": ("pos_x", "pos_y", "pos_z", "rotation_yaw", "server_tick"),
    }
    direct_missing = [key for key in direct_required_by_kind.get(action_kind, ("server_tick",)) if normalized.get(key) in (None, "")]
    normalized["dispatch_ready"] = len(direct_missing) == 0
    normalized["dispatch_missing_fields"] = direct_missing
    return normalized


def enqueue_outbox(
    target: MySqlTarget,
    session_uuid: str,
    obj: dict[str, Any],
    db_payload: dict[str, Any],
    priority: int,
    max_attempts: int,
) -> EnqueueResult:
    raw = scalar(
        target,
        f"""
        SET @action_id = NULL;
        SET @status = NULL;
        CALL mmo_enqueue_server_action(
          UUID_TO_BIN({sql_literal(session_uuid)}, 1),
          {sql_literal(obj.get('action_kind'))},
          {sql_literal(obj.get('target_key'))},
          {json_sql(db_payload)},
          {sql_literal(obj.get('idempotency_key'))},
          {int(priority)},
          {int(max_attempts)},
          @action_id,
          @status
        );
        SELECT CONCAT(BIN_TO_UUID(@action_id, 1), '\t', @status);
        """,
    )
    parts = raw.split("\t")
    if len(parts) != 2 or not parts[0] or parts[0].upper() == "NULL":
        raise RuntimeError(f"invalid enqueue result: {raw!r}")
    return EnqueueResult(action_uuid=parts[0], status=parts[1])


def append_jsonl(path: Path, obj: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    # Open per write on purpose: if a tester deletes the file after receiver
    # startup, the next accepted action recreates a visible file instead of
    # writing to an unlinked inode.
    with path.open("a", encoding="utf-8") as out:
        out.write(json.dumps(obj, ensure_ascii=False, separators=(",", ":")) + "\n")
        out.flush()


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="Receive MMO semantic action JSONL envelopes over UDP")
    ap.add_argument("--bind", type=parse_bind, default=parse_bind("127.0.0.1:29777"), help="UDP bind endpoint, default 127.0.0.1:29777")
    ap.add_argument("--jsonl", default="runtime/mmo_server_actions.jsonl", help="raw accepted action JSONL output")
    ap.add_argument("--reject-jsonl", default="", help="optional rejected/invalid action JSONL output")
    ap.add_argument("--require-session", default="", help="optional required session key prefix")
    ap.add_argument("--max-packets", type=int, default=0, help="stop after N received packets; 0 means until Ctrl+C")
    ap.add_argument("--print-every", type=int, default=1, help="print progress every N accepted packets")
    ap.add_argument("--truncate", action="store_true", help="truncate output JSONL files on start")

    ap.add_argument("--mysql-url", default=os.environ.get("GOTHIC_MMO_MYSQL_URL", ""), help="optional mysql://user:password@host:port/database")
    ap.add_argument("--enqueue-outbox", action="store_true", help="enqueue accepted actions into mmo_server_action_outbox")
    ap.add_argument("--account-name", default="local-import", help="account used for dev DB login when enqueueing")
    ap.add_argument("--character-key", default="PC_HERO", help="character used for dev DB login when enqueueing")
    ap.add_argument("--db-session-key", default="", help="server_sessions.session_key for dev login; defaults to --require-session")
    ap.add_argument("--db-session-uuid", default="", help="existing server_sessions UUID; skips mmo_login_character")
    ap.add_argument("--outbox-priority", type=int, default=100)
    ap.add_argument("--outbox-max-attempts", type=int, default=5)
    ap.add_argument("--strict-dispatch-payload", action="store_true", help="reject actions whose current envelope lacks resolver identity for supported dispatch")
    args = ap.parse_args(argv)

    out_path = Path(args.jsonl)
    reject_path = Path(args.reject_jsonl) if args.reject_jsonl else None
    if args.truncate:
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text("", encoding="utf-8")
        if reject_path is not None:
            reject_path.parent.mkdir(parents=True, exist_ok=True)
            reject_path.write_text("", encoding="utf-8")

    target: MySqlTarget | None = None
    session_uuid = args.db_session_uuid.strip()
    if args.enqueue_outbox:
        if not args.mysql_url:
            raise SystemExit("--enqueue-outbox requires --mysql-url or GOTHIC_MMO_MYSQL_URL")
        target = parse_mysql_url(args.mysql_url)
        db_session_key = args.db_session_key.strip() or args.require_session.strip()
        if not session_uuid:
            if not db_session_key:
                raise SystemExit("--enqueue-outbox requires --db-session-key or --require-session")
            session_uuid = db_login(target, args.account_name, args.character_key, db_session_key, "udp-receiver")
        print(f"db_session={session_uuid} enqueue_outbox=on", flush=True)

    bind_host, bind_port = args.bind
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((bind_host, bind_port))
    sock.settimeout(0.5)

    running = True

    def stop(_signum: int, _frame: Any) -> None:
        nonlocal running
        running = False

    signal.signal(signal.SIGINT, stop)
    signal.signal(signal.SIGTERM, stop)

    stats = Stats()
    kinds: Counter[str] = Counter()
    statuses: Counter[str] = Counter()
    seen: set[str] = set()

    print(f"listening udp://{bind_host}:{bind_port} -> {out_path}", flush=True)
    try:
        while running:
            if args.max_packets > 0 and stats.received >= args.max_packets:
                break
            try:
                data, remote = sock.recvfrom(65535)
            except socket.timeout:
                continue
            stats.received += 1
            stats.bytes_received += len(data)

            raw_line = ""
            try:
                raw_line = data.decode("utf-8").strip()
                obj = json.loads(raw_line)
            except Exception as exc:  # diagnostics tool
                stats.invalid += 1
                diagnostic = {"error": "decode_or_json", "message": str(exc), "remote": f"{remote[0]}:{remote[1]}", "raw": raw_line[:1000]}
                if reject_path is not None:
                    append_jsonl(reject_path, diagnostic)
                print(f"[invalid] remote={remote} decode/json error: {exc}", flush=True)
                continue

            err = validate_action(obj, args.require_session or None)
            if err:
                stats.invalid += 1
                diagnostic = {"error": err, "remote": f"{remote[0]}:{remote[1]}", "action": obj}
                if reject_path is not None:
                    append_jsonl(reject_path, diagnostic)
                print(f"[invalid] remote={remote} {err}", flush=True)
                continue

            idem = str(obj["idempotency_key"])
            if idem in seen:
                stats.duplicate += 1
                continue
            seen.add(idem)

            db_payload = normalized_db_payload(obj, remote)
            if args.strict_dispatch_payload and not db_payload.get("resolver_ready", False):
                stats.rejected += 1
                diagnostic = {"error": "resolver_payload_not_ready", "missing": db_payload.get("resolver_missing_fields", []), "action": obj}
                if reject_path is not None:
                    append_jsonl(reject_path, diagnostic)
                print(f"[rejected] {obj.get('action_kind')} missing={db_payload.get('resolver_missing_fields', [])}", flush=True)
                continue

            action_kind = str(obj["action_kind"])
            kinds[action_kind] += 1
            append_jsonl(out_path, obj)
            stats.accepted += 1

            enqueue_text = ""
            if args.enqueue_outbox and target is not None:
                try:
                    result = enqueue_outbox(target, session_uuid, obj, db_payload, args.outbox_priority, args.outbox_max_attempts)
                    stats.enqueued += 1
                    statuses[result.status] += 1
                    enqueue_text = f" outbox={result.status}:{result.action_uuid}"
                except Exception as exc:
                    stats.enqueue_failed += 1
                    diagnostic = {"error": "enqueue_failed", "message": str(exc), "action": obj, "db_payload": db_payload}
                    if reject_path is not None:
                        append_jsonl(reject_path, diagnostic)
                    print(f"[enqueue_failed] {action_kind} error={exc}", file=sys.stderr, flush=True)

            if args.print_every > 0 and (stats.accepted % args.print_every) == 0:
                ready = "direct-ready" if db_payload.get("dispatch_ready") else ("resolver-ready" if db_payload.get("resolver_ready") else "not-resolver-ready")
                print(
                    f"accepted={stats.accepted} received={stats.received} invalid={stats.invalid} duplicate={stats.duplicate} rejected={stats.rejected} enqueued={stats.enqueued} failed={stats.enqueue_failed} last={action_kind} {ready}{enqueue_text}",
                    flush=True,
                )
    finally:
        sock.close()

    print("summary:", flush=True)
    print(f"received={stats.received}", flush=True)
    print(f"accepted={stats.accepted}", flush=True)
    print(f"invalid={stats.invalid}", flush=True)
    print(f"rejected={stats.rejected}", flush=True)
    print(f"duplicate={stats.duplicate}", flush=True)
    print(f"enqueued={stats.enqueued}", flush=True)
    print(f"enqueue_failed={stats.enqueue_failed}", flush=True)
    print(f"bytes={stats.bytes_received}", flush=True)
    for kind, count in sorted(kinds.items()):
        print(f"kind.{kind}={count}", flush=True)
    for status, count in sorted(statuses.items()):
        print(f"outbox.{status}={count}", flush=True)
    return 0 if stats.invalid == 0 and stats.rejected == 0 and stats.enqueue_failed == 0 else 2


if __name__ == "__main__":
    raise SystemExit(main())









