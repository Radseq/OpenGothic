# 08 Step44 Live Gameplay Domains

Step44 moves beyond movement-only live server evidence. The target is to prove
that normal gameplay domains reach the server boundary, can be summarized by
coverage, and can continue through the resolved worker even when one projection
mismatch appears in the middle of a long live session.

## Current evidence from live Step43 test

The live server already accepted mixed gameplay rows:
- dialog/script: `set_known_dialog`, `set_script_int`;
- quest: `update_quest`;
- progression: `adjust_progression`;
- inventory/equipment: `pickup_world_item`, `equip_character_item`, `unequip_character_item`;
- movement authority: `movement_proposal` converted to `character_checkpoint`;
- resource/combat-side damage: `apply_character_damage`.

The real blocker seen in testing is not UDP/server intake. The blocker is a
current-state projection mismatch:

```text
pickup_world_item failed because MySQL world_entity_state/item_instances said
that the target loose item was already removed, while the current runtime save
still allowed picking it up.
```

This is expected while running repeated local tests against a DB that was not
reset/imported from the exact same runtime baseline. Do not hide it as success.
For dev-only E2E, use the projection fixture tool; for production, the server
must own world state so this mismatch cannot happen.

## New Step44 files

- `tools/check_mmo_step44_live_gameplay_domains.py`
  - reads accepted/checkpoint/rejected JSONL and optional MySQL outbox/journal;
  - reports coverage for dialog, quest, script/progression, inventory,
    equipment, drop, trade, resource, combat, kill and movement;
  - reports failed outbox rows and capture-only domains.

- `tools/run_mmo_step44_worker_followup.py`
  - optional convenience wrapper after a live capture;
  - can run the dev-only pickup projection fixture;
  - reruns the resolved worker with prefix reset and continue-on-error;
  - then builds the Step44 domain report.

- `tools/build_mmo_step44_gameplay_manifest.py`
  - packages hashes/counts of Step44 artifacts into a final manifest.

- C++ producer update:
  - `Npc::dropItem` now emits `drop_character_item` after the item is really
    spawned into the world and removed from the player inventory.
  - The resolved MySQL worker treats `drop_character_item` as capture-only
    applied no-op for Step44 because the canonical DB procedure does not exist
    yet. This keeps later unrelated session actions flowing while preserving a
    visible DB gap.

## Interpretation rules

Step44 can be green for live server-domain coverage even when not every domain
has a production DB procedure yet.

Do not claim production parity for:
- `drop_character_item` until a real DB procedure/projection mutation exists;
- repeated local pickups fixed by the dev fixture;
- combat/kill/trade unless the worker and MySQL report applied rows and journal
  evidence for the matching action kinds.

## Recommended test flow

Start the Step43/Step44 live server with outbox enqueue enabled, then play a
mixed session:

- talk to NPC and advance/receive a quest;
- read/trigger a script progression action;
- pick up an item;
- equip and unequip an item;
- drop an item;
- buy and sell with a merchant;
- attack/kill a weak NPC or creature in a disposable test world/save;
- move normally and include one legal fall.

After stopping the server, run:

```bash
python3 tools/run_mmo_step44_worker_followup.py \
  --url "mysql://gothic:gothic_dev_password@127.0.0.1:3306/gothic_mmo" \
  --session-key local-dev-PC_HERO_STEP43 \
  --prepare-pickup-fixture \
  --require-default-domains
```

Then optionally require extra domains from the gameplay you actually performed:

```bash
python3 tools/check_mmo_step44_live_gameplay_domains.py \
  --accepted-jsonl runtime/mmo_server_actions_step43.jsonl \
  --checkpoint-jsonl runtime/mmo_server_checkpoints_step43.jsonl \
  --rejected-jsonl runtime/mmo_server_rejects_step43.jsonl \
  --summary-json runtime/mmo_server_step43_summary.json \
  --url "mysql://gothic:gothic_dev_password@127.0.0.1:3306/gothic_mmo" \
  --session-key local-dev-PC_HERO_STEP43 \
  --output runtime/mmo_step44_live_gameplay_domains.json \
  --require-default-domains \
  --require-domain trade \
  --require-domain combat_damage \
  --require-domain kill \
  --require-domain drop
```

If those domains are missing, it means the gameplay action was not performed or
not captured by a C++ hook yet. If the domain is present but MySQL has failed
rows, inspect resolver/projection mismatch before changing schema.
