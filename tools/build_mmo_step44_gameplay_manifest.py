#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any


def sha256(path: Path) -> str | None:
    if not path.exists():
        return None
    h = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def json_load(path: Path) -> Any:
    try:
        return json.loads(path.read_text(encoding='utf-8'))
    except Exception as exc:
        return {'error': str(exc)}


def line_count(path: Path) -> int | None:
    if not path.exists():
        return None
    with path.open('rb') as f:
        return sum(1 for _ in f)


def file_entry(path: str) -> dict[str, Any]:
    p = Path(path)
    return {
        'path': path,
        'exists': p.exists(),
        'size_bytes': p.stat().st_size if p.exists() else None,
        'sha256': sha256(p),
        'line_count': line_count(p) if p.suffix == '.jsonl' else None,
    }


def main() -> int:
    ap = argparse.ArgumentParser(description='Build Step44 gameplay evidence manifest.')
    ap.add_argument('--session-key', required=True)
    ap.add_argument('--accepted-jsonl', default='runtime/mmo_server_actions_step43.jsonl')
    ap.add_argument('--checkpoint-jsonl', default='runtime/mmo_server_checkpoints_step43.jsonl')
    ap.add_argument('--rejected-jsonl', default='runtime/mmo_server_rejects_step43.jsonl')
    ap.add_argument('--server-summary', default='runtime/mmo_server_step43_summary.json')
    ap.add_argument('--domain-report', default='runtime/mmo_step44_live_gameplay_domains.json')
    ap.add_argument('--worker-followup', default='runtime/mmo_step44_worker_followup_manifest.json')
    ap.add_argument('--output', required=True)
    args = ap.parse_args()

    domain_path = Path(args.domain_report)
    domain = json_load(domain_path) if domain_path.exists() else None
    status = 'passed'
    if not domain or domain.get('status') != 'passed':
        status = 'failed'

    manifest = {
        'step': 'step44_live_gameplay_domains',
        'status': status,
        'session_key': args.session_key,
        'files': {
            'accepted_jsonl': file_entry(args.accepted_jsonl),
            'checkpoint_jsonl': file_entry(args.checkpoint_jsonl),
            'rejected_jsonl': file_entry(args.rejected_jsonl),
            'server_summary': file_entry(args.server_summary),
            'domain_report': file_entry(args.domain_report),
            'worker_followup': file_entry(args.worker_followup),
        },
        'domain_report': domain,
        'server_summary': json_load(Path(args.server_summary)) if Path(args.server_summary).exists() else None,
        'worker_followup': json_load(Path(args.worker_followup)) if Path(args.worker_followup).exists() else None,
    }
    out = Path(args.output)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    print(f'artifact={out}')
    print(f'status={status}')
    return 0 if status == 'passed' else 1


if __name__ == '__main__':
    raise SystemExit(main())
