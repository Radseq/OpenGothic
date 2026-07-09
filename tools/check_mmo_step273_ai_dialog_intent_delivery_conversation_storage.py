#!/usr/bin/env python3
from __future__ import annotations

import runpy
from pathlib import Path

runpy.run_path(
    str(Path(__file__).resolve().parent / "validation" / "check_mmo_step273_ai_dialog_intent_delivery_conversation_storage.py"),
    run_name="__main__",
)
