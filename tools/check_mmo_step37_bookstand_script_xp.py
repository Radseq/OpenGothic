#!/usr/bin/env python3
"""Step37 script/progression vertical-slice evidence checker.

Read-only checker for the next MMO slice:

  bookstand/bookshelf -> one-shot script flag -> XP/progression reward

It verifies evidence across:
  client/server JSONL -> server outbox -> world_event_journal -> MySQL projections

This does not mark global restore parity as passed. It proves that the server
path can make a one-shot script flag and reward idempotent in the production DB
contract.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import sqlite3
import subprocess
import sys
from collections import Counter
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable
from urllib.parse import unquote, urlparse

SCRIPT_KINDS = ("set_script_int",)
XP_KINDS = ("adjust_progression", "apply_experience_reward")
OPTIONAL_KINDS = ("update_quest", "set_known_dialog")
ALL_KINDS = SCRIPT_KINDS + XP_KINDS + OPTIONAL_KINDS
EXPECTED_EVENT_TYPES = {
    "set_script_int": "character_script_int_set",
    "adjust_progression": "character_progression_adjusted",
    "apply_experience_reward": "character_progression_adjusted",
    "update_quest": "character_quest_updated",
    "set_known_dialog": "character_dialog_known_set",
}


@dataclass(frozen=True)
class MysqlTarget:
    host: str
    port: int
    user: str
    password: str
    database: str


@dataclass
class Check:
    name: str
    ok: bool
    detail: str
    severity: str = "error"


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
            "--default-character-set=utf8mb4",
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
        return [line.rstrip("\n").split("\t") for line in proc.stdout.splitlines()]

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


def cell(row: list[str], idx: int, default: str = "") -> str:
    return row[idx] if idx < len(row) else default


def normalize_json(text: str | None) -> Any:
    if text is None or text == "" or text.upper() == "NULL":
        return None
    try:
        return json.loads(text)
    except Exception:
        return text


def nested_get(value: Any, *keys: str) -> Any:
    if not isinstance(value, dict):
        return None
    for key in keys:
        cur: Any = value
        ok = True
        for part in key.split("."):
            if not isinstance(cur, dict) or part not in cur:
                ok = False
                break
            cur = cur[part]
        if ok and cur not in (None, ""):
            return cur
    return None


def payload_first(row: dict[str, Any], *keys: str) -> Any:
    for source in (row.get("request_payload"), row.get("result_payload")):
        value = nested_get(source, *keys)
        if value not in (None, ""):
            return value
        client = nested_get(source, "client_payload")
        value = nested_get(client, *keys)
        if value not in (None, ""):
            return value
    return None


def action_fingerprint(kind: str, target_key: str) -> str:
    return f"{kind}|{target_key}"


def fingerprint_from_json(obj: dict[str, Any]) -> str:
    kind = str(obj.get("action_kind", ""))
    target = str(obj.get("target_key", ""))
    if not target and isinstance(obj.get("payload"), dict):
        payload = obj["payload"]
        target = str(payload.get("target_key") or payload.get("script_key") or payload.get("global_key") or payload.get("info_key") or "")
    return action_fingerprint(kind, target)


def fingerprint_from_outbox(row: dict[str, Any]) -> str:
    target = row.get("target_key") or payload_first(row, "target_key", "script_key", "global_key", "info_key") or ""
    return action_fingerprint(str(row.get("action_kind", "")), str(target))


def read_jsonl(path: Path, session_key: str | None, expected: Counter[str] | None) -> dict[str, Any]:
    result: dict[str, Any] = {
        "path": str(path),
        "exists": path.exists(),
        "rows": 0,
        "matching_rows": 0,
        "kind_counts": {},
        "all_kind_counts": {},
        "fingerprint_matching_rows": 0,
        "expected_fingerprint_missing": {},
        "duplicate_idempotency_keys": [],
        "errors": [],
    }
    if not path.exists():
        return result
    seen: set[str] = set()
    dupes: list[str] = []
    counts: Counter[str] = Counter()
    all_counts: Counter[str] = Counter()
    fp_counts: Counter[str] = Counter()
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
            all_counts[kind] += 1
            fp = fingerprint_from_json(obj)
            fp_counts[fp] += 1
            if expected and fp in expected:
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
    result["duplicate_idempotency_keys"] = dupes
    if expected:
        missing: dict[str, int] = {}
        for fp, needed in expected.items():
            got = fp_counts.get(fp, 0)
            if got < needed:
                missing[fp] = needed - got
        result["expected_fingerprint_missing"] = dict(sorted(missing.items()))
    return result


def load_outbox(mysql: Mysql, session_key: str) -> list[dict[str, Any]]:
    if not mysql.table_exists("mmo_server_action_outbox"):
        return []
    cols = mysql.columns("mmo_server_action_outbox")
    result_select = "CAST(result_payload AS CHAR)" if "result_payload" in cols else "NULL"
    request_select = "CAST(request_payload AS CHAR)" if "request_payload" in cols else "NULL"
    event_select = "BIN_TO_UUID(event_id,1)" if "event_id" in cols else "NULL"
    action_select = "BIN_TO_UUID(action_id,1)" if "action_id" in cols else "NULL"
    target_select = "COALESCE(target_key,'')" if "target_key" in cols else "''"
    rows = mysql.run(
        f"""
        SELECT COALESCE(action_kind,''), COALESCE(status,''), COALESCE(idempotency_key,''),
               COALESCE({event_select},''), COALESCE({action_select},''), {target_select},
               COALESCE({result_select},''), COALESCE({request_select},''),
               COALESCE(last_error_code,''), COALESCE(last_error_message,'')
          FROM mmo_server_action_outbox
         WHERE idempotency_key LIKE {sql_quote(session_key + ':%')}
         ORDER BY idempotency_key;
        """
    )
    out: list[dict[str, Any]] = []
    for row in rows:
        out.append(
            {
                "action_kind": cell(row, 0),
                "status": cell(row, 1),
                "idempotency_key": cell(row, 2),
                "event_uuid": cell(row, 3) or None,
                "action_uuid": cell(row, 4) or None,
                "target_key": cell(row, 5),
                "result_payload": normalize_json(cell(row, 6)),
                "request_payload": normalize_json(cell(row, 7)),
                "last_error_code": cell(row, 8),
                "last_error_message": cell(row, 9),
            }
        )
    return out


def load_journal(mysql: Mysql, session_key: str) -> list[dict[str, Any]]:
    if not mysql.table_exists("world_event_journal"):
        return []
    cols = mysql.columns("world_event_journal")
    payload_select = "CAST(payload AS CHAR)" if "payload" in cols else "NULL"
    entity_select = "COALESCE(entity_key,'')" if "entity_key" in cols else "''"
    subject_select = "COALESCE(subject_key,'')" if "subject_key" in cols else "''"
    event_select = "BIN_TO_UUID(event_id,1)" if "event_id" in cols else "''"
    order_col = "event_seq" if "event_seq" in cols else "idempotency_key"
    rows = mysql.run(
        f"""
        SELECT COALESCE({event_select},''), COALESCE(event_type,''), COALESCE(event_class,''),
               COALESCE(source,''), COALESCE(idempotency_key,''), {entity_select}, {subject_select},
               COALESCE({payload_select},'')
          FROM world_event_journal
         WHERE idempotency_key LIKE {sql_quote(session_key + ':%')}
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
            "payload": normalize_json(cell(r, 7)),
        }
        for r in rows
    ]


