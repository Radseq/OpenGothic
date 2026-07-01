# 09 Step45 World AI / Weapon State / Corpse Loot

Step45 closes the first live gameplay gaps found after Step44.

Evidence from the real run showed the live server path works for many domains, but three important MMO semantics were still missing or weak:
- drawing and holstering weapons, which Gothic AI can react to through perception such as assess/remove weapon;
- looting an inventory from a dead or unconscious NPC/creature, for example sheep/wolf corpse inventory transfer;
- NPC-vs-NPC combat/death, for example a guard killing a wolf that attacked sheep.

Added C++ semantic action kinds:
- `ready_weapon`: actor changed from no weapon/other state to fist/melee/ranged/mage readied state.
- `holster_weapon`: actor changed to `WeaponState::NoWeapon`.
- `loot_npc_inventory`: player looted an item from a dead/unconscious NPC inventory.

Hook sites:
- `Npc::drawWeaponFist`, `Npc::drawWeaponMelee`, `Npc::drawWeaponBow`, `Npc::drawSpell` emit `ready_weapon` after successful state change.
- `Npc::closeWeapon` emits `holster_weapon` after successful state change and passive remove-weapon perception.
- `Npc::setToFightMode` and `Npc::setToFistMode` emit weapon-state events for script/immediate fight-mode changes.
- `Npc::addItem(size_t, Npc&, size_t)` emits `loot_npc_inventory` when the player receives items from a dead or unconscious NPC source.
- `onCharacterAttributeChanged` and `onNpcLifecycleChanged` now allow live NPC-vs-NPC damage/death capture, not only player-related combat.

Transport fix:
- `appendEscaped(...)` now escapes non-ASCII display bytes as JSON `\u00XX` escapes. This prevents local UDP packets from becoming invalid UTF-8 when Gothic labels contain legacy single-byte text such as CP1250 Polish characters.
- The server still treats display names as diagnostic labels only. Stable identity remains persistent id, symbol/script ids, world/entity keys and character keys.

Server diagnostics:
- undecodable or malformed UDP packets now write `raw_hex_prefix`, `raw_base64`, UTF-8-replacement preview and Latin-1 preview into rejected JSONL;
- `--invalid-payload-dir <dir>` optionally writes raw `.bin` files for exact packet archaeology;
- console invalid logs include byte count, hex prefix and safe preview.

Step45 tooling:
- `tools/check_mmo_step45_world_ai_weapon_loot.py` checks new domains: `weapon_state`, `corpse_loot`, world-AI combat damage and NPC kill classification.
- `tools/build_mmo_step45_world_ai_manifest.py` packages server summary, domain check and artifact counts.

Current DB interpretation:
- `ready_weapon`, `holster_weapon`, `loot_npc_inventory` are capture-only in this step. The resolved worker marks them applied no-op with resolver metadata so unrelated actions can continue.
- Do not pretend these have production DB persistence until canonical MySQL procedures/projections are added.
- Existing `apply_world_entity_damage` and `mark_npc_dead` continue to use the existing combat/lifecycle MySQL procedures; Step45 only broadens capture so NPC-vs-NPC consequences can be observed.

Recommended live test:
```bash
python3 tools/run_mmo_server.py \
  --bind 127.0.0.1:29777 \
  --accepted-jsonl runtime/mmo_server_actions_step45.jsonl \
  --rejected-jsonl runtime/mmo_server_rejects_step45.jsonl \
  --checkpoint-jsonl runtime/mmo_server_checkpoints_step45.jsonl \
  --summary-json runtime/mmo_server_step45_summary.json \
  --require-session local-dev-PC_HERO_STEP45 \
  --mysql-url "mysql://gothic:gothic_dev_password@127.0.0.1:3306/gothic_mmo" \
  --account-name local-import \
  --character-key PC_HERO \
  --db-session-key local-dev-PC_HERO_STEP45 \
  --enqueue-outbox \
  --truncate \
  --invalid-payload-dir runtime/mmo_server_invalid_step45 \
  --require-motion-state-for-large-fall
```

Gameplay to perform:
- draw weapon near NPCs, then holster it;
- fight a wolf/sheep/creature;
- let an NPC kill another NPC/creature if possible;
- loot the dead/unconscious body;
- optionally repeat normal Step44 coverage: dialog, quest, pickup, equip, drop, trade.

After stopping the server:
```bash
python3 tools/run_mmo_resolved_action_worker.py \
  --url "mysql://gothic:gothic_dev_password@127.0.0.1:3306/gothic_mmo" \
  --worker-id dev-resolved-worker \
  --session-key local-dev-PC_HERO_STEP45 \
  --max-actions 500 \
  --continue-on-error
```

Check:
```bash
python3 tools/check_mmo_step45_world_ai_weapon_loot.py \
  --accepted-jsonl runtime/mmo_server_actions_step45.jsonl \
  --checkpoint-jsonl runtime/mmo_server_checkpoints_step45.jsonl \
  --rejected-jsonl runtime/mmo_server_rejects_step45.jsonl \
  --summary-json runtime/mmo_server_step45_summary.json \
  --url "mysql://gothic:gothic_dev_password@127.0.0.1:3306/gothic_mmo" \
  --session-key local-dev-PC_HERO_STEP45 \
  --output runtime/mmo_step45_world_ai_weapon_loot_check.json \
  --require-default-domains \
  --require-world-ai-domains
```

If `corpse_loot` is missing, loot did not go through `Npc::addItem(size_t, Npc&, size_t)` or the source was not dead/unconscious at transfer time.
If `npc_kill` is missing but `kill` is present, the killer was the player or source actor was not available in the hook.
If invalid packet diagnostics remain after the C++ escape fix, inspect `raw_base64`/`.bin` because the problem is then likely truncation or a non-JSON datagram, not Polish text.
