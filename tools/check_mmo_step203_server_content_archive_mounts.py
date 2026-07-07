#!/usr/bin/env python3
from pathlib import Path
import runpy
import sys

target = Path(__file__).resolve().parent / "validation" / "check_mmo_step203_server_content_archive_mounts.py"
sys.path.insert(0, str(target.parent.parent))
runpy.run_path(str(target), run_name="__main__")
