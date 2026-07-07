# 86. Typed movement/checkpoint UDP packet - step 175

## Context

Step 174 removed JSON from the client-to-server combat damage wire format. The next highest-volume path was movement: `MovementProposal` and `CharacterCheckpoint` still used `ClientActionPacket::payloadJson` over UDP, even though the JSON copy should be treated as debug/audit material.

## Change

- Added `PacketKind::ClientMovement` to `game/game/mmonetprotocol.h`.
- Added `ClientMovementPacket`, `ClientMovementStats`, movement state flags and binary encode/decode helpers.
- Switched `mmosemanticactionsink.cpp` so server-bound `MovementProposal` and `CharacterCheckpoint` use the typed movement packet.
- Kept JSONL capture intact for local debug.
- Added server-side decode in `mmo_udp_server_main_loop.inl`.
- Added a local compatibility bridge that rebuilds the movement/checkpoint payload after UDP decode so existing direct-DB handlers, movement authority validation, live world refresh checks and live-delta builders keep working.

## Wire-vs-debug boundary

For movement/checkpoint, UDP no longer transports JSON. JSON remains:

- client-side JSONL debug/audit,
- server-side compatibility payload after a binary packet has already been decoded,
- existing generic action fallback for domains not migrated yet.

## Notes

The compatibility bridge deliberately emits both `pos_x`/`rotation_yaw` and `to_pos_x`/`to_rotation_yaw` for checkpoints. This preserves checkpoint persistence and also lets the existing live-delta position helper treat checkpoint packets like movement packets.

Next candidates:

- typed bootstrap request and typed bootstrap/snapshot manifest metadata,
- typed inventory/equipment action packets,
- replacing compatibility payload bridges with direct typed request structs in each migrated server domain.

## Verification

- `verify_movement_packet.cpp` builds and passes a binary encode/decode round-trip for `ClientMovementPacket`.
- `mmonetprotocol.h` passes a standalone `g++ -std=c++20 -fsyntax-only` check.
- Full OpenGothic build was not attempted here because the scratch workspace does not include the full engine dependency set.
