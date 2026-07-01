#!/usr/bin/env python3
"""Archive obsolete OpenGothic MMO tools and old AI context safely.

Default is dry-run. Use --apply to move files into archive directories. This
script intentionally archives instead of deleting so historical step evidence can
be recovered without keeping it in the hot LLM/tooling path.
"""
from __future__ import annotations

import argparse
import json
import shutil
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Iterable


ROOT = Path(__file__).resolve().parents[1]


KEEP_TOOLS_ACTIVE_SERVER: set[str] = {
    # hygiene
    "tools/cleanup_mmo_tools.py",
    # runtime/SQLite/MySQL audit/import
    "tools/audit_runtime_sqlite.py",
    "tools/check_runtime_sqlite.py",
    "tools/import_runtime_sqlite_to_mysql.py",
    "tools/check_mysql_mmo_schema.py",
    "tools/check_mysql_bootstrap_import.py",
    "tools/check_mysql_world_item_write_path.py",
    "tools/check_mysql_wallet_write_path.py",
    "tools/check_mysql_character_inventory_equipment_write_path.py",
    "tools/check_mysql_container_interactive_write_path.py",
    "tools/check_mysql_progress_npc_projection_write_paths.py",
    "tools/check_mysql_server_write_path.py",
    "tools/check_mysql_steps_11_14_economy_combat_stack.py",
    "tools/check_mysql_steps_15_18_bridge_replay_parity.py",
    "tools/check_mysql_steps_19_22_dispatch_replay_parity.py",
    "tools/check_mysql_steps_23_30_database_completion.py",
    # server-boundary core
    "tools/run_mmo_server.py",
    "tools/run_mmo_action_receiver.py",
    "tools/run_mmo_resolved_action_worker.py",
    "tools/replay_mmo_actions_to_receiver.py",
    "tools/check_mmo_semantic_action_jsonl.py",
    "tools/check_mmo_action_receiver_outbox.py",
    "tools/check_mmo_action_dispatch_results.py",
    "tools/inspect_mmo_action_resolution.py",
    # current live gameplay probes/checkers
    "tools/check_mmo_step43_server_live.py",
    "tools/run_mmo_step43_server_smoke.py",
    "tools/check_mmo_step44_live_gameplay_domains.py",
    "tools/run_mmo_step44_worker_followup.py",
    "tools/build_mmo_step44_gameplay_manifest.py",
    "tools/check_mmo_step45_world_ai_weapon_loot.py",
    "tools/run_mmo_step45_world_ai_followup.py",
    "tools/build_mmo_step45_world_ai_manifest.py",
    "tools/check_mmo_step46_consumables_sleep_ai_context.py",
    "tools/run_mmo_step46_consumables_sleep_followup.py",
    "tools/check_mmo_step47_interactive_mobsi_state.py",
    "tools/run_mmo_step47_interactive_followup.py",
    "tools/check_mmo_step48_interactive_trigger_filter.py",
    "tools/run_mmo_step48_interactive_trigger_followup.py",
    "tools/inspect_mmo_runtime_navigation.py",
    "tools/inspect_mmo_mysql_server_bootstrap_state.py",
    "tools/run_mmo_step49_server_bootstrap_probe.py",
    "tools/apply_mmo_step51_authority_gap_procedures.py",
    "tools/check_mmo_step51_authority_gap_procedures.py",
    "tools/run_mmo_step51_authority_gap_followup.py",
    "tools/inspect_mmo_mysql_schema_maturity.py",
    "tools/run_mmo_step52_db_maturity_probe.py",
    "tools/materialize_mmo_server_read_model_v1.py",
    "tools/inspect_mmo_server_read_model_v1.py",
    "tools/export_mmo_server_materialization_manifest.py",
    "tools/run_mmo_step53_server_materialization_followup.py",
    "tools/capture_mmo_chapter1_start_sqlite_baseline.py",
    "tools/restore_mmo_chapter1_start_sqlite_baseline.py",
    "tools/reset_mmo_mysql_from_chapter1_start.py",
    "tools/run_mmo_step54_chapter1_clean_start_followup.py",
    "tools/check_mmo_step55_client_server_bootstrap.py",
    "tools/run_mmo_step55_clean_mysql_from_pre_xardas.py",
    "tools/apply_mmo_step55_live_receiver_bridge.py",
    "tools/check_mmo_step55_live_receiver_bridge.py",
    "tools/check_mmo_step56_server_bootstrap_ack.py",
    "tools/apply_mmo_step56b_clean_db_progress_bridge.py",
    "tools/check_mmo_step56b_clean_db_progress_bridge.py",
    "tools/normalize_mmo_mysql_collation.py",
    "tools/check_mmo_step57_clean_reset_and_checkpoint_ack.py",
    "tools/check_mmo_step58_movement_authority_gate.py",
}

# Safe profile only archives things that are known-obsolete even on old branches.
SAFE_OBSOLETE_TOOLS: set[str] = {
    "tools/import_runtime_sqlite_to_postgres.py",
    "tools/check_postgres_bootstrap_import.py",
    "tools/check_postgres_mmo_schema.py",
    "tools/apply_mmo_hook_cmake_fix.py",
    "tools/compact_llm_docs.py",
    "tools/print_mysql_mmo_remaining_work.py",
}

