# 08 C++ Hook Bootstrap Filter

Problem found by first JSONL smoke after Step 32/34:
- JSONL validation passed, so the sink and hook call path worked.
- The first run produced thousands of `equip_character_item` / `unequip_character_item` actions at `client_tick=0`.
- Payload actors were `npc:*`, positions were `0/0/0`, and many item persistent ids were `4294967295`; this is bootstrap/load materialization, not accepted gameplay intent.

Fix policy:
- Do not submit gameplay actions while `World::tickCount()==0`.
- Equipment and world-pickup hooks capture player actions only for now. NPC routine/bootstrap equipment is not a client/server intent.
- Generic `Inventory::transfer` only captures after live tick and only when source is player or source owner is unknown (`nullptr`) for chest/container transfers. Non-player NPC transfers are ignored until a richer owner-aware hook is added at `Npc`/`Interactive` boundaries.
- This keeps the current dev JSONL useful for first parity slices: player pickup and player equip/unequip.

Do not remove these filters when adding server transport. Instead add explicit, typed server intents for NPC/trade/combat later.

Cold server-design note:
- Filtering bootstrap equipment out of live JSONL does not mean the server should ignore NPC equipment.
- A server can load NPC equipment/state from baseline/projection tables during startup, then emit only later accepted gameplay changes as live semantic actions.
- Keep this as a useful design hint, not a mandatory implementation rule until the server/replay architecture proves it.
