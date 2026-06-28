#!/usr/bin/env python3
"""Clean obsolete OpenGothic MMO helper tools safely.

Default mode is read-only. With --apply it moves obsolete/legacy tools into an
archive directory, preserving history. Use --delete only when you intentionally
want to remove them instead of archiving.

The goal is to keep tools/ focused on the current MySQL + server-boundary path:
client semantic envelopes -> receiver/outbox -> resolved worker -> MySQL
procedures -> evidence checkers.
"""
from __future__ import annotations

import argparse
import json
import shutil
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path


@dataclass(frozen=True)
class ToolDisposition:
    path: str
    action: str
    reason: str


# Remove/archive first because current target is MySQL 8.0+ and docs now say
# PostgreSQL is no longer the active production path.
OBSOLETE: tuple[ToolDisposition, ...] = (
    ToolDisposition("tools/import_runtime_sqlite_to_postgres.py", "archive", "PostgreSQL bootstrap path is superseded by current MySQL production target."),
    ToolDisposition("tools/check_postgres_bootstrap_import.py", "archive", "PostgreSQL bootstrap checker is not needed on the active MySQL path."),
    ToolDisposition("tools/check_postgres_mmo_schema.py", "archive", "PostgreSQL schema checker is not needed on the active MySQL path."),
    ToolDisposition("tools/apply_mmo_hook_cmake_fix.py", "archive", "One-shot Step32/34 CMake repair helper; keep only as archaeology once source list is fixed."),
    ToolDisposition("tools/compact_llm_docs.py", "archive", "One-shot compact-doc migration helper; not part of current runtime/evidence loop."),
    ToolDisposition("tools/print_mysql_mmo_remaining_work.py", "archive", "Replaced by DB readiness views/checkers and compact validation playbook."),
)

KEEP_CURRENT: tuple[str, ...] = (
    "tools/audit_runtime_sqlite.py",
    "tools/check_runtime_sqlite.py",
    "tools/import_runtime_sqlite_to_mysql.py",
    "tools/check_mysql_mmo_schema.py",
    "tools/check_mysql_bootstrap_import.py",
    "tools/check_mysql_steps_23_30_database_completion.py",
    "tools/check_mmo_semantic_action_jsonl.py",
    "tools/run_mmo_action_receiver.py",
    "tools/run_mmo_resolved_action_worker.py",
    "tools/replay_mmo_actions_to_receiver.py",
    "tools/check_mmo_action_receiver_outbox.py",
    "tools/check_mmo_action_dispatch_results.py",
    "tools/prepare_mmo_dispatch_dev_fixture.py",
    "tools/check_mmo_step36_vertical_slice.py",
    "tools/package_mmo_step36_evidence.py",
    "tools/check_mmo_step37_bookstand_script_xp.py",
)


def rel(root: Path, value: str) -> Path:
    path = Path(value)
    if path.is_absolute():
        raise ValueError(f"path must be relative: {value}")
    return root / path


def archive_one(root: Path, archive_dir: Path, item: ToolDisposition, delete: bool, apply: bool) -> dict[str, str]:
    src = rel(root, item.path)
    entry = {
        "path": item.path,
        "requested_action": "delete" if delete else item.action,
        "reason": item.reason,
        "exists": str(src.exists()).lower(),
        "status": "missing",
    }
    if not src.exists():
        return entry
    if not apply:
        entry["status"] = "would_delete" if delete else "would_archive"
        if not delete:
            entry["archive_path"] = str((archive_dir / item.path).as_posix())
        return entry
    if delete:
        src.unlink()
        entry["status"] = "deleted"
        return entry
    dst = archive_dir / item.path
    dst.parent.mkdir(parents=True, exist_ok=True)
    if dst.exists():
        dst = dst.with_suffix(dst.suffix + "." + datetime.now(timezone.utc).strftime("%Y%m%d%H%M%S"))
    shutil.move(str(src), str(dst))
    entry["status"] = "archived"
    entry["archive_path"] = str(dst.as_posix())
    return entry


def main() -> int:
    ap = argparse.ArgumentParser(description="Safely archive/delete obsolete OpenGothic MMO tools.")
    ap.add_argument("--root", default=".", help="OpenGothic project root, default: current directory")
    ap.add_argument("--archive-dir", default="docs/llm/legacy/tools-cleanup-step37", help="archive directory relative to root")
    ap.add_argument("--apply", action="store_true", help="perform changes; without this only prints a dry-run manifest")
    ap.add_argument("--delete", action="store_true", help="delete instead of archiving obsolete files")
    ap.add_argument("--manifest", default="", help="optional JSON manifest path")
    args = ap.parse_args()

    root = Path(args.root).resolve()
    archive_dir = rel(root, args.archive_dir)
    if not root.exists():
        raise SystemExit(f"project root does not exist: {root}")

    actions = [archive_one(root, archive_dir, item, args.delete, args.apply) for item in OBSOLETE]
    keep = [{"path": path, "exists": str(rel(root, path).exists()).lower()} for path in KEEP_CURRENT]
    manifest = {
        "tool": "cleanup_mmo_tools.py",
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "root": str(root),
        "mode": "apply" if args.apply else "dry-run",
        "delete": args.delete,
        "archive_dir": str(archive_dir),
        "obsolete_actions": actions,
        "kept_current_tools": keep,
        "note": "Default cleanup archives, not deletes. MySQL validation and Step36/Step37 evidence tools are intentionally kept.",
    }

    text = json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True)
    print(text)
    if args.manifest:
        path = Path(args.manifest)
        if not path.is_absolute():
            path = root / path
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text + "\n", encoding="utf-8")
        print(f"manifest={path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
