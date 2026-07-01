# 17 Step36 Evidence Package

Step36 v1.3 adds a packaging tool for the passed vertical-slice artifact.

Purpose:
- prevent losing the exact files that proved the slice;
- keep one deterministic manifest for client JSONL, server JSONL, recovered server JSONL, Step36 artifact, SQLite DB hash/summary and optional native save files;
- make future comparison easier when full `.sav + SQLite + MySQL` restore parity is added.

Tool:

```bash
tools/package_mmo_step36_evidence.py
```

Typical command:

```bash
python3 tools/package_mmo_step36_evidence.py \
  --session-key local-dev-PC_HERO_STEP35V2 \
  --artifact runtime/mmo_step36_vertical_slice_STEP35V2.json \
  --client-jsonl runtime/mmo_actions.jsonl \
  --server-jsonl runtime/mmo_server_actions_step35v2.jsonl \
  --recovered-server-jsonl runtime/mmo_server_actions_step35v2.recovered.jsonl \
  --sqlite runtime/g2notr.sqlite \
  --output-dir runtime/evidence/step36_STEP35V2 \
  --zip runtime/evidence/step36_STEP35V2.zip \
  --strict
```

Output:
- `manifest.json`: machine-readable evidence manifest;
- `summary.md`: human-readable summary;
- `package.sha256.json`: stable hashes for package control files;
- copied JSON artifacts under `artifact/` and `jsonl/`;
- SQLite hash and table summary; SQLite is not copied unless `--copy-sqlite` is provided;
- optional native save files supplied with `--native-save`.

Interpretation:
- `package_status=passed` means the packaged Step36 artifact already had `status=passed`.
- This is still vertical-slice evidence. Do not mark full restore parity as passed unless the same package includes a fresh native `.sav`, SQLite save-slot and MySQL projection proof from one controlled scenario.
