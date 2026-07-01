#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from urllib.parse import unquote, urlparse


ROOT = Path(__file__).resolve().parents[1]
WORKER = ROOT / "tools" / "run_mmo_resolved_action_worker.py"
SNAPSHOT_EXPORTER = ROOT / "tools" / "export_mmo_pc_hero_test_restore_snapshot.py"
ROUNDTRIP_CHECKER = ROOT / "tools" / "check_mmo_step69_pc_hero_test_inventory_roundtrip.py"
DEFAULT_OUTPUT_ROOT = ROOT / "runtime" / "live_resolved_worker"
DEFAULT_LIVE_SESSION_KEY = "local-dev-PC_HERO_TEST"
DEFAULT_CHARACTER_KEY = "PC_HERO"


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


def run_mysql(target: Target, sql: str) -> str:
    proc = subprocess.run(mysql_cmd(target) + ["--execute", sql], text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    if proc.returncode != 0:
        if proc.stderr:
            print(proc.stderr, file=sys.stderr, end="")
        raise RuntimeError(f"mysql exited with status {proc.returncode}")
    return proc.stdout.strip()


def sql_literal(value: object) -> str:
    if value is None:
        return "NULL"
    text = str(value)
    return "'" + text.replace("\\", "\\\\").replace("'", "''") + "'"


def sanitize_filename(value: str) -> str:
    text = re.sub(r"[^A-Za-z0-9_.-]+", "_", value.strip())
    return text[:160] or "auto-session"


def session_counts(target: Target, session_key: str) -> dict[str, int]:
    raw = run_mysql(
        target,
        f"""
        SELECT status, COUNT(*)
          FROM mmo_server_action_outbox
         WHERE idempotency_key LIKE {sql_literal(session_key + ':%')}
         GROUP BY status;
        """,
    )
    counts: dict[str, int] = {}
    for line in raw.splitlines():
        parts = line.split("\t")
        if len(parts) >= 2:
            counts[parts[0]] = int(parts[1])
    return counts


def pendingish_count(counts: dict[str, int]) -> int:
    return counts.get("pending", 0) + counts.get("claimed", 0) + counts.get("failed", 0) + counts.get("dead_letter", 0)


def rows(raw: str) -> list[list[str]]:
    return [line.split("\t") for line in raw.splitlines() if line.strip()]


def session_action_summary(target: Target, session_key: str) -> dict[str, object]:
    prefix = session_key + ":%"
    raw = run_mysql(
        target,
        f"""
        SELECT action_kind,
               status,
               COALESCE(JSON_UNQUOTE(JSON_EXTRACT(result_payload,'$.response_kind')), ''),
               COUNT(*) AS c
         FROM mmo_server_action_outbox
         WHERE idempotency_key LIKE {sql_literal(prefix)}
         GROUP BY action_kind, status, COALESCE(JSON_UNQUOTE(JSON_EXTRACT(result_payload,'$.response_kind')), '')
         ORDER BY 1, 2, 3;
        """,
    )
    result_rows = []
    status_totals: dict[str, int] = {}
    action_totals: dict[str, int] = {}
    response_kind_totals: dict[str, int] = {}
    for row in rows(raw):
        row = row + [""] * 4
        count = int(row[3] or 0)
        result_rows.append({"action_kind": row[0], "status": row[1], "response_kind": row[2] or None, "count": count})
        status_totals[row[1]] = status_totals.get(row[1], 0) + count
        action_totals[row[0]] = action_totals.get(row[0], 0) + count
        if row[2]:
            response_kind_totals[row[2]] = response_kind_totals.get(row[2], 0) + count

    failures_raw = run_mysql(
        target,
        f"""
        SELECT action_kind, status, COALESCE(last_error_code,''), LEFT(COALESCE(last_error_message,''),240), idempotency_key
          FROM mmo_server_action_outbox
         WHERE idempotency_key LIKE {sql_literal(prefix)}
           AND status IN ('failed','dead_letter')
         ORDER BY failed_at DESC, requested_at DESC
         LIMIT 20;
        """,
    )
    failures = []
    for row in rows(failures_raw):
        row = row + [""] * 5
        failures.append({"action_kind": row[0], "status": row[1], "error_code": row[2], "error_message": row[3], "idempotency_key": row[4]})

    return {
        "session_key": session_key,
        "status_totals": status_totals,
        "action_totals": action_totals,
        "response_kind_totals": response_kind_totals,
        "rows": result_rows,
        "failures": failures,
    }


def inventory_evidence_summary(target: Target, session_key: str) -> dict[str, object]:
    prefix = session_key + ":%"
    events_raw = run_mysql(
        target,
        f"""
        SELECT event_type, event_class, COUNT(*) AS c
          FROM world_event_journal
         WHERE idempotency_key LIKE {sql_literal(prefix)}
           AND event_class IN ('inventory','equipment','world_entity')
         GROUP BY event_type, event_class
         ORDER BY event_type, event_class;
        """,
    )
    audits_raw = run_mysql(
        target,
        f"""
        SELECT audit_type, COUNT(*) AS c
          FROM world_item_audit
         WHERE idempotency_key LIKE {sql_literal(prefix)}
         GROUP BY audit_type
         ORDER BY audit_type;
        """,
    )
    return {
        "events": [{"event_type": r[0], "event_class": r[1], "count": int(r[2] or 0)} for r in (row + [""] * 3 for row in rows(events_raw))],
        "world_item_audit": [{"audit_type": r[0], "count": int(r[1] or 0)} for r in (row + [""] * 2 for row in rows(audits_raw))],
    }


def run_capture(cmd: list[str]) -> dict[str, object]:
    proc = subprocess.run(cmd, cwd=str(ROOT), text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    if proc.stdout:
        print(proc.stdout, end="")
    if proc.stderr:
        print(proc.stderr, file=sys.stderr, end="")
    return {
        "cmd": redacted_cmd(cmd),
        "returncode": proc.returncode,
        "stdout_tail": proc.stdout[-4000:],
        "stderr_tail": proc.stderr[-4000:],
    }


def post_run_artifacts(target: Target, url: str, session_key: str, character_key: str, output_dir: Path, worker_returncode: int) -> dict[str, object]:
    summary: dict[str, object] = {
        "step": "69_pc_hero_test_inventory_roundtrip_post_run",
        "created_at": datetime.now(timezone.utc).isoformat(),
        "session_key": session_key,
        "character_key": character_key,
        "worker_returncode": worker_returncode,
        "action_summary": session_action_summary(target, session_key),
        "inventory_evidence_summary": inventory_evidence_summary(target, session_key),
        "snapshot": None,
        "roundtrip_check": None,
    }

    snapshot_path = output_dir / "mysql_restore_snapshot.json"
    if SNAPSHOT_EXPORTER.exists():
        cmd = [
            sys.executable,
            str(SNAPSHOT_EXPORTER),
            "--url",
            url,
            "--session-key",
            session_key,
            "--character-key",
            character_key,
            "--output",
            str(snapshot_path),
        ]
        summary["snapshot"] = run_capture(cmd)

    check_path = output_dir / "inventory_roundtrip_check.json"
    if ROUNDTRIP_CHECKER.exists():
        cmd = [
            sys.executable,
            str(ROUNDTRIP_CHECKER),
            "--url",
            url,
            "--session-key",
            session_key,
            "--character-key",
            character_key,
            "--snapshot",
            str(snapshot_path),
            "--output",
            str(check_path),
        ]
        summary["roundtrip_check"] = run_capture(cmd)

    summary_path = output_dir / "live_worker_post_run_summary.json"
    summary_path.write_text(json.dumps(summary, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"post_run_summary={summary_path}")
    return summary


def latest_active_session_key(target: Target) -> tuple[str, dict[str, int]]:
    raw = run_mysql(
        target,
        """
        SELECT SUBSTRING_INDEX(idempotency_key, ':', 1) AS session_key,
               SUM(status='pending') AS pending_count,
               SUM(status='claimed') AS claimed_count,
               SUM(status='failed') AS failed_count,
               SUM(status='dead_letter') AS dead_letter_count,
               SUM(status='applied') AS applied_count,
               COUNT(*) AS total_count,
               MAX(requested_at) AS last_requested_at
          FROM mmo_server_action_outbox
         WHERE idempotency_key IS NOT NULL
           AND idempotency_key <> ''
         GROUP BY SUBSTRING_INDEX(idempotency_key, ':', 1)
        HAVING pending_count + claimed_count + failed_count + dead_letter_count > 0
         ORDER BY pending_count DESC,
                  claimed_count DESC,
                  failed_count DESC,
                  last_requested_at DESC
         LIMIT 1;
        """,
    )
    if not raw:
        return "", {}
    parts = raw.splitlines()[-1].split("\t")
    if len(parts) < 8:
        return "", {}
    counts = {
        "pending": int(parts[1] or 0),
        "claimed": int(parts[2] or 0),
        "failed": int(parts[3] or 0),
        "dead_letter": int(parts[4] or 0),
        "applied": int(parts[5] or 0),
        "total": int(parts[6] or 0),
    }
    return parts[0], counts


def worker_supported_flags() -> set[str]:
    proc = subprocess.run([sys.executable, str(WORKER), "--help"], cwd=str(ROOT), text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
    help_text = proc.stdout
    return set(re.findall(r"(--[a-zA-Z0-9][a-zA-Z0-9-]*)", help_text))


def add_if_supported(cmd: list[str], supported: set[str], flag: str, *values: str) -> None:
    if flag not in supported:
        return
    cmd.append(flag)
    cmd.extend(values)


def redacted_cmd(cmd: list[str]) -> list[str]:
    out: list[str] = []
    previous = ""
    for part in cmd:
        if previous in {"--url", "--mysql-url"}:
            out.append(redact_mysql_url(part))
        elif part.startswith("mysql://") or part.startswith("mysql+pymysql://"):
            out.append(redact_mysql_url(part))
        else:
            out.append(part)
        previous = part
    return out


def redact_mysql_url(value: str) -> str:
    try:
        p = urlparse(value)
        if p.scheme not in {"mysql", "mysql+pymysql"}:
            return value
        user = p.username or ""
        host = p.hostname or ""
        port = f":{p.port}" if p.port else ""
        auth = f"{user}:***@" if user else "***@"
        return f"{p.scheme}://{auth}{host}{port}{p.path}"
    except Exception:
        return value


def run_child(cmd: list[str]) -> int:
    proc = subprocess.Popen(cmd, cwd=str(ROOT))
    try:
        return proc.wait()
    except KeyboardInterrupt:
        print("\n[STOP] Ctrl+C received; stopping live resolved worker...", file=sys.stderr, flush=True)
        try:
            proc.wait(timeout=5)
            return 130
        except subprocess.TimeoutExpired:
            proc.terminate()
            try:
                proc.wait(timeout=5)
                return 130
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
                return 130


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description="Run the resolved MMO worker with live defaults and stable artifact paths.")
    ap.add_argument("--url", required=True)
    ap.add_argument("--session-key", default=os.environ.get("GOTHIC_MMO_LIVE_SESSION_KEY", DEFAULT_LIVE_SESSION_KEY), help="Idempotency prefix. Default: local-dev-PC_HERO_TEST.")
    ap.add_argument("--max-actions", type=int, default=500)
    ap.add_argument("--worker-id", default="dev-live-resolved-worker")
    ap.add_argument("--output-root", default=str(DEFAULT_OUTPUT_ROOT))
    ap.add_argument("--character-key", default=DEFAULT_CHARACTER_KEY)
    ap.add_argument("--reset-matching-failed", action="store_true")
    ap.add_argument("--strict-session-key", action="store_true", help="Do not auto-switch when the given session has no pending/failed rows.")
    ap.add_argument("--skip-post-run-artifacts", action="store_true", help="Do not write Step69 post-run summary/check/snapshot artifacts.")
    ap.add_argument("--dry-run", action="store_true", help="Print chosen session, artifact paths and worker command without claiming rows.")
    args = ap.parse_args(argv)

    target = parse_mysql_url(args.url)
    explicit_session_key = any(arg == "--session-key" or arg.startswith("--session-key=") for arg in argv)
    selected = args.session_key.strip()
    selected_counts: dict[str, int] = {}

    if selected:
        selected_counts = session_counts(target, selected)
        if pendingish_count(selected_counts) == 0 and not args.strict_session_key:
            auto_key, auto_counts = latest_active_session_key(target)
            if explicit_session_key and auto_key and auto_key != selected:
                print(
                    f"[AUTO-SESSION] requested={selected} has no pending/failed rows; using {auto_key} "
                    f"counts={json.dumps(auto_counts, sort_keys=True)}"
                )
                selected = auto_key
                selected_counts = auto_counts
            elif auto_key and auto_key != selected:
                print(
                    f"[PC_HERO_TEST] preferred session {selected} has no pending/failed rows; "
                    f"active other session exists: {auto_key} counts={json.dumps(auto_counts, sort_keys=True)}"
                )
                print(f"[PC_HERO_TEST] use --session-key {auto_key} only if you intentionally want to drain old rows")
    else:
        selected, selected_counts = latest_active_session_key(target)
        if selected:
            print(f"[AUTO-SESSION] using {selected} counts={json.dumps(selected_counts, sort_keys=True)}")

    if not selected or pendingish_count(selected_counts) == 0:
        print(f"ERROR: no pending/failed live work found for session {selected or DEFAULT_LIVE_SESSION_KEY}", file=sys.stderr)
        return 2

    output_dir = Path(args.output_root)
    if not output_dir.is_absolute():
        output_dir = ROOT / output_dir
    output_dir = output_dir / sanitize_filename(selected)
    output_dir.mkdir(parents=True, exist_ok=True)

    supported = worker_supported_flags()
    cmd = [
        sys.executable,
        str(WORKER),
        "--url",
        args.url,
        "--session-key",
        selected,
        "--max-actions",
        str(max(0, args.max_actions)),
        "--worker-id",
        args.worker_id,
        "--continue-on-error",
    ]
    if args.reset_matching_failed:
        cmd.append("--reset-matching-failed")

    add_if_supported(cmd, supported, "--enable-movement-authority-gate")
    add_if_supported(cmd, supported, "--bootstrap-ack-jsonl", str(output_dir / "bootstrap_acks.jsonl"))
    add_if_supported(cmd, supported, "--bootstrap-manifest-output", str(output_dir / "bootstrap_ack.json"))
    add_if_supported(cmd, supported, "--checkpoint-ack-jsonl", str(output_dir / "checkpoint_acks.jsonl"))
    add_if_supported(cmd, supported, "--checkpoint-ack-output", str(output_dir / "checkpoint_ack.json"))
    add_if_supported(cmd, supported, "--movement-authority-jsonl", str(output_dir / "movement_authority_acks.jsonl"))
    add_if_supported(cmd, supported, "--movement-authority-output", str(output_dir / "movement_authority_ack.json"))
    add_if_supported(cmd, supported, "--pickup-ack-jsonl", str(output_dir / "pickup_acks.jsonl"))
    add_if_supported(cmd, supported, "--pickup-ack-output", str(output_dir / "pickup_ack.json"))
    add_if_supported(cmd, supported, "--equipment-ack-jsonl", str(output_dir / "equipment_acks.jsonl"))
    add_if_supported(cmd, supported, "--equipment-ack-output", str(output_dir / "equipment_ack.json"))

    manifest = {
        "step": "62_pc_hero_test_live_profile",
        "created_at": datetime.now(timezone.utc).isoformat(),
        "session_key": selected,
        "character_key": args.character_key,
        "session_counts_before": selected_counts,
        "output_dir": str(output_dir),
        "worker_command": redacted_cmd(cmd),
        "post_run_artifacts": not args.skip_post_run_artifacts,
        "supported_response_flags": sorted(flag for flag in supported if flag.endswith("-jsonl") or flag.endswith("-output")),
    }
    manifest_path = output_dir / "live_worker_profile_manifest.json"
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    print(f"session_key={selected}")
    print(f"artifact_dir={output_dir}")
    print(f"manifest={manifest_path}")
    print("[RUN] " + " ".join(redacted_cmd(cmd)))

    if args.dry_run:
        return 0
    worker_rc = run_child(cmd)
    if not args.skip_post_run_artifacts:
        try:
            post_run_artifacts(target, args.url, selected, args.character_key, output_dir, worker_rc)
        except Exception as exc:  # noqa: BLE001 - post-run report must not hide the worker result
            print(f"ERROR: Step69 post-run artifacts failed: {exc}", file=sys.stderr)
            if worker_rc == 0:
                return 1
    return worker_rc


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))

