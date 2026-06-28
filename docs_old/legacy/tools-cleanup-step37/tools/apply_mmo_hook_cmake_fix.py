#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
from pathlib import Path

REQUIRED_SOURCES = [
    "game/game/mmosemanticevents.cpp",
    "game/game/mmosemanticactionsink.cpp",
    "game/game/mmosemantichooks.cpp",
]

ANCHORS = [
    "game/game/mmoruntimesqlite.cpp",
    "game/game/inventory.cpp",
]

def normalized(text: str) -> str:
    return text.replace("\\", "/")

def candidate_files(project_root: Path) -> list[Path]:
    files: list[Path] = []
    for name in ("CMakeLists.txt",):
        files.extend(project_root.rglob(name))
    files.extend(project_root.rglob("*.cmake"))
    # Stable order: root-level CMakeLists first, then shorter paths.
    return sorted(set(files), key=lambda p: (p.name != "CMakeLists.txt", len(p.parts), str(p)))

def has_any_anchor(text: str) -> bool:
    n = normalized(text)
    return any(a in n for a in ANCHORS)

def patch_after_anchor(text: str, anchor: str, missing: list[str]) -> tuple[str, bool]:
    lines = text.splitlines(keepends=True)
    for i, line in enumerate(lines):
        if anchor in normalized(line):
            indent = re.match(r"^(\s*)", line).group(1)
            newline = "\n"
            if line.endswith("\r\n"):
                newline = "\r\n"
            elif line.endswith("\n"):
                newline = "\n"
            insert = [f"{indent}{src}{newline}" for src in missing]
            lines[i + 1:i + 1] = insert
            return "".join(lines), True
    return text, False

def patch_file(path: Path, apply: bool) -> tuple[bool, str]:
    text = path.read_text(encoding="utf-8", errors="surrogateescape")
    ntext = normalized(text)
    present = [src for src in REQUIRED_SOURCES if src in ntext]
    missing = [src for src in REQUIRED_SOURCES if src not in ntext]

    if not missing:
        return False, f"[SKIP] {path}: sources already present"

    if not has_any_anchor(text):
        return False, f"[SKIP] {path}: no OpenGothic game source anchor"

    patched = text
    applied = False

    for anchor in ANCHORS:
        if anchor in normalized(patched):
            patched, applied = patch_after_anchor(patched, anchor, missing)
            if applied:
                break

    if not applied:
        return False, f"[SKIP] {path}: anchor found in normalized text but not patchable line-by-line"

    if apply:
        backup = path.with_suffix(path.suffix + ".before-mmo-hooks-link-fix")
        if not backup.exists():
            backup.write_text(text, encoding="utf-8", errors="surrogateescape")
        path.write_text(patched, encoding="utf-8", errors="surrogateescape")
        return True, f"[PATCHED] {path}: added {', '.join(missing)}; backup={backup}"

    return True, f"[DRY-RUN] {path}: would add {', '.join(missing)}"

def main() -> int:
    parser = argparse.ArgumentParser(
        description="Add Mmo semantic hook .cpp files to the OpenGothic CMake source list."
    )
    parser.add_argument(
        "--root",
        default=".",
        help="OpenGothic project root, default: current directory",
    )
    parser.add_argument(
        "--apply",
        action="store_true",
        help="Actually modify CMake files. Without this, only prints what would change.",
    )
    args = parser.parse_args()

    project_root = Path(args.root).resolve()
    if not project_root.exists():
        raise SystemExit(f"project root does not exist: {project_root}")

    patched_any = False
    inspected = 0

    for path in candidate_files(project_root):
        # Avoid build artifacts and third-party trees.
        rel = path.relative_to(project_root)
        parts = set(rel.parts)
        if "build" in parts or ".git" in parts or "lib" in parts:
            continue
        inspected += 1
        changed, msg = patch_file(path, args.apply)
        print(msg)
        patched_any = patched_any or changed

    if not patched_any:
        print("[ERROR] No CMake file was patched.")
        print("Manual fix: add these sources to the Gothic2Notr target source list:")
        for src in REQUIRED_SOURCES:
            print(f"  {src}")
        return 2

    if not args.apply:
        print("")
        print("Dry-run only. Re-run with --apply to modify the project.")
    else:
        print("")
        print("Done. Reconfigure/build if needed:")
        print('  cmake --build build -j"$(nproc)"')

    print(f"Inspected {inspected} CMake files under {project_root}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
