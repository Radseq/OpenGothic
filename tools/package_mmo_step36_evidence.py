#!/usr/bin/env python3
from __future__ import annotations

import argparse
import datetime as _dt
import hashlib
import json
import os
import shutil
import sqlite3
import sys
import zipfile
from pathlib import Path
from typing import Any


SMALL_COPY_LIMIT = 32 * 1024 * 1024


def sha256_file(path: Path, chunk_size: int = 1024 * 1024) -> str:
    h = hashlib.sha256()
    with path.open('rb') as f:
        while True:
            b = f.read(chunk_size)
            if not b:
                break
            h.update(b)
    return h.hexdigest()


def utc_now_iso() -> str:
    return _dt.datetime.now(_dt.timezone.utc).replace(microsecond=0).isoformat()


def read_json(path: Path) -> Any:
    with path.open('r', encoding='utf-8') as f:
        return json.load(f)


def rel_copy(src: Path, root: Path, rel: str) -> dict[str, Any]:
    dst = root / rel
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)
    st = dst.stat()
    return {
        'source_path': str(src),
        'package_path': rel,
        'size_bytes': st.st_size,
        'sha256': sha256_file(dst),
        'copied': True,
    }


def file_hash_entry(path: Path, copied: bool = False, package_path: str | None = None) -> dict[str, Any]:
    st = path.stat()
    return {
        'source_path': str(path),
        'package_path': package_path,
        'size_bytes': st.st_size,
        'sha256': sha256_file(path),
        'copied': copied,
    }


def count_jsonl(path: Path) -> dict[str, Any]:
    rows = 0
    kinds: dict[str, int] = {}
    bad_rows = 0
    if not path.exists():
        return {'exists': False, 'rows': 0, 'bad_rows': 0, 'kinds': {}}
    with path.open('r', encoding='utf-8', errors='replace') as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            rows += 1
            try:
                obj = json.loads(line)
                kind = str(obj.get('action_kind') or obj.get('kind') or '')
                if kind:
                    kinds[kind] = kinds.get(kind, 0) + 1
            except json.JSONDecodeError:
                bad_rows += 1
    return {'exists': True, 'rows': rows, 'bad_rows': bad_rows, 'kinds': dict(sorted(kinds.items()))}


def sqlite_summary(path: Path) -> dict[str, Any]:
    out: dict[str, Any] = {'exists': path.exists()}
    if not path.exists():
        return out
    out.update(file_hash_entry(path))
    try:
        con = sqlite3.connect(f'file:{path}?mode=ro', uri=True)
        cur = con.cursor()
        cur.execute("SELECT name FROM sqlite_master WHERE type='table' ORDER BY name")
        tables = [r[0] for r in cur.fetchall()]
        out['table_count'] = len(tables)
        counts: dict[str, int] = {}
        for t in tables:
            if t.startswith(('runtime_', 'mmo_')):
                try:
                    cur.execute(f'SELECT COUNT(*) FROM "{t}"')
                    counts[t] = int(cur.fetchone()[0])
                except sqlite3.Error:
                    pass
        out['interesting_table_rows'] = counts
        try:
            cur.execute('PRAGMA integrity_check')
            out['integrity_check'] = cur.fetchone()[0]
        except sqlite3.Error as e:
            out['integrity_check_error'] = str(e)
        con.close()
    except Exception as e:  # diagnostics only
        out['summary_error'] = str(e)
    return out


