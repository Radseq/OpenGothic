#!/usr/bin/env python3
"""Step 36 dev vertical-slice evidence checker.

This checker is intentionally read-only. It does not mark global restore/parity
scenarios as passed. It verifies that one semantic action slice has durable
cross-layer evidence:

  JSONL/client evidence -> server outbox -> world_event_journal -> projections

Target slice:
  pickup_world_item + equip_character_item + unequip_character_item

The script is schema-tolerant enough for the current MySQL 8 dev migrations, but
it fails closed for missing DB evidence.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import sqlite3
import subprocess
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable
from urllib.parse import unquote, urlparse

REQUIRED_KINDS = ("pickup_world_item", "equip_character_item", "unequip_character_item")
EXPECTED_JOURNAL_TYPES = {
    "pickup_world_item": "world_item_picked_up",
    "equip_character_item": "character_item_equipped",
    "unequip_character_item": "character_item_unequipped",
}


@dataclass(frozen=True)
class MysqlTarget:
    host: str
    port: int
    user: str
    password: str
    database: str


def parse_mysql_url(url: str) -> MysqlTarget:
    parsed = urlparse(url)
    if parsed.scheme not in {"mysql", "mysql+pymysql"}:
        raise SystemExit(f"unsupported mysql url scheme: {parsed.scheme!r}")
    if not parsed.hostname or not parsed.username or not parsed.path.strip("/"):
        raise SystemExit("mysql url must include user, host and database")
    return MysqlTarget(
        host=parsed.hostname,
        port=parsed.port or 3306,
        user=unquote(parsed.username),
        password=unquote(parsed.password or ""),
        database=parsed.path.strip("/"),
    )


def sql_quote(value: str) -> str:
    return "'" + value.replace("\\", "\\\\").replace("'", "''") + "'"


class Mysql:
    def __init__(self, target: MysqlTarget) -> None:
        self.target = target
        self._columns: dict[str, set[str]] = {}
        self._tables: dict[str, bool] = {}

    def run(self, sql: str, *, allow_error: bool = False) -> list[list[str]]:
        cmd = [
            "mysql",
            "--batch",
            "--raw",
            "--skip-column-names",
            "-h",
            self.target.host,
            "-P",
            str(self.target.port),
            "-u",
            self.target.user,
        ]
        if self.target.password:
            cmd.append(f"-p{self.target.password}")
        cmd.extend([self.target.database, "-e", sql])
        proc = subprocess.run(cmd, text=True, capture_output=True)
        if proc.returncode != 0:
            if allow_error:
                return []
            err = (proc.stderr or proc.stdout).strip()
            raise RuntimeError(f"mysql exited with status {proc.returncode}: {err}")
        out = proc.stdout
        rows: list[list[str]] = []
        for line in out.splitlines():
            # Do not strip tabs; mysql can output empty trailing columns.
            rows.append(line.rstrip("\n").split("\t"))
        return rows

    def table_exists(self, table: str) -> bool:
        if table in self._tables:
            return self._tables[table]
        rows = self.run(
            "SELECT COUNT(*) FROM INFORMATION_SCHEMA.TABLES "
            f"WHERE TABLE_SCHEMA={sql_quote(self.target.database)} AND TABLE_NAME={sql_quote(table)}",
            allow_error=True,
        )
        exists = bool(rows and rows[0] and rows[0][0] != "0")
        self._tables[table] = exists
        return exists

    def columns(self, table: str) -> set[str]:
        if table in self._columns:
            return self._columns[table]
        rows = self.run(
            "SELECT COLUMN_NAME FROM INFORMATION_SCHEMA.COLUMNS "
            f"WHERE TABLE_SCHEMA={sql_quote(self.target.database)} AND TABLE_NAME={sql_quote(table)}",
            allow_error=True,
        )
        cols = {r[0] for r in rows if r}
        self._columns[table] = cols
        return cols

    def has_columns(self, table: str, names: Iterable[str]) -> bool:
        cols = self.columns(table)
        return all(name in cols for name in names)


def action_fingerprint(kind: str, target_key: str) -> str:
    """Stable action identity used for JSONL<->outbox correlation.

    Idempotency keys intentionally include a dev session prefix and local sequence.
    Replayed JSONL can rewrite that prefix, while the gameplay identity remains the
    kind + target key. This fingerprint is not a production idempotency key; it is
    a Step36 evidence correlator only.
    """
    return f"{kind}|{target_key}"


def fingerprint_from_json_action(obj: dict[str, Any]) -> str:
    kind = str(obj.get("action_kind", ""))
    target = str(obj.get("target_key", ""))
    if not target and isinstance(obj.get("payload"), dict):
        payload = obj["payload"]
        target = str(payload.get("target_key") or payload.get("world_item_entity_key") or payload.get("item_key") or "")
    return action_fingerprint(kind, target)


def fingerprint_from_outbox(row: dict[str, Any]) -> str:
    return action_fingerprint(str(row.get("action_kind", "")), str(row.get("target_key", "")))


def read_jsonl(path: Path, session_key: str | None, expected_fingerprints: Counter[str] | None = None) -> dict[str, Any]:
    result: dict[str, Any] = {
        "path": str(path),
        "exists": path.exists(),
        "rows": 0,
        "matching_rows": 0,
        "kind_counts": {},
        "all_kind_counts": {},
        "fingerprint_counts": {},
        "fingerprint_kind_counts": {},
        "expected_fingerprint_matches": {},
        "expected_fingerprint_missing": {},
        "fingerprint_matching_rows": 0,
        "duplicate_idempotency_keys": [],
        "errors": [],
    }
    if not path.exists():
        return result
    counts: Counter[str] = Counter()
    all_counts: Counter[str] = Counter()
    fp_counts: Counter[str] = Counter()
    fp_kind_counts: Counter[str] = Counter()
    seen: set[str] = set()
    dupes: list[str] = []
    with path.open("r", encoding="utf-8") as f:
        for lineno, line in enumerate(f, start=1):
            line = line.strip()
            if not line:
                continue
            result["rows"] += 1
            try:
                obj = json.loads(line)
            except Exception as exc:  # noqa: BLE001
                result["errors"].append(f"line {lineno}: invalid json: {exc}")
                continue
            kind = str(obj.get("action_kind", ""))
            idem = str(obj.get("idempotency_key", ""))
            fp = fingerprint_from_json_action(obj)
            all_counts[kind] += 1
            if fp != "|":
                fp_counts[fp] += 1
            if expected_fingerprints and fp in expected_fingerprints:
                fp_kind_counts[kind] += 1
                result["fingerprint_matching_rows"] += 1
            if session_key and not idem.startswith(session_key + ":"):
                continue
            result["matching_rows"] += 1
            counts[kind] += 1
            if idem:
                if idem in seen:
                    dupes.append(idem)
                seen.add(idem)
    result["kind_counts"] = dict(sorted(counts.items()))
    result["all_kind_counts"] = dict(sorted(all_counts.items()))
    result["fingerprint_counts"] = dict(sorted(fp_counts.items()))
    result["fingerprint_kind_counts"] = dict(sorted(fp_kind_counts.items()))
    result["duplicate_idempotency_keys"] = dupes
    if expected_fingerprints:
        matches: dict[str, int] = {}
        missing: dict[str, int] = {}
        for fp, needed in expected_fingerprints.items():
            got = fp_counts.get(fp, 0)
            matches[fp] = min(got, needed)
            if got < needed:
                missing[fp] = needed - got
        result["expected_fingerprint_matches"] = dict(sorted(matches.items()))
        result["expected_fingerprint_missing"] = dict(sorted(missing.items()))
    return result



def _parse_json_maybe(value: Any) -> Any:
    if isinstance(value, str):
        try:
            return json.loads(value)
        except Exception:
            return value
    return value


def envelope_from_outbox_row(row: dict[str, Any]) -> tuple[dict[str, Any], str]:
    """Return a JSONL-like action envelope for evidence correlation.

    The receiver stores the original OpenGothic envelope inside request_payload in
    newer Step35 rows. When that preserved envelope is unavailable, this function
    synthesizes a minimal evidence envelope from outbox columns. The synthesized
    form is only for Step36 correlation/reporting, never for replay/dispatch.
    """
    request_payload = _parse_json_maybe(row.get("request_payload"))
    if isinstance(request_payload, dict):
        for key in (
            "client_payload",
            "client_envelope",
            "client_action",
            "envelope",
            "raw_action",
            "payload",
        ):
            candidate = _parse_json_maybe(request_payload.get(key))
            if isinstance(candidate, dict) and candidate.get("action_kind"):
                obj = dict(candidate)
                obj.setdefault("idempotency_key", row.get("idempotency_key") or "")
                obj.setdefault("action_kind", row.get("action_kind") or "")
                obj.setdefault("target_key", row.get("target_key") or "")
                return obj, f"outbox.request_payload.{key}"
        # Some receiver versions store aliases at top-level request_payload.
        if request_payload.get("action_kind") or request_payload.get("target_key"):
            obj = dict(request_payload)
            obj.setdefault("idempotency_key", row.get("idempotency_key") or "")
            obj.setdefault("action_kind", row.get("action_kind") or "")
            obj.setdefault("target_key", row.get("target_key") or "")
            return obj, "outbox.request_payload"
    obj = {
        "version": 1,
        "action_kind": row.get("action_kind") or "",
        "target_key": row.get("target_key") or "",
        "idempotency_key": row.get("idempotency_key") or "",
        "payload": {
            "source": "step36_synthetic_from_outbox",
            "event_uuid": row.get("event_uuid"),
        },
    }
    return obj, "outbox.synthetic_minimal"


def evidence_from_json_actions(
    *,
    name: str,
    actions: list[dict[str, Any]],
    session_key: str | None,
    expected_fingerprints: Counter[str] | None,
    path: str = "",
    source: str = "memory",
) -> dict[str, Any]:
    result: dict[str, Any] = {
        "path": path,
        "exists": True,
        "source": source,
        "rows": 0,
        "matching_rows": 0,
        "kind_counts": {},
        "all_kind_counts": {},
        "fingerprint_counts": {},
        "fingerprint_kind_counts": {},
        "expected_fingerprint_matches": {},
        "expected_fingerprint_missing": {},
        "fingerprint_matching_rows": 0,
        "duplicate_idempotency_keys": [],
        "errors": [],
    }
    counts: Counter[str] = Counter()
    all_counts: Counter[str] = Counter()
    fp_counts: Counter[str] = Counter()
    fp_kind_counts: Counter[str] = Counter()
    seen: set[str] = set()
    dupes: list[str] = []
    for idx, obj in enumerate(actions, start=1):
        if not isinstance(obj, dict):
            result["errors"].append(f"action {idx}: not an object")
            continue
        result["rows"] += 1
        kind = str(obj.get("action_kind", ""))
        idem = str(obj.get("idempotency_key", ""))
        fp = fingerprint_from_json_action(obj)
        all_counts[kind] += 1
        if fp != "|":
            fp_counts[fp] += 1
        if expected_fingerprints and fp in expected_fingerprints:
            fp_kind_counts[kind] += 1
            result["fingerprint_matching_rows"] += 1
        if session_key and not idem.startswith(session_key + ":"):
            continue
        result["matching_rows"] += 1
        counts[kind] += 1
        if idem:
            if idem in seen:
                dupes.append(idem)
            seen.add(idem)
    result["kind_counts"] = dict(sorted(counts.items()))
    result["all_kind_counts"] = dict(sorted(all_counts.items()))
    result["fingerprint_counts"] = dict(sorted(fp_counts.items()))
    result["fingerprint_kind_counts"] = dict(sorted(fp_kind_counts.items()))
    result["duplicate_idempotency_keys"] = dupes
    if expected_fingerprints:
        matches: dict[str, int] = {}
        missing: dict[str, int] = {}
        for fp, needed in expected_fingerprints.items():
            got = fp_counts.get(fp, 0)
            matches[fp] = min(got, needed)
            if got < needed:
                missing[fp] = needed - got
        result["expected_fingerprint_matches"] = dict(sorted(matches.items()))
        result["expected_fingerprint_missing"] = dict(sorted(missing.items()))
    return result


def recover_server_jsonl_from_outbox(outbox_rows: list[dict[str, Any]]) -> tuple[list[dict[str, Any]], dict[str, int]]:
    actions: list[dict[str, Any]] = []
    sources: Counter[str] = Counter()
    for row in outbox_rows:
        if row.get("action_kind") not in REQUIRED_KINDS:
            continue
        if row.get("status") != "applied":
            continue
        action, source = envelope_from_outbox_row(row)
        actions.append(action)
        sources[source] += 1
    return actions, dict(sorted(sources.items()))

def stable_json_hash(value: Any) -> str:
    encoded = json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def normalize_json_value(text: str | None) -> Any:
    if text is None or text == "" or text.upper() == "NULL":
        return None
    try:
        return json.loads(text)
    except Exception:
        return text


def cell(row: list[str], idx: int, default: str = "") -> str:
    return row[idx] if idx < len(row) else default


def load_outbox(mysql: Mysql, session_key: str) -> list[dict[str, Any]]:
    prefix = sql_quote(session_key + ":%")
    rows: list[list[str]]
    out: list[dict[str, Any]] = []

    if mysql.table_exists("mmo_server_action_outbox"):
        cols = mysql.columns("mmo_server_action_outbox")
        result_select = "NULL"
        if "result_payload" in cols:
            result_select = "CAST(result_payload AS CHAR)"
        request_select = "NULL"
        if "request_payload" in cols:
            request_select = "CAST(request_payload AS CHAR)"
        event_select = "NULL"
        if "event_id" in cols:
            event_select = "BIN_TO_UUID(event_id,1)"
        elif "event_uuid" in cols:
            event_select = "event_uuid"
        action_select = "NULL"
        if "action_id" in cols:
            action_select = "BIN_TO_UUID(action_id,1)"
        elif "action_uuid" in cols:
            action_select = "action_uuid"
        target_select = "COALESCE(target_key,'')" if "target_key" in cols else "''"
        updated_select = "COALESCE(CAST(updated_at AS CHAR),'')" if "updated_at" in cols else "''"
        error_code_select = "COALESCE(last_error_code,'')" if "last_error_code" in cols else "''"
        error_msg_select = "COALESCE(last_error_message,'')" if "last_error_message" in cols else "''"
        sql = f"""
        SELECT
          COALESCE(action_kind,''), COALESCE(status,''), COALESCE(idempotency_key,''),
          COALESCE({event_select},''), COALESCE({action_select},''), {target_select},
          COALESCE({result_select},''), COALESCE({request_select},''),
          {error_code_select}, {error_msg_select}, {updated_select}
        FROM mmo_server_action_outbox
        WHERE idempotency_key LIKE {prefix}
        ORDER BY idempotency_key;
        """
        rows = mysql.run(sql)
        for row in rows:
            result_payload = normalize_json_value(cell(row, 6))
            request_payload = normalize_json_value(cell(row, 7))
            out.append(
                {
                    "action_kind": cell(row, 0),
                    "status": cell(row, 1),
                    "idempotency_key": cell(row, 2),
                    "event_uuid": cell(row, 3) or None,
                    "action_uuid": cell(row, 4) or None,
                    "target_key": cell(row, 5),
                    "result_payload": result_payload,
                    "request_payload": request_payload,
                    "last_error_code": cell(row, 8),
                    "last_error_message": cell(row, 9),
                    "updated_at": cell(row, 10),
                }
            )
        return out

    if mysql.table_exists("v_server_action_outbox"):
        rows = mysql.run(
            f"""
            SELECT action_kind,status,idempotency_key,COALESCE(event_uuid,''),COALESCE(target_key,'')
            FROM v_server_action_outbox
            WHERE idempotency_key LIKE {prefix}
            ORDER BY idempotency_key;
            """
        )
        for row in rows:
            out.append(
                {
                    "action_kind": cell(row, 0),
                    "status": cell(row, 1),
                    "idempotency_key": cell(row, 2),
                    "event_uuid": cell(row, 3) or None,
                    "target_key": cell(row, 4),
                    "result_payload": None,
                    "request_payload": None,
                }
            )
    return out


def load_journal(mysql: Mysql, session_key: str) -> list[dict[str, Any]]:
    if not mysql.table_exists("world_event_journal"):
        return []
    prefix = sql_quote(session_key + ":%")
    cols = mysql.columns("world_event_journal")
    payload_select = "CAST(payload AS CHAR)" if "payload" in cols else "NULL"
    entity_select = "COALESCE(entity_key,'')" if "entity_key" in cols else "''"
    subject_select = "COALESCE(subject_key,'')" if "subject_key" in cols else "''"
    event_uuid_select = "BIN_TO_UUID(event_id,1)" if "event_id" in cols else "''"
    order_col = "event_seq" if "event_seq" in cols else "idempotency_key"
    rows = mysql.run(
        f"""
        SELECT
          COALESCE({event_uuid_select},''), COALESCE(event_type,''), COALESCE(event_class,''),
          COALESCE(source,''), COALESCE(idempotency_key,''), {entity_select}, {subject_select},
          COALESCE({payload_select},'')
        FROM world_event_journal
        WHERE idempotency_key LIKE {prefix}
        ORDER BY {order_col};
        """
    )
    return [
        {
            "event_uuid": cell(r, 0) or None,
            "event_type": cell(r, 1),
            "event_class": cell(r, 2),
            "source": cell(r, 3),
            "idempotency_key": cell(r, 4),
            "entity_key": cell(r, 5),
            "subject_key": cell(r, 6),
            "payload": normalize_json_value(cell(r, 7)),
        }
        for r in rows
    ]


def extract_uuid_from_result(payload: Any, key: str) -> str | None:
    if not isinstance(payload, dict):
        return None
    value = payload.get(key)
    if value is None and isinstance(payload.get("resolved"), dict):
        value = payload["resolved"].get(key)
    if value is None:
        return None
    value = str(value)
    # basic UUID sanity check
    if re.fullmatch(r"[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}", value):
        return value.lower()
    return value


def extract_world_item_key(payload: Any) -> str | None:
    if isinstance(payload, dict):
        if payload.get("world_item_entity_key"):
            return str(payload["world_item_entity_key"])
        if isinstance(payload.get("resolved"), dict) and payload["resolved"].get("world_item_entity_key"):
            return str(payload["resolved"]["world_item_entity_key"])
    return None


def extract_equipment_slot(payload: Any) -> str | None:
    if isinstance(payload, dict):
        if payload.get("equipment_slot"):
            return str(payload["equipment_slot"])
        if isinstance(payload.get("resolved"), dict) and payload["resolved"].get("equipment_slot"):
            return str(payload["resolved"]["equipment_slot"])
    return None


def query_item_instance(mysql: Mysql, uuid: str) -> dict[str, Any] | None:
    if not mysql.table_exists("item_instances") or not mysql.has_columns("item_instances", ["item_instance_id"]):
        return None
    cols = mysql.columns("item_instances")
    wanted = [
        ("uuid", "BIN_TO_UUID(item_instance_id,1)"),
        ("item_instance_key", "item_instance_key" if "item_instance_key" in cols else "''"),
        ("owner_type", "owner_type" if "owner_type" in cols else "''"),
        ("lifecycle_state", "lifecycle_state" if "lifecycle_state" in cols else "''"),
        ("amount", "amount" if "amount" in cols else ("quantity" if "quantity" in cols else "''")),
        ("owner_entity_key", "owner_entity_key" if "owner_entity_key" in cols else "''"),
        ("world_entity_key", "world_entity_key" if "world_entity_key" in cols else "''"),
    ]
    rows = mysql.run(
        "SELECT " + ",".join(expr for _, expr in wanted) +
        f" FROM item_instances WHERE item_instance_id=UUID_TO_BIN({sql_quote(uuid)},1) LIMIT 2;",
        allow_error=True,
    )
    if not rows:
        return None
    row = rows[0]
    return {name: cell(row, i) for i, (name, _) in enumerate(wanted)}


def query_world_entity(mysql: Mysql, key: str) -> dict[str, Any] | None:
    if not mysql.table_exists("world_entity_state") or "entity_key" not in mysql.columns("world_entity_state"):
        return None
    cols = mysql.columns("world_entity_state")
    wanted = [
        ("entity_key", "entity_key"),
        ("entity_kind", "entity_kind" if "entity_kind" in cols else "''"),
        ("lifecycle_state", "lifecycle_state" if "lifecycle_state" in cols else "''"),
        ("persistent_id", "persistent_id" if "persistent_id" in cols else "''"),
        ("item_symbol", "item_symbol" if "item_symbol" in cols else "''"),
        ("row_version", "row_version" if "row_version" in cols else "''"),
    ]
    rows = mysql.run(
        "SELECT " + ",".join(expr for _, expr in wanted) +
        f" FROM world_entity_state WHERE entity_key={sql_quote(key)} LIMIT 2;",
        allow_error=True,
    )
    if not rows:
        return None
    row = rows[0]
    return {name: cell(row, i) for i, (name, _) in enumerate(wanted)}


def count_by_item_uuid(mysql: Mysql, table: str, uuid: str) -> int | None:
    if not mysql.table_exists(table) or "item_instance_id" not in mysql.columns(table):
        return None
    lifecycle_clause = ""
    if "lifecycle_state" in mysql.columns(table):
        lifecycle_clause = " AND lifecycle_state='active'"
    rows = mysql.run(
        f"SELECT COUNT(*) FROM {table} WHERE item_instance_id=UUID_TO_BIN({sql_quote(uuid)},1){lifecycle_clause};",
        allow_error=True,
    )
    if not rows or not rows[0]:
        return None
    try:
        return int(rows[0][0])
    except ValueError:
        return None


def projection_checks(mysql: Mysql, outbox_rows: list[dict[str, Any]]) -> dict[str, Any]:
    checks: dict[str, Any] = {"pickup_items": [], "equipment_items": [], "errors": [], "warnings": []}
    for row in outbox_rows:
        if row.get("status") != "applied":
            continue
        kind = row.get("action_kind")
        payload = row.get("result_payload")
        uuid = extract_uuid_from_result(payload, "item_instance_uuid")
        world_key = extract_world_item_key(payload)
        equip_slot = extract_equipment_slot(payload)
        if kind == "pickup_world_item":
            item = query_item_instance(mysql, uuid) if uuid else None
            world = query_world_entity(mysql, world_key) if world_key else None
            inv_count = count_by_item_uuid(mysql, "character_inventory", uuid) if uuid else None
            pickup = {
                "idempotency_key": row.get("idempotency_key"),
                "item_instance_uuid": uuid,
                "world_item_entity_key": world_key,
                "item_instance": item,
                "world_entity": world,
                "character_inventory_active_rows": inv_count,
                "passed": False,
                "problems": [],
            }
            if not uuid:
                pickup["problems"].append("missing item_instance_uuid in outbox result_payload")
            if item is None:
                pickup["problems"].append("item_instances row not found")
            else:
                if item.get("owner_type") and item.get("owner_type") != "character":
                    pickup["problems"].append(f"item owner_type is {item.get('owner_type')!r}, expected 'character'")
                if item.get("lifecycle_state") and item.get("lifecycle_state") != "active":
                    pickup["problems"].append(f"item lifecycle_state is {item.get('lifecycle_state')!r}, expected 'active'")
            if inv_count is not None and inv_count < 1:
                pickup["problems"].append("character_inventory has no active row for picked item")
            if world is None:
                pickup["problems"].append("world_entity_state row not found")
            else:
                lifecycle = world.get("lifecycle_state")
                if lifecycle == "active":
                    pickup["problems"].append("world_entity_state is still active after pickup")
            pickup["passed"] = not pickup["problems"]
            checks["pickup_items"].append(pickup)
        elif kind in {"equip_character_item", "unequip_character_item"}:
            item = query_item_instance(mysql, uuid) if uuid else None
            inv_count = count_by_item_uuid(mysql, "character_inventory", uuid) if uuid else None
            equip_count = count_by_item_uuid(mysql, "character_equipment", uuid) if uuid else None
            eq = {
                "kind": kind,
                "idempotency_key": row.get("idempotency_key"),
                "item_instance_uuid": uuid,
                "equipment_slot": equip_slot,
                "item_instance": item,
                "character_inventory_active_rows": inv_count,
                "character_equipment_active_rows_for_item": equip_count,
                "passed": False,
                "problems": [],
            }
            if not uuid:
                eq["problems"].append("missing item_instance_uuid in outbox result_payload")
            if item is None:
                eq["problems"].append("item_instances row not found")
            else:
                if item.get("owner_type") and item.get("owner_type") != "character":
                    eq["problems"].append(f"item owner_type is {item.get('owner_type')!r}, expected 'character'")
                if item.get("lifecycle_state") and item.get("lifecycle_state") != "active":
                    eq["problems"].append(f"item lifecycle_state is {item.get('lifecycle_state')!r}, expected 'active'")
            if inv_count is not None and inv_count < 1:
                eq["problems"].append("character_inventory has no active row for equipment item")
            if kind == "unequip_character_item" and equip_count is not None and equip_count != 0:
                eq["problems"].append("item remains in character_equipment after unequip")
            eq["passed"] = not eq["problems"]
            checks["equipment_items"].append(eq)
    for bucket in ("pickup_items", "equipment_items"):
        for entry in checks[bucket]:
            if not entry.get("passed"):
                checks["errors"].append({"idempotency_key": entry.get("idempotency_key"), "problems": entry.get("problems", [])})
    return checks


def load_sqlite_evidence(path: Path | None) -> dict[str, Any]:
    if path is None:
        return {"enabled": False}
    result: dict[str, Any] = {"enabled": True, "path": str(path), "exists": path.exists(), "tables": {}, "hashes": {}, "errors": []}
    if not path.exists():
        return result
    try:
        con = sqlite3.connect(str(path))
        con.row_factory = sqlite3.Row
        tables = [r[0] for r in con.execute("SELECT name FROM sqlite_master WHERE type='table' ORDER BY name")]
        interesting = [
            t
            for t in tables
            if any(fragment in t.lower() for fragment in ("inventory", "equipment", "world_item", "save_slot", "character"))
        ]
        for table in interesting[:80]:
            try:
                count = con.execute(f'SELECT COUNT(*) FROM "{table}"').fetchone()[0]
                result["tables"][table] = int(count)
                sample_rows = con.execute(f'SELECT * FROM "{table}" LIMIT 25').fetchall()
                sample = [dict(r) for r in sample_rows]
                result["hashes"][table] = stable_json_hash(sample)
            except Exception as exc:  # noqa: BLE001
                result["errors"].append(f"{table}: {exc}")
        con.close()
    except Exception as exc:  # noqa: BLE001
        result["errors"].append(str(exc))
    return result


def file_hashes(paths: list[Path]) -> dict[str, Any]:
    out: dict[str, Any] = {}
    for path in paths:
        if not path.exists():
            out[str(path)] = {"exists": False}
            continue
        if path.is_dir():
            files = sorted(p for p in path.rglob("*") if p.is_file())[:200]
        else:
            files = [path]
        entries = []
        for p in files:
            try:
                h = hashlib.sha256()
                with p.open("rb") as f:
                    for chunk in iter(lambda: f.read(1024 * 1024), b""):
                        h.update(chunk)
                entries.append({"path": str(p), "size": p.stat().st_size, "sha256": h.hexdigest()})
            except Exception as exc:  # noqa: BLE001
                entries.append({"path": str(p), "error": str(exc)})
        out[str(path)] = {"exists": True, "files": entries}
    return out


def summarize_counts(rows: list[dict[str, Any]], key_fields: tuple[str, ...]) -> dict[str, int]:
    c: Counter[str] = Counter()
    for row in rows:
        label = "/".join(str(row.get(k, "")) for k in key_fields)
        c[label] += 1
    return dict(sorted(c.items()))


def evaluate(args: argparse.Namespace) -> tuple[int, dict[str, Any]]:
    target = parse_mysql_url(args.url)
    mysql = Mysql(target)
    session_key = args.session_key

    outbox = load_outbox(mysql, session_key)
    applied_outbox_fingerprints = Counter(
        fingerprint_from_outbox(r)
        for r in outbox
        if r.get("status") == "applied" and r.get("action_kind") in REQUIRED_KINDS
    )
    client_jsonl = read_jsonl(Path(args.client_jsonl), session_key, applied_outbox_fingerprints) if args.client_jsonl else {"enabled": False}
    server_jsonl = read_jsonl(Path(args.server_jsonl), session_key, applied_outbox_fingerprints) if args.server_jsonl else {"enabled": False}
    recovered_server_jsonl: dict[str, Any] = {"enabled": False}
    if args.recover_server_jsonl_from_outbox:
        recovered_actions, recovered_sources = recover_server_jsonl_from_outbox(outbox)
        if args.write_recovered_server_jsonl:
            out_path = Path(args.write_recovered_server_jsonl)
            out_path.parent.mkdir(parents=True, exist_ok=True)
            with out_path.open("w", encoding="utf-8") as f:
                for obj in recovered_actions:
                    f.write(json.dumps(obj, ensure_ascii=False, sort_keys=True, separators=(",", ":")) + "\n")
        recovered_server_jsonl = evidence_from_json_actions(
            name="server_jsonl_recovered_from_outbox",
            actions=recovered_actions,
            session_key=session_key,
            expected_fingerprints=applied_outbox_fingerprints,
            path=args.write_recovered_server_jsonl or "<outbox.request_payload>",
            source="outbox_request_payload",
        )
        recovered_server_jsonl["recovered_sources"] = recovered_sources
        if (not server_jsonl.get("exists")) or int(server_jsonl.get("rows", 0) or 0) == 0:
            server_jsonl = dict(recovered_server_jsonl)
            server_jsonl["replaces_empty_server_jsonl"] = True
    journal = load_journal(mysql, session_key)
    projection = projection_checks(mysql, outbox)

    outbox_by_kind_status = summarize_counts(outbox, ("action_kind", "status"))
    journal_by_type = summarize_counts(journal, ("event_type", "event_class", "source"))

    requirements: dict[str, Any] = {}
    failures: list[str] = []
    warnings: list[str] = []

    applied_by_kind = Counter(r["action_kind"] for r in outbox if r.get("status") == "applied")
    status_bad = [r for r in outbox if r.get("status") in {"failed", "dead_letter", "claimed", "pending"}]
    for kind in REQUIRED_KINDS:
        min_count = 2 if kind == "pickup_world_item" and args.require_two_pickups else 1
        ok = applied_by_kind[kind] >= min_count
        requirements[f"outbox_applied_{kind}"] = {"passed": ok, "count": applied_by_kind[kind], "required": min_count}
        if not ok:
            failures.append(f"outbox has {applied_by_kind[kind]} applied {kind}, required {min_count}")
    requirements["outbox_no_unfinished_or_failed"] = {"passed": not status_bad, "bad_rows": status_bad}
    if status_bad:
        failures.append(f"outbox has {len(status_bad)} unfinished/failed matching rows")

    journal_counts = Counter(j["event_type"] for j in journal)
    for kind, event_type in EXPECTED_JOURNAL_TYPES.items():
        min_count = 2 if kind == "pickup_world_item" and args.require_two_pickups else 1
        ok = journal_counts[event_type] >= min_count
        requirements[f"journal_{event_type}"] = {"passed": ok, "count": journal_counts[event_type], "required": min_count}
        if not ok:
            failures.append(f"journal has {journal_counts[event_type]} {event_type}, required {min_count}")

    pickup_entries = projection.get("pickup_items", [])
    equipment_entries = projection.get("equipment_items", [])
    pickup_passed = sum(1 for e in pickup_entries if e.get("passed"))
    equip_passed = any(e.get("passed") and e.get("kind") == "equip_character_item" for e in equipment_entries)
    unequip_passed = any(e.get("passed") and e.get("kind") == "unequip_character_item" for e in equipment_entries)
    requirements["projection_pickup"] = {"passed": pickup_passed >= (2 if args.require_two_pickups else 1), "passed_count": pickup_passed}
    requirements["projection_equip"] = {"passed": equip_passed}
    requirements["projection_unequip_final"] = {"passed": unequip_passed}
    if not requirements["projection_pickup"]["passed"]:
        failures.append("projection check did not confirm enough picked items moved to character and left world")
    if not equip_passed:
        failures.append("projection check did not confirm equipped item ownership/inventory")
    if not unequip_passed:
        failures.append("projection check did not confirm unequipped final state")

    for name, evidence in (("client_jsonl", client_jsonl), ("server_jsonl", server_jsonl)):
        if evidence.get("enabled") is False:
            continue
        if not evidence.get("exists"):
            warnings.append(f"{name} path does not exist: {evidence.get('path')}")
            continue
        if evidence.get("errors"):
            failures.append(f"{name} contains JSON errors")
        if evidence.get("duplicate_idempotency_keys"):
            failures.append(f"{name} contains duplicate idempotency keys")
        kind_counts = evidence.get("kind_counts", {})
        fp_kind_counts = evidence.get("fingerprint_kind_counts", {})
        missing_fp = evidence.get("expected_fingerprint_missing", {})
        for kind in REQUIRED_KINDS:
            if kind_counts.get(kind, 0) == 0:
                if fp_kind_counts.get(kind, 0) > 0:
                    warnings.append(
                        f"{name} has no session-prefix {kind}, but fingerprint correlation matched "
                        f"{fp_kind_counts.get(kind, 0)} row(s); likely replay session-key rewrite"
                    )
                else:
                    message = f"{name} has no matching {kind}; DB-only evidence may still pass but capture comparison is incomplete"
                    if args.require_jsonl_correlation:
                        failures.append(message)
                    else:
                        warnings.append(message)
        if missing_fp:
            message = f"{name} is missing {sum(int(v) for v in missing_fp.values())} expected action fingerprint(s)"
            if args.require_jsonl_correlation:
                failures.append(message)
            else:
                warnings.append(message)

    sqlite_evidence = load_sqlite_evidence(Path(args.sqlite) if args.sqlite else None)
    if args.sqlite and not sqlite_evidence.get("exists"):
        warnings.append(f"sqlite evidence path does not exist: {args.sqlite}")

    save_evidence = file_hashes([Path(p) for p in args.save_artifact]) if args.save_artifact else {}
    if args.save_artifact:
        warnings.append("native save evidence is hashed only; semantic native-vs-MySQL comparison is not implemented in Step36 v1")

    evidence_level = "db_projection_dispatch"
    if sqlite_evidence.get("exists") and save_evidence:
        evidence_level = "db_projection_plus_sqlite_and_native_hash_artifacts"
    elif sqlite_evidence.get("exists"):
        evidence_level = "db_projection_plus_sqlite_snapshot_summary"

    passed = not failures
    report: dict[str, Any] = {
        "version": 1,
        "tool": "check_mmo_step36_vertical_slice.py",
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "session_key": session_key,
        "evidence_level": evidence_level,
        "passed": passed,
        "status": "passed" if passed else "failed",
        "failures": failures,
        "warnings": warnings,
        "requirements": requirements,
        "client_jsonl": client_jsonl,
        "server_jsonl": server_jsonl,
        "recovered_server_jsonl": recovered_server_jsonl,
        "applied_outbox_fingerprints": dict(sorted(applied_outbox_fingerprints.items())),
        "outbox_by_kind_status": outbox_by_kind_status,
        "journal_by_type": journal_by_type,
        "outbox_rows": outbox,
        "journal_rows": journal,
        "projection_checks": projection,
        "sqlite_evidence": sqlite_evidence,
        "native_save_evidence": save_evidence,
        "hash": None,
    }
    report["hash"] = stable_json_hash({k: v for k, v in report.items() if k != "hash"})
    return (0 if passed else 1), report


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description="Check Step 36 pickup/equip/unequip vertical-slice evidence")
    ap.add_argument("--url", required=True, help="mysql://user:pass@host:port/database")
    ap.add_argument("--session-key", required=True, help="idempotency/session prefix, e.g. local-dev-PC_HERO_STEP35V2")
    ap.add_argument("--client-jsonl", default="runtime/mmo_actions.jsonl", help="optional local client JSONL evidence")
    ap.add_argument("--server-jsonl", default="", help="optional receiver JSONL evidence")
    ap.add_argument("--sqlite", default="", help="optional runtime SQLite file for snapshot summary")
    ap.add_argument("--save-artifact", action="append", default=[], help="optional native .sav/save directory/file to hash")
    ap.add_argument("--output", default="", help="write JSON report to this path")
    ap.add_argument("--require-two-pickups", action="store_true", help="require two applied pickup actions instead of one")
    ap.add_argument("--require-jsonl-correlation", action="store_true", help="fail if client/server JSONL cannot be correlated to applied DB actions by fingerprint")
    ap.add_argument("--recover-server-jsonl-from-outbox", action="store_true", help="when receiver JSONL is missing/empty, reconstruct server-side evidence from mmo_server_action_outbox.request_payload")
    ap.add_argument("--write-recovered-server-jsonl", default="", help="optional path to write recovered server JSONL reconstructed from outbox")
    args = ap.parse_args(argv)

    try:
        code, report = evaluate(args)
    except Exception as exc:  # noqa: BLE001
        print(f"[ERROR] {exc}", file=sys.stderr)
        return 2

    if args.output:
        path = Path(args.output)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(report, indent=2, sort_keys=True, ensure_ascii=False) + "\n", encoding="utf-8")
        print(f"artifact={path}")

    print(f"session={report['session_key']}")
    print(f"status={report['status']} evidence_level={report['evidence_level']} hash={report['hash']}")
    print("outbox:")
    for key, value in report["outbox_by_kind_status"].items():
        print(f"  {key}={value}")
    print("journal:")
    for key, value in report["journal_by_type"].items():
        print(f"  {key}={value}")
    if report.get("client_jsonl", {}).get("exists"):
        cj = report["client_jsonl"]
        print(f"client_jsonl: rows={cj.get('rows')} session_rows={cj.get('matching_rows')} fingerprint_rows={cj.get('fingerprint_matching_rows')}")
    if report.get("server_jsonl", {}).get("exists"):
        sj = report["server_jsonl"]
        extra = ""
        if sj.get("source"):
            extra += f" source={sj.get('source')}"
        if sj.get("replaces_empty_server_jsonl"):
            extra += " recovered=1"
        print(f"server_jsonl: rows={sj.get('rows')} session_rows={sj.get('matching_rows')} fingerprint_rows={sj.get('fingerprint_matching_rows')}{extra}")
    print("requirements:")
    for key, value in report["requirements"].items():
        state = "OK" if value.get("passed") else "FAIL"
        print(f"  [{state}] {key}")
    for warning in report.get("warnings", []):
        print(f"[WARN] {warning}")
    for failure in report.get("failures", []):
        print(f"[FAIL] {failure}")
    print("[OK]" if code == 0 else "[FAIL]")
    return code


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