def projection_summary(mysql: Mysql, outbox_rows: list[dict[str, Any]]) -> dict[str, Any]:
    summary: dict[str, Any] = {"script_rows": [], "character_stats": None, "quest_rows": [], "known_dialog_rows": []}
    script_keys = sorted({str(payload_first(r, "script_key", "global_key", "symbol_name") or r.get("target_key") or "") for r in outbox_rows if r.get("action_kind") == "set_script_int"})
    script_keys = [x for x in script_keys if x]
    if script_keys and mysql.table_exists("character_script_state"):
        keys = ",".join(sql_quote(k) for k in script_keys)
        cols = mysql.columns("character_script_state")
        value_col = "value_int" if "value_int" in cols else "value_text"
        rows = mysql.run(
            f"""
            SELECT script_key, COALESCE(CAST(symbol_index AS CHAR),''), COALESCE(CAST(value_index AS CHAR),''),
                   COALESCE(CAST({value_col} AS CHAR),'')
              FROM character_script_state
             WHERE script_key IN ({keys})
             ORDER BY script_key, value_index;
            """,
            allow_error=True,
        )
        summary["script_rows"] = [{"script_key": cell(r, 0), "symbol_index": cell(r, 1), "value_index": cell(r, 2), "value": cell(r, 3)} for r in rows]
    if mysql.table_exists("character_stats") and mysql.table_exists("server_sessions"):
        rows = mysql.run(
            f"""
            SELECT COALESCE(CAST(cs.experience AS CHAR),''), COALESCE(CAST(cs.learning_points AS CHAR),'')
              FROM server_sessions ss
              JOIN character_stats cs ON cs.character_id=ss.character_id
             WHERE ss.session_key={sql_quote(args_global_session_key)}
             LIMIT 1;
            """,
            allow_error=True,
        )
        if rows:
            summary["character_stats"] = {"experience": cell(rows[0], 0), "learning_points": cell(rows[0], 1)}
    quest_keys = sorted({str(payload_first(r, "quest_key", "topic") or r.get("target_key") or "") for r in outbox_rows if r.get("action_kind") == "update_quest"})
    quest_keys = [x for x in quest_keys if x]
    if quest_keys and mysql.table_exists("character_quests"):
        keys = ",".join(sql_quote(k) for k in quest_keys)
        rows = mysql.run(f"SELECT quest_key,status,COALESCE(CAST(entry_order AS CHAR),'') FROM character_quests WHERE quest_key IN ({keys}) ORDER BY quest_key;", allow_error=True)
        summary["quest_rows"] = [{"quest_key": cell(r, 0), "status": cell(r, 1), "entry_order": cell(r, 2)} for r in rows]
    info_keys = sorted({str(payload_first(r, "info_key", "info_symbol_name") or r.get("target_key") or "") for r in outbox_rows if r.get("action_kind") == "set_known_dialog"})
    info_keys = [x for x in info_keys if x]
    if info_keys and mysql.table_exists("character_known_dialogs"):
        keys = ",".join(sql_quote(k) for k in info_keys)
        rows = mysql.run(f"SELECT npc_key,info_key,COALESCE(CAST(known AS CHAR),''),availability_state FROM character_known_dialogs WHERE info_key IN ({keys}) ORDER BY npc_key,info_key;", allow_error=True)
        summary["known_dialog_rows"] = [{"npc_key": cell(r, 0), "info_key": cell(r, 1), "known": cell(r, 2), "availability_state": cell(r, 3)} for r in rows]
    return summary


