#!/usr/bin/env python3
"""Generate Step206 import jobs from content inventory and archive plans."""
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from urllib.parse import unquote, urlparse

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

from pathlib import Path as _MysqlCliPath

_MYSQL_CLI_TOOLS_DIR = _MysqlCliPath(__file__).resolve().parents[1]
if str(_MYSQL_CLI_TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(_MYSQL_CLI_TOOLS_DIR))
try:
    from _mysql_cli import resolve_mysql_exe
except Exception:  # pragma: no cover - fallback for standalone patch bundles.
    def resolve_mysql_exe() -> str | None:
        return shutil.which("mysql")


ROOT = Path(__file__).resolve().parents[2]


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
    database = (parsed.path or "/").lstrip("/")
    if not database:
        raise ValueError("database missing in MySQL URL")
    return Target(
        host=parsed.hostname or "localhost",
        port=parsed.port or 3306,
        user=unquote(parsed.username or ""),
        password=unquote(parsed.password or ""),
        database=database,
    )


def mysql_cmd(target: Target) -> list[str]:
    exe = resolve_mysql_exe()
    if exe is None:
        raise RuntimeError("mysql executable not found in PATH")
    cmd = [
        exe,
        "--default-character-set=utf8mb4",
        "--init-command=SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci",
        "--batch",
        "--raw",
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


def redact_cmd(cmd: list[str]) -> list[str]:
    return ["-p***" if item.startswith("-p") and len(item) > 2 else item for item in cmd]


def sql_literal(value: object) -> str:
    if value is None:
        return "NULL"
    text = str(value)
    return "'" + text.replace("\\", "\\\\").replace("'", "''").replace("\0", "") + "'"


def sql_json(value: object) -> str:
    text = json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
    return "CAST(" + sql_literal(text) + " AS JSON)"


def rel(path: Path) -> str:
    try:
        return path.resolve().relative_to(ROOT.resolve()).as_posix()
    except ValueError:
        return str(path)


def normalize_path(value: object) -> str:
    return str(value or "").replace("\\", "/").lower().strip("/")


def load_json(path: Path) -> dict[str, object]:
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise ValueError(f"expected JSON object: {path}")
    return data


def importer_for_role(role: str) -> tuple[str, str, int] | None:
    if role == "world_zen":
        return ("zen_world_import", "world_zen", 10)
    if role == "scripts_dat":
        return ("daedalus_dat_index", "scripts_dat", 20)
    if role == "dialog_ou":
        return ("dialog_ou_index", "dialog_ou", 30)
    return None


def build_jobs(inventory: dict[str, object], archive_plan: dict[str, object] | None) -> list[dict[str, object]]:
    jobs: list[dict[str, object]] = []

    if archive_plan is not None:
        for raw in archive_plan.get("archive_plans", []):
            if not isinstance(raw, dict):
                continue
            path = normalize_path(raw.get("archive_logical_path"))
            if not path:
                continue
            status = str(raw.get("mount_status") or "planned")
            jobs.append({
                "importer_key": "archive_mount_verify",
                "source_kind": "archive",
                "source_logical_path": path,
                "source_sha256": raw.get("archive_sha256"),
                "job_priority": 5,
                "job_status": "queued" if status in {"mounted", "extracted", "verified"} else "blocked",
                "input_payload": {
                    "schema": "mmo.server_content_import_job_input.v1",
                    "archive_role": raw.get("archive_role"),
                    "mount_strategy": raw.get("mount_strategy"),
                    "mount_status": status,
                    "extracted_root_label": raw.get("extracted_root_label"),
                    "extracted_manifest_hash": raw.get("extracted_manifest_hash"),
                },
            })

    for raw in inventory.get("items", []):
        if not isinstance(raw, dict):
            continue
        role = str(raw.get("file_role") or "")
        importer = importer_for_role(role)
        if importer is None:
            continue
        importer_key, source_kind, priority = importer
        path = normalize_path(raw.get("logical_path"))
        if not path:
            continue
        jobs.append({
            "importer_key": importer_key,
            "source_kind": source_kind,
            "source_logical_path": path,
            "source_sha256": raw.get("sha256"),
            "job_priority": priority,
            "job_status": "queued",
            "input_payload": {
                "schema": "mmo.server_content_import_job_input.v1",
                "file_role": role,
                "loader_stage": raw.get("loader_stage"),
                "import_status": raw.get("import_status"),
                "byte_size": raw.get("byte_size", 0),
            },
        })

    return sorted(jobs, key=lambda item: (int(item["job_priority"]), str(item["source_logical_path"])))


def summarize(jobs: list[dict[str, object]]) -> dict[str, object]:
    by_importer: dict[str, int] = {}
    by_status: dict[str, int] = {}
    by_source_kind: dict[str, int] = {}
    for job in jobs:
        importer = str(job.get("importer_key") or "")
        status = str(job.get("job_status") or "queued")
        source_kind = str(job.get("source_kind") or "")
        by_importer[importer] = by_importer.get(importer, 0) + 1
        by_status[status] = by_status.get(status, 0) + 1
        by_source_kind[source_kind] = by_source_kind.get(source_kind, 0) + 1
    return {
        "job_count": len(jobs),
        "by_importer": dict(sorted(by_importer.items())),
        "by_source_kind": dict(sorted(by_source_kind.items())),
        "by_status": dict(sorted(by_status.items())),
    }


def build_sql(content_revision_key: str, jobs: list[dict[str, object]]) -> str:
    lines = [
        "SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci;",
        "SET @mmo_content_import_job_id=NULL;",
    ]
    for job in jobs:
        lines.append(
            "CALL mmo_enqueue_server_content_import_job("
            + sql_literal(content_revision_key) + ","
            + sql_literal(job.get("importer_key")) + ","
            + sql_literal(job.get("source_kind")) + ","
            + sql_literal(job.get("source_logical_path")) + ","
            + sql_literal(job.get("source_sha256")) + ","
            + str(int(job.get("job_priority") or 1000)) + ","
            + sql_literal(job.get("job_status")) + ","
            + sql_json(job.get("input_payload") or {}) + ","
            + "@mmo_content_import_job_id);"
        )
    lines.append("SELECT COUNT(*) AS content_import_job_count FROM mmo_server_content_import_jobs;")
    return "\n".join(lines) + "\n"


def run_mysql_file(target: Target, sql_path: Path, dry_run: bool) -> dict[str, object]:
    cmd = mysql_cmd(target)
    shown = redact_cmd(cmd)
    if dry_run:
        return {"status": "dry_run", "cmd": shown, "stdout": "", "stderr": "", "returncode": 0}
    proc = subprocess.run(
        cmd,
        input=sql_path.read_text(encoding="utf-8"),
        text=True,
        encoding="utf-8",
        errors="replace",
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        cwd=str(ROOT),
    )
    return {
        "status": "applied" if proc.returncode == 0 else "failed",
        "returncode": proc.returncode,
        "cmd": shown,
        "stdout": proc.stdout,
        "stderr": proc.stderr,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate server content import jobs from inventory/archive plans.")
    parser.add_argument("--inventory", required=True, help="Inventory JSON generated by analyze_server_content_pack_inventory.py")
    parser.add_argument("--archive-plan", default="", help="Optional archive mount plan JSON generated by plan_server_content_archive_mounts.py")
    parser.add_argument("--content-revision-key", default="", help="Override content revision key.")
    parser.add_argument("--url", default="", help="mysql://user:password@host:port/database. If omitted, only artifacts are written.")
    parser.add_argument("--output", default="runtime/step207_server_content_import_jobs/import_jobs.json")
    parser.add_argument("--sql-output", default="runtime/step207_server_content_import_jobs/enqueue_import_jobs.sql")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    inventory_path = Path(args.inventory)
    if not inventory_path.is_absolute():
        inventory_path = ROOT / inventory_path
    inventory = load_json(inventory_path)

    archive_plan = None
    archive_plan_path = None
    if args.archive_plan:
        archive_plan_path = Path(args.archive_plan)
        if not archive_plan_path.is_absolute():
            archive_plan_path = ROOT / archive_plan_path
        archive_plan = load_json(archive_plan_path)

    content_revision_key = args.content_revision_key or str(inventory.get("content_revision_key") or "")
    if not content_revision_key:
        raise SystemExit("content revision key missing; pass --content-revision-key or use inventory with content_revision_key")

    jobs = build_jobs(inventory, archive_plan)
    report = {
        "schema": "mmo.server_content_import_jobs.v1",
        "tool": "enqueue_server_content_import_jobs.py",
        "content_revision_key": content_revision_key,
        "manifest_hash": inventory.get("manifest_hash"),
        "inventory_path": rel(inventory_path),
        "archive_plan_path": rel(archive_plan_path) if archive_plan_path else "",
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "summary": summarize(jobs),
        "jobs": jobs,
    }

    output_path = Path(args.output)
    if not output_path.is_absolute():
        output_path = ROOT / output_path
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    sql_path = Path(args.sql_output)
    if not sql_path.is_absolute():
        sql_path = ROOT / sql_path
    sql_path.parent.mkdir(parents=True, exist_ok=True)
    sql_path.write_text(build_sql(content_revision_key, jobs), encoding="utf-8")

    result: dict[str, object] = {
        "status": "generated",
        "summary": report["summary"],
        "output": rel(output_path),
        "sql_output": rel(sql_path),
    }
    if args.url:
        mysql_result = run_mysql_file(parse_mysql_url(args.url), sql_path, args.dry_run)
        result["mysql"] = mysql_result
        result["status"] = mysql_result["status"]
        if mysql_result.get("stdout"):
            print(mysql_result["stdout"], end="")
        if mysql_result.get("stderr"):
            print(mysql_result["stderr"], file=sys.stderr, end="")

    print(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True))
    return 0 if result["status"] in {"generated", "applied", "dry_run"} else 1


if __name__ == "__main__":
    raise SystemExit(main())
