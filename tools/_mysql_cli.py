from __future__ import annotations

import os
import shutil
from pathlib import Path


def resolve_mysql_exe() -> str:
    for env_name in ("GOTHIC_MMO_MYSQL_EXE", "MYSQL_EXE"):
        value = os.environ.get(env_name)
        if value:
            path = Path(value)
            if path.exists():
                return str(path)
            found = shutil.which(value)
            if found is not None:
                return found
            raise RuntimeError(f"{env_name} points to missing mysql executable: {value}")

    found = shutil.which("mysql")
    if found is not None:
        return found

    if os.name == "nt":
        roots = [
            Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "MySQL",
            Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "MySQL",
            Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "MariaDB",
            Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "MariaDB",
        ]
        matches: list[Path] = []
        for root in roots:
            if root.exists():
                matches.extend(root.glob("**/mysql.exe"))
        if matches:
            return str(sorted(matches, key=lambda p: (len(p.parts), str(p)))[0])

    raise RuntimeError("mysql executable was not found; set MYSQL_EXE to the full mysql.exe path")
