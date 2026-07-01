#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from urllib.parse import urlparse


ROOT = Path(__file__).resolve().parents[1]
RECEIVER = ROOT / "tools" / "run_mmo_action_receiver.py"
DEFAULT_SESSION_KEY = "local-dev-PC_HERO_TEST"
DEFAULT_CHARACTER_KEY = "PC_HERO"
DEFAULT_ACCOUNT_NAME = "local-import"
DEFAULT_RUNTIME_DIR = ROOT / "runtime" / "pc_hero_test_live"


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


def run_child(cmd: list[str]) -> int:
    proc = subprocess.Popen(cmd, cwd=str(ROOT))
    try:
        return proc.wait()
    except KeyboardInterrupt:
        print("\n[STOP] Ctrl+C received; stopping PC_HERO_TEST receiver...", file=sys.stderr, flush=True)
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
    ap = argparse.ArgumentParser(description="Run the dev UDP receiver with the stable PC_HERO_TEST live profile.")
    ap.add_argument("--url", default=os.environ.get("GOTHIC_MMO_MYSQL_URL", ""), help="mysql://user:password@host:port/database")
    ap.add_argument("--bind", default="127.0.0.1:29777")
    ap.add_argument("--runtime-dir", default=str(DEFAULT_RUNTIME_DIR))
    ap.add_argument("--session-key", default=DEFAULT_SESSION_KEY)
    ap.add_argument("--account-name", default=DEFAULT_ACCOUNT_NAME)
    ap.add_argument("--character-key", default=DEFAULT_CHARACTER_KEY)
    ap.add_argument("--no-truncate", action="store_true", help="Keep existing receiver JSONL files instead of truncating on start.")
    ap.add_argument("--dry-run", action="store_true", help="Print command and manifest without starting the receiver.")
    args = ap.parse_args(argv)

    if not args.url:
        print("ERROR: provide --url or set GOTHIC_MMO_MYSQL_URL", file=sys.stderr)
        return 2

    runtime_dir = Path(args.runtime_dir)
    if not runtime_dir.is_absolute():
        runtime_dir = ROOT / runtime_dir
    runtime_dir.mkdir(parents=True, exist_ok=True)

    accepted_jsonl = runtime_dir / "server_actions.jsonl"
    rejected_jsonl = runtime_dir / "server_rejects.jsonl"
    manifest_path = runtime_dir / "receiver_profile_manifest.json"

    cmd = [
        sys.executable,
        str(RECEIVER),
        "--bind",
        args.bind,
        "--jsonl",
        str(accepted_jsonl),
        "--reject-jsonl",
        str(rejected_jsonl),
        "--require-session",
        args.session_key,
        "--mysql-url",
        args.url,
        "--account-name",
        args.account_name,
        "--character-key",
        args.character_key,
        "--db-session-key",
        args.session_key,
        "--enqueue-outbox",
    ]
    if not args.no_truncate:
        cmd.append("--truncate")

    manifest = {
        "step": "62_pc_hero_test_live_profile",
        "created_at": datetime.now(timezone.utc).isoformat(),
        "bind": args.bind,
        "session_key": args.session_key,
        "account_name": args.account_name,
        "character_key": args.character_key,
        "accepted_jsonl": str(accepted_jsonl),
        "rejected_jsonl": str(rejected_jsonl),
        "receiver_command": redacted_cmd(cmd),
    }
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    print(f"session_key={args.session_key}")
    print(f"artifact_dir={runtime_dir}")
    print(f"manifest={manifest_path}")
    print("[RUN] " + " ".join(redacted_cmd(cmd)))

    if args.dry_run:
        return 0
    return run_child(cmd)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