def sqlite_summary(path: Path | None) -> dict[str, Any]:
    if path is None:
        return {"provided": False}
    result: dict[str, Any] = {"provided": True, "path": str(path), "exists": path.exists()}
    if not path.exists():
        return result
    result["sha256"] = hashlib.sha256(path.read_bytes()).hexdigest()
    try:
        conn = sqlite3.connect(path)
        cur = conn.cursor()
        tables = [
            "runtime_script_globals",
            "runtime_script_global_history",
            "runtime_known_dialogs",
            "runtime_known_dialog_history",
            "runtime_quests",
            "runtime_quest_history",
            "mmo_script_global_values_current",
            "mmo_character_known_dialogs_current",
            "mmo_character_quests_current",
        ]
        counts: dict[str, int] = {}
        for table in tables:
            row = cur.execute("SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name=?", (table,)).fetchone()
            if row and row[0]:
                counts[table] = int(cur.execute(f"SELECT COUNT(*) FROM {table}").fetchone()[0])
        result["table_counts"] = counts
    except Exception as exc:  # noqa: BLE001
        result["error"] = str(exc)
    return result


def print_check(check: Check) -> None:
    prefix = "[OK]" if check.ok else ("[WARN]" if check.severity == "warning" else "[FAIL]")
    print(f"{prefix} {check.name}: {check.detail}")


