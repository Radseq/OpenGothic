#!/usr/bin/env python3
"""Export a read-only plan from docs/llm/llm_db_changes.

This tool intentionally does not create SQL, connect to MySQL or mutate any
runtime state. It turns the paused DB-change ledger into a machine-readable
"planned only" manifest so future agents can see what storage surfaces are
being discussed without mistaking them for applied schema.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_LEDGER_DIR = ROOT / "docs" / "llm" / "llm_db_changes"

ENTRY_RE = re.compile(r"^(?P<num>\d{3})-[A-Za-z0-9_.-]+\.md$")
TITLE_RE = re.compile(r"^#\s+(?P<title>.+?)\s*$", re.MULTILINE)
META_RE = re.compile(r"^\s*(?:-\s*)?(?P<key>[a-z_]+):\s*`?(?P<value>[^`\s#]+)`?\s*$", re.MULTILINE)
SQL_FENCE_RE = re.compile(r"^```\s*(sql|mysql)\b", re.IGNORECASE | re.MULTILINE)
EXECUTABLE_SQL_LINE_RE = re.compile(
    r"^\s*(?:DELIMITER\b|CREATE\s+(?:TABLE|VIEW|PROCEDURE|FUNCTION|DATABASE|INDEX|TRIGGER)\b|"
    r"ALTER\s+(?:TABLE|VIEW|PROCEDURE|FUNCTION|DATABASE)\b|DROP\s+(?:TABLE|VIEW|PROCEDURE|FUNCTION|DATABASE|INDEX|TRIGGER)\b|"
    r"INSERT\s+INTO\b|UPDATE\s+[`A-Za-z0-9_]+\b|DELETE\s+FROM\b|TRUNCATE\s+TABLE\b|CALL\s+[`A-Za-z0-9_]+\b)",
    re.IGNORECASE | re.MULTILINE,
)

SURFACE_PATTERNS: tuple[tuple[str, re.Pattern[str]], ...] = (
    ("ai_dialog_intent_dispatch_receipts", re.compile(r"\b(dispatch receipt|dispatch receipts|dispatch attempt|dispatch attempts)\b", re.IGNORECASE)),
    ("client_ack_nack_receipts", re.compile(r"\b(ACK/NACK receipt|ACK/NACK receipts|client ACK|client NACK|client ack|client nack)\b", re.IGNORECASE)),
    ("timeout_dead_letter_retry", re.compile(r"\b(timeout|dead-letter|dead letter|retry|retries)\b", re.IGNORECASE)),
    ("terminal_apply_procedures", re.compile(r"\b(terminal apply|terminal procedure|terminal procedures|mark_applied|mark actions applied)\b", re.IGNORECASE)),
    ("validation_and_replay_tools", re.compile(r"\b(validation/check tools|validation tools|replay|cleanup safety|checker|check tools)\b", re.IGNORECASE)),
    ("endpoint_or_route_audit", re.compile(r"\b(endpoint audit|endpoint lookup|route lookup|route audit|active-session lookup)\b", re.IGNORECASE)),
)

REQUIRED_META = ("applied_to_db", "sql_created", "server_sql_touched")
META_ALIASES = {
    "applied_to_db": ("applied_to_db",),
    "sql_created": ("sql_created", "sql_added", "executable_sql_added"),
    "server_sql_touched": ("server_sql_touched", "runtime_schema_required_now", "migration_required_now"),
}


@dataclass(frozen=True)
class PlanEntry:
    path: Path
    sequence: int
    title: str
    metadata: dict[str, str]
    surfaces: list[str]
    issues: list[str]


def rel(path: Path) -> str:
    try:
        return path.resolve().relative_to(ROOT.resolve()).as_posix()
    except ValueError:
        return path.as_posix()


def normalize_boolish(value: str) -> str:
    text = value.strip().strip("`").lower()
    if text in {"false", "0", "none", "no", "intentionally_no_sql"}:
        return "no"
    if text in {"true", "1", "yes"}:
        return "yes"
    return text


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def iter_entries(ledger_dir: Path) -> list[Path]:
    return sorted(path for path in ledger_dir.glob("*.md") if path.name.lower() != "readme.md")


def extract_surfaces(text: str) -> list[str]:
    surfaces: list[str] = []
    for key, pattern in SURFACE_PATTERNS:
        if pattern.search(text):
            surfaces.append(key)
    return surfaces


def parse_metadata(text: str) -> dict[str, str]:
    raw = {match.group("key"): normalize_boolish(match.group("value")) for match in META_RE.finditer(text)}
    metadata: dict[str, str] = {}
    for canonical, aliases in META_ALIASES.items():
        for alias in aliases:
            if alias in raw:
                metadata[canonical] = raw[alias]
                break
    if metadata.get("sql_created") is None and raw.get("status") == "no":
        metadata["sql_created"] = "no"
    if metadata.get("server_sql_touched") is None and raw.get("status") == "no":
        metadata["server_sql_touched"] = "no"
    return metadata


def parse_entry(path: Path) -> PlanEntry:
    text = read_text(path)
    issues: list[str] = []

    name_match = ENTRY_RE.fullmatch(path.name)
    sequence = -1
    if name_match is None:
        issues.append("filename_must_start_with_three_digit_sequence")
    else:
        sequence = int(name_match.group("num"))

    title_match = TITLE_RE.search(text)
    title = title_match.group("title").strip() if title_match else ""
    if not title:
        issues.append("missing_title")

    metadata = parse_metadata(text)
    for key in REQUIRED_META:
        if metadata.get(key) != "no":
            issues.append(f"metadata_{key}_must_be_no")

    if SQL_FENCE_RE.search(text):
        issues.append("ledger_entry_contains_sql_code_fence")
    if EXECUTABLE_SQL_LINE_RE.search(text):
        issues.append("ledger_entry_contains_executable_sql_statement")

    if "not present in mysql" not in text.lower() and "not applied" not in text.lower() and "no db" not in text.lower():
        issues.append("entry_should_state_design_is_not_applied")

    return PlanEntry(
        path=path,
        sequence=sequence,
        title=title,
        metadata={key: metadata.get(key, "") for key in REQUIRED_META},
        surfaces=extract_surfaces(text),
        issues=issues,
    )


def sequence_issues(entries: list[PlanEntry]) -> list[str]:
    issues: list[str] = []
    numbers = [entry.sequence for entry in entries if entry.sequence >= 0]
    if len(numbers) != len(set(numbers)):
        issues.append("duplicate_ledger_sequence_numbers")
    if numbers and sorted(numbers) != list(range(1, max(numbers) + 1)):
        issues.append("ledger_sequence_must_be_contiguous_from_001")
    return issues


def build_surface_catalog(entries: list[PlanEntry]) -> dict[str, dict[str, object]]:
    catalog: dict[str, dict[str, object]] = {}
    for entry in entries:
        for surface in entry.surfaces:
            item = catalog.setdefault(surface, {"mentions": 0, "entries": []})
            item["mentions"] = int(item["mentions"]) + 1
            cast_entries = item["entries"]
            assert isinstance(cast_entries, list)
            cast_entries.append(entry.sequence)
    return dict(sorted(catalog.items()))


def main() -> int:
    parser = argparse.ArgumentParser(description="Export a read-only planned-only manifest from docs/llm/llm_db_changes.")
    parser.add_argument("--ledger-dir", default=str(DEFAULT_LEDGER_DIR))
    parser.add_argument("--output", default="")
    parser.add_argument("--strict", action="store_true", help="Fail if no future DB storage surfaces can be classified.")
    parser.add_argument("--include-empty-surface-entries", action="store_true", help="Keep entries without classified future surfaces in the manifest.")
    args = parser.parse_args()

    ledger_dir = Path(args.ledger_dir)
    if not ledger_dir.is_absolute():
        ledger_dir = ROOT / ledger_dir

    issues: list[str] = []
    if not ledger_dir.exists():
        entries: list[PlanEntry] = []
        issues.append("ledger_dir_missing")
    else:
        entries = [parse_entry(path) for path in iter_entries(ledger_dir)]
        if not entries:
            issues.append("ledger_has_no_entries")

    issues.extend(sequence_issues(entries))
    failed_entries = [entry for entry in entries if entry.issues]
    surface_catalog = build_surface_catalog(entries)
    if args.strict and not surface_catalog:
        issues.append("strict_mode_requires_classified_future_storage_surfaces")

    filtered_entries = entries if args.include_empty_surface_entries else [entry for entry in entries if entry.surfaces or entry.issues]
    status = "failed" if issues or failed_entries else "passed"
    manifest = {
        "tool": "export_llm_db_changes_plan.py",
        "status": status,
        "exported_at": datetime.now(timezone.utc).isoformat(),
        "ledger_dir": rel(ledger_dir),
        "planned_only": True,
        "applied_to_db": False,
        "db_mutated": False,
        "sql_generated": False,
        "server_sql_touched": False,
        "mysql_connection_used": False,
        "migration_files_created": False,
        "entries_checked": len(entries),
        "latest_sequence": max((entry.sequence for entry in entries), default=0),
        "surface_catalog": surface_catalog,
        "issues": issues,
        "entries": [
            {
                "path": rel(entry.path),
                "sequence": entry.sequence,
                "title": entry.title,
                "metadata": entry.metadata,
                "planned_storage_surfaces": entry.surfaces,
                "applied_to_db": False,
                "sql_generated": False,
                "issues": entry.issues,
                "status": "failed" if entry.issues else "passed",
            }
            for entry in filtered_entries
        ],
    }

    if args.output:
        out = Path(args.output)
        if not out.is_absolute():
            out = ROOT / out
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print("artifact=" + str(out))
    print(json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True))
    return 0 if status == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
