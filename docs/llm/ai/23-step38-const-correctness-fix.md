# 23 Step38 const-correctness build fix

## Symptom

The Step38 C++ producer failed to compile in `game/game/mmosemantichooks.cpp`:

```text
error: passing const Npc as this argument discards qualifiers
```

The failing helper functions called `Npc::world()`, while the engine exposes only the non-const API:

```cpp
World& Npc::world();
```

## Fix

The Step38 helper path now uses mutable NPC references/pointers where world access is required:

- `shouldCapturePlayerRelated(Npc& actor, Npc* other)`
- `appendNpcIdentity(std::string&, const char*, Npc&)`
- `onCharacterAttributeChanged(..., Npc* sourceActor, ...)`
- `onNpcLifecycleChanged(..., Npc* sourceActor, ...)`

No `const_cast` is used and the engine API is not changed. The hook still only reads identity, world name, tick and position data, then submits immutable semantic envelopes through the semantic action sink.

## Validation

Re-run:

```bash
python3 -m py_compile \
  tools/run_mmo_action_receiver.py \
  tools/run_mmo_resolved_action_worker.py \
  tools/check_mmo_step38_trade_combat_jsonl.py \
  tools/check_mmo_step38_trade_combat_mysql.py \
  tools/run_mmo_step38_trade_combat_e2e.py

cmake --build build -j"$(nproc)"
```
