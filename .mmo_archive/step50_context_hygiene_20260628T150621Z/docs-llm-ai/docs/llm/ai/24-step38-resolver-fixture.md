# 24 Step38 resolver and dev fixture

## Problem found by real run

The first Step38 gameplay capture was good on the C++ side:

```text
rows=26
apply_world_entity_damage=14
mark_npc_dead=7
apply_character_damage=2
consume_item=3
status=passed
```

The MySQL E2E failed later in the resolved worker:

```text
NPC/world entity not found for key='npc:newworld.zen:pid:258:sym:12469'
character item instance not found for symbol=7083 pid=6
```

This means the receiver/outbox path worked, but the server projection did not resolve runtime keys from the captured client session.

## Fixes

`tools/run_mmo_resolved_action_worker.py` now includes:

- NPC alias resolution from Step38 hook keys to imported runtime keys:
  - hook: `npc:<world>:pid:<pid>:sym:<symbol>`
  - import: `npc:<world>:<pid>:<symbol>:<script_id>`
- character-item resolver fallback:
  - first tries exact/persistent-id matches;
  - then uses symbol-only only when exactly one active character-owned stack can satisfy the amount.

Ambiguous matches still fail by design.

## Dev-only fixture

New tool:

```text
tools/prepare_mmo_step38_dev_fixture.py
```

It runs outside OpenGothic and may, for a selected local E2E session:

- insert/reactivate missing NPC rows from captured Step38 target keys;
- seed missing character-owned ammunition/item stacks for `consume_item`;
- write a JSON manifest with every planned/applied fixture operation.

This fixture is not production authority. It is only a local bridge for proving:

```text
client JSONL -> receiver -> outbox -> resolved worker -> MySQL procedures -> journal/projection
```

when MySQL was not imported from exactly the same runtime/save state as the client that produced the JSONL.