KEEP_DOCS_ACTIVE: set[str] = {
    "docs/llm/ai/00-current-mmo-state.md",
    "docs/llm/ai/01-authority-and-bootstrap.md",
    "docs/llm/ai/02-gameplay-domain-contract.md",
    "docs/llm/ai/03-source-map-and-hooks.md",
    "docs/llm/ai/04-active-tools.md",
    "docs/llm/ai/05-next-work.md",
    "docs/llm/ai/06-step50-context-hygiene.md",
    "docs/llm/ai/07-step51-authority-gap-db.md",
    "docs/llm/ai/08-step52-production-db-contract.md",
    "docs/llm/ai/09-step53-server-read-model.md",
    "docs/llm/ai/10-step54-chapter1-clean-start.md",
    "docs/llm/ai/10-step54-pre-xardas-capture.md",
    "docs/llm/ai/11-step55-client-server-bootstrap.md",
    "docs/llm/ai/12-step55b-clean-mysql-flow.md",
    "docs/llm/ai/13-step55d-live-receiver-bridge.md",
    "docs/llm/ai/14-step55e-live-receiver-event-class-fix.md",
    "docs/llm/ai/15-step56-server-bootstrap-ack.md",
    "docs/llm/ai/16-step56b-clean-db-progress-bridge.md",
    "docs/llm/ai/17-step57-clean-reset-and-checkpoint-ack.md",
    "docs/llm/ai/18-step58-movement-authority-gate.md",
    "docs/llm/ai/10-step54-pre-xardas-capture.md",
    "docs/llm/ai/11-step55-client-server-bootstrap.md",
}


@dataclass(frozen=True)
class Candidate:
    path: str
    archive_subdir: str
    reason: str


def rel(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def iter_root_tools() -> Iterable[str]:
    tools = ROOT / "tools"
    if not tools.exists():
        return []
    return sorted(rel(p) for p in tools.glob("*.py") if p.is_file())


def iter_ai_docs() -> Iterable[str]:
    ai = ROOT / "docs" / "llm" / "ai"
    if not ai.exists():
        return []
    return sorted(rel(p) for p in ai.glob("*.md") if p.is_file())


def build_candidates(profile: str, include_tools: bool, include_docs: bool) -> list[Candidate]:
    out: list[Candidate] = []
    if include_tools:
        if profile == "safe":
            for path in sorted(SAFE_OBSOLETE_TOOLS):
                if (ROOT / path).exists():
                    out.append(Candidate(path, "tools", "known obsolete one-shot or superseded non-MySQL tool"))
        elif profile == "active-server":
            for path in iter_root_tools():
                if path not in KEEP_TOOLS_ACTIVE_SERVER:
                    out.append(Candidate(path, "tools", "not in active server-boundary/MySQL/tooling set"))
        else:
            raise ValueError(f"unknown profile: {profile}")
    if include_docs and profile == "active-server":
        for path in iter_ai_docs():
            if path not in KEEP_DOCS_ACTIVE:
                out.append(Candidate(path, "docs-llm-ai", "old step history; compact active memory replaces it"))
    return out


def archive_path(base: Path, candidate: Candidate) -> Path:
    return base / candidate.archive_subdir / candidate.path


def move_candidate(base: Path, candidate: Candidate, apply: bool) -> dict[str, str]:
    src = ROOT / candidate.path
    dst = archive_path(base, candidate)
    status = "missing" if not src.exists() else ("would_archive" if not apply else "archived")
    if apply and src.exists():
        dst.parent.mkdir(parents=True, exist_ok=True)
        if dst.exists():
            raise FileExistsError(f"archive target already exists: {dst}")
        shutil.move(str(src), str(dst))
    return {
        "path": candidate.path,
        "status": status,
        "archive_path": rel(dst) if dst.is_relative_to(ROOT) else str(dst),
        "reason": candidate.reason,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Archive obsolete MMO tools and old docs/llm/ai files safely.")
    parser.add_argument("--profile", choices=("safe", "active-server"), default="safe")
    parser.add_argument("--apply", action="store_true", help="Actually move files into archive directories.")
    parser.add_argument("--dry-run", action="store_true", help="Force dry-run even if --apply is omitted; kept for explicitness.")
    parser.add_argument("--no-tools", action="store_true", help="Do not process tools/*.py.")
    parser.add_argument("--no-docs", action="store_true", help="Do not process docs/llm/ai/*.md.")
    parser.add_argument("--archive-root", default=None, help="Override archive root. Default: .mmo_archive/step50_<timestamp>.")
    parser.add_argument("--manifest", default=None, help="Manifest path. Default: archive_root/manifest.json.")
    args = parser.parse_args()

    timestamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    archive_root = Path(args.archive_root) if args.archive_root else Path(".mmo_archive") / f"step50_context_hygiene_{timestamp}"
    if not archive_root.is_absolute():
        archive_root = ROOT / archive_root

    include_tools = not args.no_tools
    include_docs = not args.no_docs
    candidates = build_candidates(args.profile, include_tools, include_docs)
    apply = bool(args.apply)

    manifest = {
        "step": 50,
        "profile": args.profile,
        "apply": apply,
        "archive_root": rel(archive_root) if archive_root.is_relative_to(ROOT) else str(archive_root),
        "candidate_count": len(candidates),
        "entries": [],
    }

    for candidate in candidates:
        entry = move_candidate(archive_root, candidate, apply)
        manifest["entries"].append(entry)
        print(f"{entry['status']}: {entry['path']} -> {entry['archive_path']} [{entry['reason']}]")

    manifest_path = Path(args.manifest) if args.manifest else archive_root / "manifest.json"
    if not manifest_path.is_absolute():
        manifest_path = ROOT / manifest_path
    if apply:
        manifest_path.parent.mkdir(parents=True, exist_ok=True)
        manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print(f"manifest={rel(manifest_path) if manifest_path.is_relative_to(ROOT) else manifest_path}")
    else:
        print(json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True))

    print(f"summary: candidates={len(candidates)} mode={'apply' if apply else 'dry-run'} profile={args.profile}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
