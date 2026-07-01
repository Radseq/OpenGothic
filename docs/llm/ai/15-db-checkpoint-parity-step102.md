# 15 DB Checkpoint Parity Step102

State before Step102:
- Strict DB-save-checkpoint restore works end to end with
  `--require-db-save-checkpoint-restore`.
- The server can export a latest DB checkpoint as `mmo_bootstrap_snapshot_v1`.
- The client can start guarded DB Continue from that snapshot and no longer
  needs the native `.sav` as the authoritative source in this dev path.
- Normal accepted `movement_proposal` logs are coalesced, but repeated
  `ready_weapon` / `holster_weapon` packets could still spam the server log.

Step102 adds two things:

1. Runtime server log policy:
   - accepted, diagnostic-free `ready_weapon` and `holster_weapon` packets are
     coalesced like movement proposals;
   - use `[weapon_state_summary]` and final summary counters instead of
     per-packet `last=ready_weapon` spam;
   - rejected weapon state packets, diagnostics, bootstrap, item, combat, story,
     save checkpoint and snapshot send lines still log explicitly.

2. `tools/check_mmo_step102_db_checkpoint_parity.py`:
   - inspects the latest real save checkpoint for a session/character;
   - checks required Step97/Step98 routines, views and snapshot tables;
   - reports latest manifest metadata;
   - compares actual snapshot table counts against live projection counts for:
     character, inventory, equipment, quests, known dialogs, script state,
     world entities, world items, interactives, NPC lifecycle, world inventory,
     world clock and movers;
   - probes the exported JSON snapshot and reports array lengths for the client
     contract sections;
   - can fail CI/dev loops with `--assert-strict` and optionally
     `--assert-no-drift`.

Use after a real in-game save and before strict DB Continue:

```bash
cd ~/Desktop/OpenGothic

python3 tools/check_mmo_step102_db_checkpoint_parity.py \
  --url "mysql://gothic:gothic_dev_password@127.0.0.1:3306/gothic_mmo_ch1_clean" \
  --session-key local-dev-PC_HERO_TEST \
  --character-key PC_HERO \
  --assert-strict \
  --output runtime/step102_db_checkpoint_parity/check.json
```

For a strict save -> exit -> continue test with no extra gameplay after save,
also use:

```bash
python3 tools/check_mmo_step102_db_checkpoint_parity.py \
  --url "mysql://gothic:gothic_dev_password@127.0.0.1:3306/gothic_mmo_ch1_clean" \
  --session-key local-dev-PC_HERO_TEST \
  --character-key PC_HERO \
  --assert-strict \
  --assert-no-drift \
  --output runtime/step102_db_checkpoint_parity/check_no_drift.json
```

Interpretation:
- `strict_ready=true` means latest checkpoint can be exported as
  `snapshot_source=db_save_checkpoint_v1` and passes strict restore validation.
- `no_drift=true` means snapshot table counts and live projection counts match
  for the checked domains.
- `no_drift=false` is expected if gameplay continued after the save; inspect
  `count_drift` to see which domain changed.
- If `strict_ready=false`, do not debug the client first. Inspect missing
  routines/views/tables, `strict_restore`, `validation` and `export_probe`.

This is a gate before deeper NPC/dialog/waypoint migration. It gives a concrete
domain-by-domain answer to: "did DB checkpoint restore lose anything important
before the client started?"

