#!/usr/bin/env python3
from __future__ import annotations

import runpy
import sys
from pathlib import Path


def run(relative_target: str) -> None:
    target = Path(__file__).resolve().parent / relative_target
    if not target.exists():
        raise SystemExit(f"tool target does not exist: {target}")
    sys.argv[0] = str(target)
    runpy.run_path(str(target), run_name="__main__")




