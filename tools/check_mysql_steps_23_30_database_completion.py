#!/usr/bin/env python3
"""Validate Gothic MMO MySQL production DB completion steps 023..030.

The script uses the mysql CLI deliberately, matching earlier project tools and avoiding
an additional Python MySQL dependency.
"""
from __future__ import annotations

import argparse
import dataclasses
import secrets
import subprocess
import sys
from urllib.parse import urlparse, unquote


@dataclasses.dataclass(frozen=True)
class MysqlTarget:
    host: str
    port: int
    user: str
    password: str
    database: str


def parse_mysql_url(url: str) -> MysqlTarget:
    parsed = urlparse(url)
    if parsed.scheme != "mysql":
        raise ValueError("expected mysql:// URL")
    if not parsed.hostname or not parsed.username or not parsed.path.strip("/"):
        raise ValueError("mysql URL must include host, user and database")
    return MysqlTarget(
        host=parsed.hostname,
        port=parsed.port or 3306,
        user=unquote(parsed.username),
        password=unquote(parsed.password or ""),
        database=parsed.path.lstrip("/"),
    )


def mysql_args(target: MysqlTarget) -> list[str]:
    args = [
        "mysql",
        "--batch",
        "--raw",
        "--skip-column-names",
        "--default-character-set=utf8mb4", "--init-command=SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci",
        "-h",
        target.host,
        "-P",
        str(target.port),
        "-u",
        target.user,
    ]
    if target.password:
        args.append(f"-p{target.password}")
    args.append(target.database)
    return args


