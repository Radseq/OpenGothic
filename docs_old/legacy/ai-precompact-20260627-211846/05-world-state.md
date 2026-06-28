# World State

Stable identity rules:

- NPC: world, persistent id, symbol index, script id.
- World item: world, persistent id, symbol index, script id.
- Interactive: world, stable VOB id.

Mutable state belongs in row fields or delta events, never in identity. Persist NPC stats through the component signature table `runtime_npc_stat_capture_state`; query/write EAV rows only when that exact component changed. Keep AI paths, animations, and active queues transient unless a safe restore contract exists.