def write_summary_md(root: Path, manifest: dict[str, Any]) -> None:
    artifact = manifest.get('step36_artifact', {})
    outbox = artifact.get('outbox', {}) if isinstance(artifact, dict) else {}
    journal = artifact.get('journal', {}) if isinstance(artifact, dict) else {}
    req = artifact.get('requirements', []) if isinstance(artifact, dict) else []

    lines = [
        f"# Step36 Evidence Package - {manifest['session_key']}",
        '',
        f"Generated UTC: `{manifest['generated_utc']}`",
        f"Package status: `{manifest['package_status']}`",
        f"Step36 status: `{artifact.get('status', 'unknown') if isinstance(artifact, dict) else 'unknown'}`",
        f"Step36 evidence level: `{artifact.get('evidence_level', 'unknown') if isinstance(artifact, dict) else 'unknown'}`",
        f"Step36 hash: `{artifact.get('hash', 'unknown') if isinstance(artifact, dict) else 'unknown'}`",
        '',
        '## Outbox',
    ]
    if isinstance(outbox, dict) and outbox:
        for k, v in sorted(outbox.items()):
            lines.append(f"- `{k}` = `{v}`")
    else:
        lines.append('- no outbox summary in artifact')
    lines.extend(['', '## Journal'])
    if isinstance(journal, dict) and journal:
        for k, v in sorted(journal.items()):
            lines.append(f"- `{k}` = `{v}`")
    else:
        lines.append('- no journal summary in artifact')
    lines.extend(['', '## Requirements'])
    if isinstance(req, list) and req:
        for r in req:
            if isinstance(r, dict):
                status = 'OK' if r.get('ok') else 'FAIL'
                lines.append(f"- [{status}] `{r.get('key', r.get('name', 'unknown'))}`")
            else:
                lines.append(f"- `{r}`")
    else:
        lines.append('- no requirement rows in artifact')
    lines.extend(['', '## Packaged files'])
    for name, info in sorted(manifest.get('files', {}).items()):
        if isinstance(info, dict):
            copied = 'copied' if info.get('copied') else 'hash-only'
            lines.append(f"- `{name}`: {copied}, sha256 `{info.get('sha256', 'missing')}`, size `{info.get('size_bytes', 0)}`")
    lines.extend(['', '## Notes', '- This package is Step36 vertical-slice evidence, not full `.sav + SQLite + MySQL` restore parity proof.'])
    (root / 'summary.md').write_text('\n'.join(lines) + '\n', encoding='utf-8')


