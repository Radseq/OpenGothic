# 01 Authority And Data Model

Production runtime flow:

```text
content seed/baseline -> world instance -> current projections
-> server memory materialization -> live intents/events
-> journal/projections -> replication
```

Two boot cases:
- New empty DB/new server: seed from immutable content baseline for selected
  game target/world/template revision.
- Restart/relogin existing server: load current projections and replay/validate
  journal if needed. Never reset to baseline on login.

Baseline is content, not live state:
- templates contain original NPCs/items/interactives/containers/script globals,
  guild attitudes, waynet/routines;
- current projections contain live truth;
- event journal explains divergence from baseline.

Persistent state to model:
- player character sheet/progression/resources;
- inventory, equipment, wallet;
- quest state, known/consumed dialogs, character-scoped script state;
- killed unique NPCs, tombstoned/taken/spawned world items;
- container contents, mob/interactable state;
- world-scoped script state and explicit scheduled respawn policy.

Do not canonically persist:
- transient AI targets/path queues/fight queues;
- current animation frame;
- perception queues and short cooldowns;
- camera/render/audio/input/focus.

Production-shaped table groups:
- auth/account: accounts, entitlements, bans, sessions;
- realm/shard: realms, world instances, server status;
- content/static: game targets, world templates, entity templates, item
  templates, quest/dialog metadata;
- characters: character state, stats, inventory, equipment, quests, known
  dialogs, character script state;
- persistent world: entity state, world inventory, interactives/containers,
  world script state;
- event journal: append-only gameplay ledger;
- runtime/cache outside DB: tick positions, transient AI, animation/perception.

Inventory model:
- Preserve import/source rows while rules mature.
- Server-facing layer needs durable `item_instances` plus `character_equipment`.
- Gold/food/potions/scrolls can be stack-like.
- Weapons/armor/accessories may need durable instances.
- Equipped state must not be inferred from a bag row.
- Quest/key/progression items may need unique/bind rules.
- Gothic fields `amount` and `iterator_count` can differ; keep both until rule
  decisions are explicit.

Quest/dialog/script semantics:
- Quest status mapping: `1=running`, `2=success`, `3=failed`, `4=obsolete`.
- `runtime_known_dialogs.known=1` means the player heard/selected this info.
- `permanent=0 && known=1` means consumed hidden one-shot dialog.
- `permanent=1 && known=1` means repeatable known dialog.
- Script globals are mixed character/world/server state. Classify carefully.

Identity:
- NPC direction: `world + persistent_id + symbol_index + script_id`.
- World item direction: `world + persistent_id + symbol_index + display_name`.
- Inventory: owner persistent id + item persistent id + item symbol + equipped
  + slot.
- Mobsi/interactives: vob id + tag/focus/scheme/position.
- Stable keys must not include mutable state such as position, HP, waypoint,
  amount, mob state or runtime array order.

Production DB line:
- JSON belongs to raw ingress/audit/debug/event-envelope evidence only.
- Hot authority paths need typed packets, typed indexed columns and server-owned
  transactions.
- SQL views are useful for audit/compatibility, not final gameplay truth.
- Stored procedures are proof scaffolding until C++ server owns validation and
  transaction orchestration.



Step92 identity/admin note:
- MySQL GUI tools display `BINARY(16)` UUID PK/FK columns such as `item_instance_id`, `realm_id`, `item_template_id` as `BLOB`. This is expected storage, not corrupted data. Use `BIN_TO_UUID(col,1)` or the Step92 admin views for human inspection.
- `engine_template_key` and `item_instance_key` are import/source identity strings, not final gameplay labels. They intentionally preserve world name, engine symbol/script id, source persistent id and sometimes display name so identity collisions can be diagnosed across Gothic worlds and save/runtime captures.
