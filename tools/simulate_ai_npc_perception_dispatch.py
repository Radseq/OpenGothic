#!/usr/bin/env python3
"""Simulate one NPC perception decision and dispatch cycle in mmo_ai_runtime."""
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

from pathlib import Path as _MysqlCliPath

_MYSQL_CLI_TOOLS_DIR = _MysqlCliPath(__file__).resolve().parent
if str(_MYSQL_CLI_TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(_MYSQL_CLI_TOOLS_DIR))
try:
    from _mysql_cli import resolve_mysql_exe
except Exception:  # pragma: no cover - fallback for standalone patch bundles.
    def resolve_mysql_exe() -> str | None:
        return shutil.which("mysql")


ROOT = Path(__file__).resolve().parents[1]


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
        raise ValueError("expected mysql://user:password@host:port/database")
    return Target(parsed.hostname or "localhost", parsed.port or 3306, unquote(parsed.username or ""), unquote(parsed.password or ""), (parsed.path or "/").lstrip("/"))


def mysql_cmd(target: Target) -> list[str]:
    exe = resolve_mysql_exe()
    if exe is None:
        raise RuntimeError("mysql executable not found in PATH")
    cmd = [exe, "--default-character-set=utf8mb4", "--init-command=SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci", "--batch", "--raw", "--skip-column-names", "-h", target.host, "-P", str(target.port), "-u", target.user]
    if target.password:
        cmd.append(f"-p{target.password}")
    cmd.append(target.database)
    return cmd


def sql_literal(value: object) -> str:
    if value is None:
        return "NULL"
    return "'" + str(value).replace("\\", "\\\\").replace("'", "''").replace("\0", "") + "'"


def run_mysql(target: Target, sql: str) -> list[object]:
    proc = subprocess.run(mysql_cmd(target), input=sql, text=True, encoding="utf-8", errors="replace", stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=str(ROOT))
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or f"mysql exited with status {proc.returncode}")
    events: list[object] = []
    for line in proc.stdout.splitlines():
        line = line.strip()
        if line.startswith("{") and line.endswith("}"):
            events.append(json.loads(line))
    return events