def zip_dir(src_dir: Path, zip_path: Path) -> str:
    zip_path.parent.mkdir(parents=True, exist_ok=True)
    if zip_path.exists():
        zip_path.unlink()
    with zipfile.ZipFile(zip_path, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for p in sorted(src_dir.rglob('*')):
            if p.is_file():
                zf.write(p, p.relative_to(src_dir).as_posix())
    return sha256_file(zip_path)


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description='Package Step36 vertical-slice evidence into a deterministic evidence directory/zip.')
    ap.add_argument('--session-key', required=True)
    ap.add_argument('--artifact', required=True, help='JSON artifact produced by check_mmo_step36_vertical_slice.py')
    ap.add_argument('--client-jsonl')
    ap.add_argument('--server-jsonl')
    ap.add_argument('--recovered-server-jsonl')
    ap.add_argument('--sqlite')
    ap.add_argument('--copy-sqlite', action='store_true', help='Copy SQLite DB into the package instead of hashing/summarizing only.')
    ap.add_argument('--native-save', action='append', default=[], help='Optional native .sav/world zip file to hash and copy if reasonably small. Repeatable.')
    ap.add_argument('--output-dir', default=None)
    ap.add_argument('--zip', dest='zip_path', default=None, help='Optional output zip path. If omitted, no zip is created.')
    ap.add_argument('--strict', action='store_true', help='Exit non-zero unless the Step36 artifact status is passed.')
    args = ap.parse_args(argv)

    artifact_path = Path(args.artifact)
    if not artifact_path.exists():
        print(f'[FAIL] artifact not found: {artifact_path}', file=sys.stderr)
        return 2

    artifact = read_json(artifact_path)
    ts = _dt.datetime.now().strftime('%Y%m%d_%H%M%S')
    out_dir = Path(args.output_dir) if args.output_dir else Path('runtime/evidence') / f"step36_{args.session_key}_{ts}"
    out_dir.mkdir(parents=True, exist_ok=True)

    manifest: dict[str, Any] = {
        'schema_version': 1,
        'package_kind': 'mmo_step36_vertical_slice_evidence_package',
        'generated_utc': utc_now_iso(),
        'session_key': args.session_key,
        'command': ' '.join(sys.argv),
        'step36_artifact': artifact,
        'files': {},
        'jsonl_summaries': {},
        'sqlite_summary': None,
        'native_save_files': [],
    }

    manifest['files']['step36_artifact'] = rel_copy(artifact_path, out_dir, 'artifact/step36_vertical_slice.json')

    jsonl_inputs = [
        ('client_jsonl', args.client_jsonl, 'jsonl/client_actions.jsonl'),
        ('server_jsonl', args.server_jsonl, 'jsonl/server_actions.jsonl'),
        ('recovered_server_jsonl', args.recovered_server_jsonl, 'jsonl/server_actions.recovered.jsonl'),
    ]
    for name, value, rel in jsonl_inputs:
        if not value:
            continue
        path = Path(value)
        if path.exists():
            manifest['files'][name] = rel_copy(path, out_dir, rel)
            manifest['jsonl_summaries'][name] = count_jsonl(path)
        else:
            manifest['files'][name] = {'source_path': str(path), 'exists': False, 'copied': False}
            manifest['jsonl_summaries'][name] = {'exists': False, 'rows': 0, 'bad_rows': 0, 'kinds': {}}

    if args.sqlite:
        sqlite_path = Path(args.sqlite)
        manifest['sqlite_summary'] = sqlite_summary(sqlite_path)
        if sqlite_path.exists() and args.copy_sqlite:
            manifest['files']['sqlite'] = rel_copy(sqlite_path, out_dir, 'sqlite/runtime.sqlite')
        elif sqlite_path.exists():
            manifest['files']['sqlite'] = file_hash_entry(sqlite_path, copied=False)
        else:
            manifest['files']['sqlite'] = {'source_path': str(sqlite_path), 'exists': False, 'copied': False}

    for idx, value in enumerate(args.native_save):
        path = Path(value)
        entry: dict[str, Any]
        if path.exists():
            rel = f'native_save/{idx:02d}_{path.name}'
            if path.stat().st_size <= SMALL_COPY_LIMIT:
                entry = rel_copy(path, out_dir, rel)
            else:
                entry = file_hash_entry(path, copied=False)
            manifest['native_save_files'].append(entry)
        else:
            entry = {'source_path': str(path), 'exists': False, 'copied': False}
            manifest['native_save_files'].append(entry)

    status = str(artifact.get('status', 'unknown')) if isinstance(artifact, dict) else 'unknown'
    manifest['package_status'] = 'passed' if status == 'passed' else 'failed'

    write_summary_md(out_dir, manifest)
    manifest_path = out_dir / 'manifest.json'
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + '\n', encoding='utf-8')

    # Include summary/manifest hashes after writing them.
    final_hashes = {
        'manifest.json': sha256_file(manifest_path),
        'summary.md': sha256_file(out_dir / 'summary.md'),
    }
    (out_dir / 'package.sha256.json').write_text(json.dumps(final_hashes, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    package_dir_hash = hashlib.sha256(json.dumps(final_hashes, sort_keys=True).encode('utf-8')).hexdigest()

    zip_hash = None
    if args.zip_path:
        zip_hash = zip_dir(out_dir, Path(args.zip_path))

    print(f'evidence_dir={out_dir}')
    if zip_hash:
        print(f'evidence_zip={args.zip_path}')
        print(f'evidence_zip_sha256={zip_hash}')
    print(f'package_status={manifest["package_status"]}')
    print(f'step36_status={status}')
    print(f'package_dir_hash={package_dir_hash}')

    if args.strict and status != 'passed':
        print('[FAIL] strict mode requires Step36 artifact status=passed', file=sys.stderr)
        return 1
    print('[OK]' if status == 'passed' else '[WARN] packaged a non-passed Step36 artifact')
    return 0


if __name__ == '__main__':
    raise SystemExit(main(sys.argv[1:]))