def run_mysql(target: MysqlTarget, sql: str) -> str:
    proc = subprocess.run(
        mysql_args(target),
        input=sql,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if proc.returncode != 0:
        if proc.stderr:
            sys.stderr.write(proc.stderr)
        raise RuntimeError(f"mysql exited with status {proc.returncode}")
    return proc.stdout.strip()


def sql_literal(value: str | None) -> str:
    if value is None:
        return "NULL"
    return "'" + value.replace("\\", "\\\\").replace("'", "''") + "'"


def scalar(target: MysqlTarget, sql: str) -> str:
    out = run_mysql(target, sql)
    return out.splitlines()[0].split("\t")[0] if out else ""


def row(target: MysqlTarget, sql: str) -> list[str]:
    out = run_mysql(target, sql)
    if not out:
        return []
    return out.splitlines()[0].split("\t")


def require_marker(target: MysqlTarget, migration_key: str, contract: str) -> tuple[bool, str]:
    got = scalar(
        target,
        f"SELECT COALESCE(schema_contract,'') FROM mmo_schema_versions WHERE migration_key={sql_literal(migration_key)};",
    )
    return got == contract, got


def count_missing_tables(target: MysqlTarget, tables: list[str]) -> int:
    values = " UNION ALL ".join(f"SELECT {sql_literal(t)} AS name" for t in tables)
    sql = f"""
    SELECT COUNT(*)
    FROM ({values}) required
    LEFT JOIN information_schema.tables t
      ON t.table_schema = DATABASE() AND t.table_name = required.name
    WHERE t.table_name IS NULL;
    """
    return int(scalar(target, sql) or "0")


def count_missing_views(target: MysqlTarget, views: list[str]) -> int:
    values = " UNION ALL ".join(f"SELECT {sql_literal(v)} AS name" for v in views)
    sql = f"""
    SELECT COUNT(*)
    FROM ({values}) required
    LEFT JOIN information_schema.views v
      ON v.table_schema = DATABASE() AND v.table_name = required.name
    WHERE v.table_name IS NULL;
    """
    return int(scalar(target, sql) or "0")


def count_missing_routines(target: MysqlTarget, routines: list[str]) -> int:
    values = " UNION ALL ".join(f"SELECT {sql_literal(r)} AS name" for r in routines)
    sql = f"""
    SELECT COUNT(*)
    FROM ({values}) required
    LEFT JOIN information_schema.routines r
      ON r.routine_schema = DATABASE() AND r.routine_name = required.name
    WHERE r.routine_name IS NULL;
    """
    return int(scalar(target, sql) or "0")


def print_check(ok: bool, name: str, detail: str) -> bool:
    print(f"[{'OK' if ok else 'FAIL'}] {name}: {detail}")
    return ok


def current_world_uuid(target: MysqlTarget, character_key: str) -> str:
    return scalar(
        target,
        f"""
        SELECT BIN_TO_UUID(c.current_world_instance_id,1)
        FROM characters c
        WHERE c.character_key={sql_literal(character_key)}
        LIMIT 1;
        """,
    )


def run_smoke(target: MysqlTarget, account_name: str, character_key: str) -> list[bool]:
    checks: list[bool] = []
    suffix = secrets.token_hex(8)
    world_uuid = current_world_uuid(target, character_key)
    checks.append(print_check(bool(world_uuid), "current world", world_uuid or "missing"))
    if not world_uuid:
        return checks

    projection = row(
        target,
        f"""
        SET @world_id=UUID_TO_BIN({sql_literal(world_uuid)},1);
        SET @run_id=NULL; SET @component_count=NULL;
        CALL mmo_materialize_projection_hash_run(@world_id, {sql_literal(character_key)}, {sql_literal('smoke:23-30:projection:' + suffix)}, JSON_OBJECT('source','steps-23-30-smoke'), @run_id, @component_count);
        SELECT BIN_TO_UUID(@run_id,1), @component_count;
        """,
    )
    component_count = int(projection[1]) if len(projection) > 1 and projection[1] not in ("", "NULL") else 0
    checks.append(print_check(component_count >= 12, "projection hash manifest", f"{projection[0] if projection else 'NULL'}/components={component_count}"))

    integrity = row(
        target,
        f"""
        SET @world_id=UUID_TO_BIN({sql_literal(world_uuid)},1);
        SET @run_id=NULL; SET @errors=NULL; SET @warnings=NULL;
        CALL mmo_run_final_database_integrity_audit(@world_id, {sql_literal('final-db:smoke:23-30:' + suffix)}, JSON_OBJECT('source','steps-23-30-smoke'), @run_id, @errors, @warnings);
        SELECT BIN_TO_UUID(@run_id,1), @errors, @warnings;
        """,
    )
    errors = int(integrity[1]) if len(integrity) > 1 and integrity[1] not in ("", "NULL") else 999
    warnings = int(integrity[2]) if len(integrity) > 2 and integrity[2] not in ("", "NULL") else 0
    checks.append(print_check(errors == 0, "final DB integrity audit", f"{integrity[0] if integrity else 'NULL'}/errors={errors}/warnings={warnings}"))

    manifest = row(
        target,
        f"""
        SET @world_id=UUID_TO_BIN({sql_literal(world_uuid)},1);
        SET @manifest_id=NULL; SET @status=NULL; SET @db_errors=NULL; SET @external_blockers=NULL;
        CALL mmo_create_db_restore_manifest(@world_id, {sql_literal(character_key)}, {sql_literal('smoke:23-30:restore-manifest:' + suffix)}, JSON_OBJECT('source','steps-23-30-smoke'), @manifest_id, @status, @db_errors, @external_blockers);
        SELECT BIN_TO_UUID(@manifest_id,1), @status, @db_errors, @external_blockers;
        """,
    )
    db_errors = int(manifest[2]) if len(manifest) > 2 and manifest[2] not in ("", "NULL") else 999
    checks.append(print_check(db_errors == 0, "DB restore manifest", "/".join(manifest) if manifest else "missing"))

    backup_hash = secrets.token_hex(32)
    backup = row(
        target,
        f"""
        SET @world_id=UUID_TO_BIN({sql_literal(world_uuid)},1);
        SET @backup_id=NULL;
        CALL mmo_record_database_backup_manifest(@world_id, {sql_literal('smoke:23-30:backup:' + suffix)}, 'diagnostic', NULL, {sql_literal(backup_hash)}, 30, 0, 'recorded', JSON_OBJECT('source','steps-23-30-smoke'), @backup_id);
        SELECT BIN_TO_UUID(@backup_id,1);
        """,
    )
    checks.append(print_check(bool(backup and backup[0]), "backup manifest", backup[0] if backup else "missing"))

    completion = row(
        target,
        f"""
        SET @world_id=UUID_TO_BIN({sql_literal(world_uuid)},1);
        SET @run_id=NULL; SET @db_status=NULL; SET @mmo_status=NULL; SET @db_blockers=NULL; SET @external_blockers=NULL; SET @warnings=NULL;
        CALL mmo_evaluate_database_completion(@world_id, {sql_literal(character_key)}, {sql_literal('smoke:23-30:completion:' + suffix)}, JSON_OBJECT('source','steps-23-30-smoke','account_name',{sql_literal(account_name)}), @run_id, @db_status, @mmo_status, @db_blockers, @external_blockers, @warnings);
        SELECT BIN_TO_UUID(@run_id,1), @db_status, @mmo_status, @db_blockers, @external_blockers, @warnings;
        """,
    )
    db_status = completion[1] if len(completion) > 1 else "missing"
    mmo_status = completion[2] if len(completion) > 2 else "missing"
    db_blockers = int(completion[3]) if len(completion) > 3 and completion[3] not in ("", "NULL") else 999
    checks.append(print_check(db_status == "complete" and db_blockers == 0, "database completion evaluator", "/".join(completion) if completion else "missing"))
    checks.append(print_check(mmo_status in {"blocked", "yellow", "green"}, "MMO external gate status", mmo_status))

    if db_status == "complete" and mmo_status == "blocked":
        print("[OK] interpretation: MySQL database layer is complete; external C++/parity/server-authority gates still block full MMO readiness")

    return checks


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--url", required=True)
    parser.add_argument("--account-name", default="local-import")
    parser.add_argument("--character-key", default="PC_HERO")
    parser.add_argument("--run-smoke", action="store_true")
    args = parser.parse_args(argv)

    target = parse_mysql_url(args.url)
    checks: list[bool] = []

    markers = [
        ("production/mysql/023_database_completion_registry", "gothic-mmo-database-completion-registry-v1-mysql"),
        ("production/mysql/024_projection_hash_manifest", "gothic-mmo-projection-hash-manifest-v1-mysql"),
        ("production/mysql/025_final_database_integrity_audit", "gothic-mmo-final-database-integrity-audit-v1-mysql"),
        ("production/mysql/026_db_restore_manifest_gate", "gothic-mmo-db-restore-manifest-gate-v1-mysql"),
        ("production/mysql/027_database_ops_backup_manifest", "gothic-mmo-database-ops-backup-manifest-v1-mysql"),
        ("production/mysql/028_final_read_models", "gothic-mmo-final-read-models-v1-mysql"),
        ("production/mysql/029_external_integration_gates", "gothic-mmo-external-integration-gates-v1-mysql"),
        ("production/mysql/030_database_completion_evaluator", "gothic-mmo-database-completion-evaluator-v1-mysql"),
    ]
    for key, contract in markers:
        ok, got = require_marker(target, key, contract)
        checks.append(print_check(ok, f"migration {key.split('/')[-1].split('_')[0]} marker", got or "missing"))

    tables = [
        "mmo_database_completion_requirements",
        "mmo_database_completion_runs",
        "mmo_database_completion_results",
        "mmo_projection_hash_runs",
        "mmo_projection_component_hashes",
        "mmo_db_restore_manifests",
        "mmo_database_backup_manifests",
        "mmo_database_retention_policies",
        "mmo_external_integration_gates",
    ]
    missing = count_missing_tables(target, tables)
    checks.append(print_check(missing == 0, "steps 023..030 tables", f"{len(tables)-missing}/{len(tables)}"))

    views = [
        "v_mmo_database_completion_requirements",
        "v_mmo_database_completion_latest",
        "v_mmo_database_completion_blockers",
        "v_projection_hash_latest",
        "v_projection_hash_latest_components",
        "v_final_database_integrity_latest",
        "v_final_database_integrity_latest_errors",
        "v_db_restore_manifests",
        "v_db_restore_manifest_latest",
        "v_database_backup_manifests",
        "v_database_ops_dashboard",
        "v_mmo_character_load_sheet_final",
        "v_mmo_world_state_summary_final",
        "v_mmo_database_final_dashboard",
        "v_external_integration_gates",
        "v_external_integration_blockers",
        "v_external_integration_summary",
        "v_mmo_database_done_summary",
        "v_mmo_database_remaining_work_final",
    ]
    missing = count_missing_views(target, views)
    checks.append(print_check(missing == 0, "steps 023..030 views", f"{len(views)-missing}/{len(views)}"))

    routines = [
        "mmo_materialize_projection_hash_run",
        "mmo_run_final_database_integrity_audit",
        "mmo_create_db_restore_manifest",
        "mmo_record_database_backup_manifest",
        "mmo_set_external_integration_gate_status",
        "mmo_evaluate_database_completion",
    ]
    missing = count_missing_routines(target, routines)
    checks.append(print_check(missing == 0, "steps 023..030 routines", f"{len(routines)-missing}/{len(routines)}"))

    if args.run_smoke:
        checks.extend(run_smoke(target, args.account_name, args.character_key))

    print("\nHint: apply migrations 023..030 after 022. A blocked MMO status is expected until C++ hooks, real parity runs and server-authority code exist.")
    return 0 if all(checks) else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