def main() -> int:
    global args_global_session_key
    ap = argparse.ArgumentParser(description="Check Step37 bookstand/bookshelf script flag + XP evidence in MySQL.")
    ap.add_argument("--url", required=True, help="mysql://user:password@host:port/database")
    ap.add_argument("--session-key", required=True, help="semantic action session key prefix")
    ap.add_argument("--client-jsonl", default="", help="optional local OpenGothic semantic action JSONL")
    ap.add_argument("--server-jsonl", default="", help="optional receiver JSONL")
    ap.add_argument("--sqlite", default="", help="optional runtime SQLite file for hash/table-count evidence")
    ap.add_argument("--output", default="", help="optional JSON artifact output")
    ap.add_argument("--require-jsonl-correlation", action="store_true", help="fail if provided JSONL files do not contain matching Step37 fingerprints")
    args = ap.parse_args()
    args_global_session_key = args.session_key

    mysql = Mysql(parse_mysql_url(args.url))
    outbox = load_outbox(mysql, args.session_key)
    journal = load_journal(mysql, args.session_key)

    checks: list[Check] = []
    checks.append(Check("outbox present", bool(outbox), f"rows={len(outbox)}"))

    outbox_counts = Counter(str(r.get("action_kind", "")) for r in outbox)
    applied_counts = Counter(str(r.get("action_kind", "")) for r in outbox if r.get("status") == "applied")
    failed_rows = [r for r in outbox if r.get("status") in {"failed", "dead_letter"}]
    checks.append(Check("script flag action applied", applied_counts["set_script_int"] >= 1, f"applied={applied_counts.get('set_script_int', 0)}"))
    xp_applied = sum(applied_counts[k] for k in XP_KINDS)
    checks.append(Check("xp/progression action applied", xp_applied >= 1, f"adjust={applied_counts.get('adjust_progression', 0)} reward={applied_counts.get('apply_experience_reward', 0)}"))
    checks.append(Check("no failed Step37 outbox rows", not failed_rows, f"failed={len(failed_rows)}"))

    idem_counts = Counter(str(r.get("idempotency_key", "")) for r in outbox if r.get("idempotency_key"))
    dupes = {k: v for k, v in idem_counts.items() if v > 1}
    checks.append(Check("outbox idempotency unique", not dupes, json.dumps(dupes, sort_keys=True) if dupes else "ok"))

    journal_counts = Counter(str(r.get("event_type", "")) for r in journal)
    checks.append(Check("script journal event", journal_counts["character_script_int_set"] >= 1, f"count={journal_counts.get('character_script_int_set', 0)}"))
    checks.append(Check("progression journal event", journal_counts["character_progression_adjusted"] >= 1, f"count={journal_counts.get('character_progression_adjusted', 0)}"))
    non_server = [r for r in journal if r.get("event_type") in {"character_script_int_set", "character_progression_adjusted", "character_quest_updated", "character_dialog_known_set"} and r.get("source") not in {"server", ""}]
    checks.append(Check("journal server sourced", not non_server, f"non_server={len(non_server)}"))

    projections = projection_summary(mysql, outbox)
    checks.append(Check("script projection row", bool(projections.get("script_rows")), f"rows={len(projections.get('script_rows') or [])}"))
    if any(r.get("action_kind") == "update_quest" for r in outbox):
        checks.append(Check("quest projection row", bool(projections.get("quest_rows")), f"rows={len(projections.get('quest_rows') or [])}"))
    if any(r.get("action_kind") == "set_known_dialog" for r in outbox):
        checks.append(Check("known dialog projection row", bool(projections.get("known_dialog_rows")), f"rows={len(projections.get('known_dialog_rows') or [])}"))

    expected_fp = Counter(fingerprint_from_outbox(r) for r in outbox if r.get("action_kind") in ALL_KINDS and r.get("status") == "applied")
    client_jsonl = read_jsonl(Path(args.client_jsonl), args.session_key, expected_fp) if args.client_jsonl else None
    server_jsonl = read_jsonl(Path(args.server_jsonl), args.session_key, expected_fp) if args.server_jsonl else None
    if client_jsonl is not None:
        ok = not client_jsonl["errors"] and not client_jsonl["duplicate_idempotency_keys"]
        if args.require_jsonl_correlation:
            ok = ok and not client_jsonl["expected_fingerprint_missing"]
        checks.append(Check("client JSONL", ok, f"rows={client_jsonl['rows']} matching={client_jsonl['matching_rows']} fp_missing={len(client_jsonl['expected_fingerprint_missing'])}"))
    if server_jsonl is not None:
        ok = not server_jsonl["errors"] and not server_jsonl["duplicate_idempotency_keys"]
        if args.require_jsonl_correlation:
            ok = ok and not server_jsonl["expected_fingerprint_missing"]
        checks.append(Check("server JSONL", ok, f"rows={server_jsonl['rows']} matching={server_jsonl['matching_rows']} fp_missing={len(server_jsonl['expected_fingerprint_missing'])}"))

    artifact = {
        "tool": "check_mmo_step37_bookstand_script_xp.py",
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "session_key": args.session_key,
        "status": "passed" if all(c.ok or c.severity == "warning" for c in checks) else "failed",
        "checks": [c.__dict__ for c in checks],
        "outbox_counts": dict(sorted(outbox_counts.items())),
        "applied_counts": dict(sorted(applied_counts.items())),
        "journal_counts": dict(sorted(journal_counts.items())),
        "outbox_rows": outbox,
        "journal_rows": journal,
        "projection_summary": projections,
        "client_jsonl": client_jsonl,
        "server_jsonl": server_jsonl,
        "sqlite": sqlite_summary(Path(args.sqlite) if args.sqlite else None),
        "interpretation": "Step37 evidence only: proves one-shot script flag + XP/progression server path/idempotency, not full .sav + SQLite + MySQL restore parity.",
    }

    for check in checks:
        print_check(check)
    print(f"status={artifact['status']}")

    if args.output:
        out_path = Path(args.output)
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text(json.dumps(artifact, ensure_ascii=False, indent=2, sort_keys=True), encoding="utf-8")
        print(f"artifact={out_path}")

    return 0 if artifact["status"] == "passed" else 2


args_global_session_key = ""

if __name__ == "__main__":
    raise SystemExit(main())
