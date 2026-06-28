#!/usr/bin/env python3
"""Move old docs/llm/ai markdown files out of the active AI context.

Run from repository root after copying the compact docs from the ZIP:
  python3 tools/compact_llm_docs.py --apply
"""
from __future__ import annotations

import argparse
import datetime as _dt
import shutil
from pathlib import Path

KEEP = {
    "00-core-context.md",
    "01-persistence-stack.md",
    "02-domain-model.md",
    "03-engine-surfaces.md",
    "04-mutation-boundaries.md",
    "05-server-first-roadmap.md",
    "06-validation-playbook.md",
}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--apply", action="store_true", help="actually move non-compact docs; otherwise dry-run")
    ap.add_argument("--root", type=Path, default=Path.cwd(), help="repository root, default: cwd")
    args = ap.parse_args()

    root = args.root.resolve()
    ai = root / "docs" / "llm" / "ai"
    if not ai.is_dir():
        raise SystemExit(f"missing directory: {ai}")

    existing = sorted(p for p in ai.glob("*.md") if p.is_file())
    stale = [p for p in existing if p.name not in KEEP]
    missing = sorted(KEEP - {p.name for p in existing})

    print(f"active compact docs expected: {len(KEEP)}")
    print(f"current markdown files:        {len(existing)}")
    print(f"files to archive:             {len(stale)}")
    if missing:
        print("missing compact files:")
        for name in missing:
            print(f"  MISSING {name}")

    if not args.apply:
        for p in stale:
            print(f"  would archive {p.relative_to(root)}")
        return 0 if not missing else 2

    stamp = _dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    archive = root / "docs" / "llm" / "legacy" / f"ai-precompact-{stamp}"
    archive.mkdir(parents=True, exist_ok=True)

    for p in stale:
        dst = archive / p.name
        if dst.exists():
            dst = archive / f"{p.stem}-{stamp}{p.suffix}"
        shutil.move(str(p), str(dst))
        print(f"archived {p.relative_to(root)} -> {dst.relative_to(root)}")

    left = sorted(p.name for p in ai.glob("*.md"))
    print("active docs/llm/ai files:")
    for name in left:
        print(f"  {name}")
    return 0 if not missing else 2


if __name__ == "__main__":
    raise SystemExit(main())
