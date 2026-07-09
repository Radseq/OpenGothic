#!/usr/bin/env python3
"""Validate docs/llm/llm_db_changes while DB work is paused.

This tool is intentionally read-only. It does not connect to MySQL, does not
execute SQL and does not generate migrations. Its purpose is to make the paused
DB-plan ledger explicit so future agents do not confuse design notes with
applied schema state.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Iterable

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_LEDGER_DIR = ROOT / "docs" / "llm" / "llm_db_changes"

ENTRY_RE = re.compile(r"^(?P<num>\d{3})-[A-Za-z0-9_.-]+\.md$")
HEADER_RE = re.compile(r"^#\s+(?:(?P<num>\d{3})\s+-\s+.*|.*\bStep(?P<step>\d{3})\b.*)", re.IGNORECASE | re.MULTILINE)
META_RE = re.compile(r"^\s*(?:-\s*)?(?P<key>[a-z_]+):\s*`?(?P<value>[^`\s#]+)`?\s*$", re.MULTILINE)
SQL_FENCE_RE = re.compile(r"^```\s*(sql|mysql)\b", re.IGNORECASE | re.MULTILINE)
EXECUTABLE_SQL_LINE_RE = re.compile(
    r"^\s*(?:DELIMITER\b|CREATE\s+(?:TABLE|VIEW|PROCEDURE|FUNCTION|DATABASE|INDEX|TRIGGER)\b|"
    r"ALTER\s+(?:TABLE|VIEW|PROCEDURE|FUNCTION|DATABASE)\b|DROP\s+(?:TABLE|VIEW|PROCEDURE|FUNCTION|DATABASE|INDEX|TRIGGER)\b|"
    r"INSERT\s+INTO\b|UPDATE\s+[`A-Za-z0-9_]+\b|DELETE\s+FROM\b|TRUNCATE\s+TABLE\b|CALL\s+[`A-Za-z0-9_]+\b)",
    re.IGNORECASE | re.MULTILINE,
)
FUTURE_SURFACE_RE = re.compile(
    r"\b(dispatch receipt|dispatch receipts|ACK/NACK receipt|ACK/NACK receipts|timeout|dead-letter|retry|terminal apply|terminal procedures|terminal procedure)\b",
    re.IGNORECASE,
)

REQUIRED_META = ("applied_to_db", "sql_created", "server_sql_touched")
META_ALIASES = {
    "applied_to_db": ("applied_to_db",),
    "sql_created": ("sql_created", "sql_added", "executable_sql_added"),
    "server_sql_touched": ("server_sql_touched", "runtime_schema_required_now", "migration_required_now"),
}


@dataclass(frozen=True)
class LedgerEntry:
    path: Path
    number: int
    relative_path: str
    metadata: dict[str, str]
    future_surface_mentions: int
    issues: list[str]


def normalize_boolish(value: str) -> str:
    text = value.strip().strip("`").lower()
    if text in {"false", "0", "none", "no", "intentionally_no_sql"}:
        return "no"
    if text in {"true", "1", "yes"}:
        return "yes"
    return text


def rel(path: Path) -> str:
    try:
        return path.resolve().relative_to(ROOT.resolve()).as_posix()
    except ValueError:
        return path.as_posix()


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def iter_entries(ledger_dir: Path) -> list[Path]:
    return sorted(
        path for path in ledger_dir.glob("*.md")
        if path.name.lower() != "readme.md"
    )


def parse_entry(path: Path, ledger_dir: Path) -> LedgerEntry:
    text = read_text(path)
    issues: list[str] = []
    name_match = ENTRY_RE.fullmatch(path.name)
    number = -1
    if name_match is None:
        issues.append("filename_must_start_with_three_digit_sequence")
    else:
        number = int(name_match.group("num"))

    header_match = HEADER_RE.search(text)
    if header_match is None:
        issues.append("missing_step_markdown_header")
    else:
        header_num = header_match.group("num")
        if number >= 0 and header_num is not None and int(header_num) != number:
            issues.append("filename_sequence_does_not_match_header_sequence")

    raw_metadata = {m.group("key"): normalize_boolish(m.group("value")) for m in META_RE.finditer(text)}
    metadata: dict[str, str] = {}
    for canonical, aliases in META_ALIASES.items():
        for alias in aliases:
            if alias in raw_metadata:
                metadata[canonical] = raw_metadata[alias]
                break
    if metadata.get("sql_created") is None and raw_metadata.get("status") == "no":
        metadata["sql_created"] = "no"
    if metadata.get("server_sql_touched") is None and raw_metadata.get("status") == "no":
        metadata["server_sql_touched"] = "no"
    for key in REQUIRED_META:
        if key not in metadata:
            issues.append("missing_metadata_" + key)
        elif metadata[key] != "no":
            issues.append("metadata_" + key + "_must_be_no_while_db_work_paused")

    if SQL_FENCE_RE.search(text):
        issues.append("ledger_entry_must_not_contain_sql_code_fence")
    if EXECUTABLE_SQL_LINE_RE.search(text):
        issues.append("ledger_entry_must_not_contain_executable_sql_statement")

    if "not present in mysql" not in text.lower() and "not applied" not in text.lower() and "no db" not in text.lower():
        issues.append("entry_should_explicitly_state_db_work_is_not_applied")

    return LedgerEntry(
        path=path,
        number=number,
        relative_path=rel(path),
        metadata=metadata,
        future_surface_mentions=len(FUTURE_SURFACE_RE.findall(text)),
        issues=issues,
    )


def check_sequence(entries: list[LedgerEntry]) -> list[str]:
    issues: list[str] = []
    numbers = [entry.number for entry in entries if entry.number >= 0]
    if len(numbers) != len(set(numbers)):
        issues.append("duplicate_ledger_sequence_numbers")
    if numbers:
        expected = list(range(1, max(numbers) + 1))
        if sorted(numbers) != expected:
            issues.append("ledger_sequence_must_be_contiguous_from_001")
    return issues


def scan_for_server_sql(paths: Iterable[Path]) -> list[str]:
    touched: list[str] = []
    for path in paths:
        try:
            relative = path.resolve().relative_to(ROOT.resolve())
        except ValueError:
            continue
        parts = relative.parts
        if len(parts) >= 2 and parts[0] == "server" and parts[1] == "sql":
            touched.append(relative.as_posix())
    return sorted(touched)


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate docs/llm/llm_db_changes as a no-DB paused-work ledger.")
    parser.add_argument("--ledger-dir", default=str(DEFAULT_LEDGER_DIR))
    parser.add_argument("--output", default="")
    parser.add_argument("--strict", action="store_true", help="Fail if there are no future DB surface mentions in the ledger.")
    parser.add_argument(
        "--scan-path",
        action="append",
        default=[],
        help="Optional changed-file path to check for forbidden server/sql entries. May be passed more than once.",
    )
    args = parser.parse_args()

    ledger_dir = Path(args.ledger_dir)
    if not ledger_dir.is_absolute():
        ledger_dir = ROOT / ledger_dir

    top_level_issues: list[str] = []
    if not ledger_dir.exists():
        top_level_issues.append("ledger_dir_missing")
        entries: list[LedgerEntry] = []
    else:
        entries = [parse_entry(path, ledger_dir) for path in iter_entries(ledger_dir)]
        if not entries:
            top_level_issues.append("ledger_has_no_entries")

    top_level_issues.extend(check_sequence(entries))
    future_mentions = sum(entry.future_surface_mentions for entry in entries)
    if args.strict and future_mentions == 0:
        top_level_issues.append("strict_mode_requires_future_db_surface_mentions")

    scan_paths = [Path(value) for value in args.scan_path]
    server_sql_paths = scan_for_server_sql(scan_paths)
    if server_sql_paths:
        top_level_issues.append("server_sql_path_present_while_db_work_paused")

    failed_entries = [entry for entry in entries if entry.issues]
    status = "failed" if top_level_issues or failed_entries else "passed"
    report = {
        "tool": "check_llm_db_changes_ledger.py",
        "status": status,
        "checked_at": datetime.now(timezone.utc).isoformat(),
        "ledger_dir": rel(ledger_dir),
        "entries_checked": len(entries),
        "latest_sequence": max((entry.number for entry in entries), default=0),
        "future_surface_mentions": future_mentions,
        "db_mutated": False,
        "sql_generated": False,
        "mysql_connection_used": False,
        "server_sql_paths": server_sql_paths,
        "issues": top_level_issues,
        "entries": [
            {
                "path": entry.relative_path,
                "sequence": entry.number,
                "metadata": entry.metadata,
                "future_surface_mentions": entry.future_surface_mentions,
                "issues": entry.issues,
                "status": "failed" if entry.issues else "passed",
            }
            for entry in entries
        ],
    }

    if args.output:
        out = Path(args.output)
        if not out.is_absolute():
            out = ROOT / out
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print("artifact=" + str(out))
    print(json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True))
    return 0 if status == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