def main() -> int:
    parser = argparse.ArgumentParser(description="Simulate one Step212/213 AI NPC perception dispatch cycle.")
    parser.add_argument("--url", required=True, help="mysql://user:password@host:port/database")
    parser.add_argument("--ai-db-name", default="mmo_ai_runtime")
    parser.add_argument("--world-instance-uuid", default="")
    parser.add_argument("--session-uuid", default="")
    parser.add_argument("--character-uuid", default="")
    parser.add_argument("--character-key", default="hero")
    parser.add_argument("--npc-entity-key", default="npc:test:greet")
    parser.add_argument("--target-key", default="player:hero")
    parser.add_argument("--content-revision-key", default="dev-content")
    parser.add_argument("--perception-kind", default="PERC_ASSESSPLAYER")
    parser.add_argument("--decision-kind", default="greet_player", choices=["assess_player", "turn_to_player", "approach_player", "greet_player", "warn_player", "start_dialog", "attack_player", "ignore_player", "noop"])
    parser.add_argument("--server-tick", type=int, default=1)
    parser.add_argument("--cooldown-ticks", type=int, default=120)
    parser.add_argument("--priority", type=int, default=50)
    parser.add_argument("--worker-id", default="dev-ai-dispatch-worker")
    parser.add_argument("--finish", choices=["applied", "failed", "skipped", "claim-only"], default="applied")
    parser.add_argument("--idempotency-key", default="")
    parser.add_argument("--output", default="")
    args = parser.parse_args()

    target = parse_mysql_url(args.url)
    world_uuid = args.world_instance_uuid or str(uuid.uuid4())
    idem = args.idempotency_key or "ai-sim:" + str(uuid.uuid4())
    db = args.ai_db_name.replace("`", "")

    payload = (
        "JSON_OBJECT("
        "'source','simulate_ai_npc_perception_dispatch.py',"
        "'created_at'," + sql_literal(datetime.now(timezone.utc).isoformat()) + ","
        "'world_instance_uuid'," + sql_literal(world_uuid) + ","
        "'npc_entity_key'," + sql_literal(args.npc_entity_key) + ","
        "'target_key'," + sql_literal(args.target_key) + ","
        "'decision_kind'," + sql_literal(args.decision_kind) + ")"
    )

    sql = [
        "SET @mmo_ai_decision_id=NULL;",
        "SET @mmo_ai_decision_status=NULL;",
        "SET @mmo_ai_action_queue_id=NULL;",
        "CALL `" + db + "`.mmo_ai_record_npc_perception_decision("
        + sql_literal(world_uuid) + ","
        + sql_literal(args.session_uuid) + ","
        + sql_literal(args.character_uuid) + ","
        + sql_literal(args.character_key) + ","
        + sql_literal(args.npc_entity_key) + ","
        + sql_literal(args.target_key) + ","
        + sql_literal(args.content_revision_key) + ","
        + sql_literal("dev:" + args.perception_kind + ":" + args.decision_kind) + ","
        + sql_literal(args.perception_kind) + ","
        + sql_literal(args.decision_kind) + ","
        + str(args.priority) + ","
        + str(args.server_tick) + ","
        + str(args.cooldown_ticks) + ","
        + "1,"
        + payload + ","
        + sql_literal(idem) + ","
        + "@mmo_ai_decision_id,@mmo_ai_decision_status,@mmo_ai_action_queue_id);",
        "SELECT JSON_OBJECT('phase','record','decision_uuid',BIN_TO_UUID(@mmo_ai_decision_id,1),'decision_status',@mmo_ai_decision_status,'created_action_queue_uuid',BIN_TO_UUID(@mmo_ai_action_queue_id,1),'idempotency_key'," + sql_literal(idem) + ");",
        "SET @mmo_ai_claim_action_queue_id=NULL;",
        "SET @mmo_ai_claim_decision_id=NULL;",
        "SET @mmo_ai_claim_action_kind=NULL;",
        "SET @mmo_ai_claim_world_uuid=NULL;",
        "SET @mmo_ai_claim_session_uuid=NULL;",
        "SET @mmo_ai_claim_character_uuid=NULL;",
        "SET @mmo_ai_claim_target_key=NULL;",
        "SET @mmo_ai_claim_idem=NULL;",
        "SET @mmo_ai_claim_payload=NULL;",
        "CALL `" + db + "`.mmo_ai_claim_next_npc_perception_action("
        + sql_literal(args.worker_id)
        + ",@mmo_ai_claim_action_queue_id,@mmo_ai_claim_decision_id,@mmo_ai_claim_action_kind,@mmo_ai_claim_world_uuid,@mmo_ai_claim_session_uuid,@mmo_ai_claim_character_uuid,@mmo_ai_claim_target_key,@mmo_ai_claim_idem,@mmo_ai_claim_payload);",
        "SELECT JSON_OBJECT('phase','claim','claimed_action_queue_uuid',BIN_TO_UUID(@mmo_ai_claim_action_queue_id,1),'claimed_decision_uuid',BIN_TO_UUID(@mmo_ai_claim_decision_id,1),'action_kind',@mmo_ai_claim_action_kind,'target_key',@mmo_ai_claim_target_key,'idempotency_key',@mmo_ai_claim_idem);",
    ]

    if args.finish == "applied":
        sql.extend([
            "SET @mmo_ai_final_status=NULL;",
            "CALL `" + db + "`.mmo_ai_mark_npc_perception_action_applied(@mmo_ai_claim_action_queue_id," + sql_literal(args.worker_id) + ",JSON_OBJECT('simulated',true,'finish','applied'),@mmo_ai_final_status);",
            "SELECT JSON_OBJECT('phase','finish','finish','applied','action_status',@mmo_ai_final_status);",
        ])
    elif args.finish == "failed":
        sql.extend([
            "SET @mmo_ai_final_status=NULL;",
            "CALL `" + db + "`.mmo_ai_mark_npc_perception_action_failed(@mmo_ai_claim_action_queue_id," + sql_literal(args.worker_id) + ",'SIMULATED_FAILURE','Simulated failure from dev tool',0,0,JSON_OBJECT('simulated',true,'finish','failed'),@mmo_ai_final_status);",
            "SELECT JSON_OBJECT('phase','finish','finish','failed','action_status',@mmo_ai_final_status);",
        ])
    elif args.finish == "skipped":
        sql.extend([
            "SET @mmo_ai_final_status=NULL;",
            "CALL `" + db + "`.mmo_ai_skip_npc_perception_action(@mmo_ai_claim_action_queue_id," + sql_literal(args.worker_id) + ",'Simulated skip from dev tool',@mmo_ai_final_status);",
            "SELECT JSON_OBJECT('phase','finish','finish','skipped','action_status',@mmo_ai_final_status);",
        ])

    events = run_mysql(target, "\n".join(sql) + "\n")
    report = {
        "tool": "simulate_ai_npc_perception_dispatch.py",
        "status": "passed",
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "ai_db_name": args.ai_db_name,
        "world_instance_uuid": world_uuid,
        "idempotency_key": idem,
        "events": events,
    }
    if args.output:
        out = Path(args.output)
        if not out.is_absolute():
            out = ROOT / out
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print("artifact=" + str(out))
    print(json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())




