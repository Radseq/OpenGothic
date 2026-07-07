# 85. Typed combat damage UDP packet - step 174

## Context

Previous steps moved live deltas away from JSON-on-wire and added richer client-side damage observations for the server damage calculator. The remaining client-to-server hot path still used `ClientActionPacket::payloadJson`, so damage observations were transported as JSON even though the JSON was meant to be debug/audit material.

## Change

- Added `PacketKind::ClientCombatDamage` to `game/game/mmonetprotocol.h`.
- Added `ClientCombatDamagePacket`, `ClientCombatProfile`, damage kind/modifier enums, flags and encode/decode helpers.
- Switched `mmosemanticactionsink.cpp` so `ApplyCharacterDamage` and `ApplyWorldEntityDamage` use the typed binary packet in server-bound UDP mode.
- Kept JSONL capture unchanged for local debug/audit.
- Added server-side decoding for the typed combat-damage datagram in `mmo_udp_server_main_loop.inl`.
- Added a local compatibility bridge that rebuilds the existing direct-DB payload from typed fields after UDP decode. This keeps current combat handlers stable while removing JSON from the damage wire format.

## Notes

This is intentionally modular. It does not rewrite all semantic actions at once and does not add SQL views/procedures. The next good targets are:

- typed movement/checkpoint packets,
- typed bootstrap request/snapshot metadata,
- typed inventory/equipment action packets,
- replacing the server-side compatibility JSON bridge with direct typed handler inputs once each domain is migrated.

## Verification

- `verify_combat_packet.cpp` builds and passes a binary encode/decode round-trip for `ClientCombatDamagePacket`.
- `mmonetprotocol.h` passes a standalone `g++ -std=c++20 -fsyntax-only` check.
- Full OpenGothic build was not attempted here because the scratch workspace does not contain the full engine dependency set.
