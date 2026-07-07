# MMO server perception reaction UDP - step 181

## What changed

Perception reactions now have a first real server-to-client UDP path through the
existing `ServerLiveDelta` packet.

Added:

- `ServerLiveDeltaKind::PerceptionReaction`
- client live-delta domain name `perception_reaction`
- server pending queue for perception reaction live deltas
- UDP send after accepted direct server packet processing

## Runtime flow

Current flow:

1. Client sends a typed UDP action.
2. Server direct handler records a Gothic II perception event.
3. Server queues the event, evaluates witnesses and plans the reaction.
4. If the plan has `needsUdpReplication`, the server creates a
   `ServerLiveDeltaKind::PerceptionReaction`.
5. After ACK, the server sends that live delta over UDP.

The live delta includes:

- perception id/name,
- reaction kind,
- source/other/victim/item keys,
- witness counts,
- script/interrupt/hostility flags,
- optional origin position.

## Boundary

This is not final interest management yet.

For now, the live delta is sent to the remote endpoint that sent the triggering
UDP packet. That proves the wire path and keeps gameplay out of the database.

The final MMO version needs a runtime client registry/AOI broadcaster so every
player inside the relevant range receives the same server-authored reaction.

## Important rule

The live-delta is still an event notification, not authoritative script
execution. Exact guard/faction/attitude behavior must come from inspected
Gothic II scripts/perception handlers before mutating server AI state.

## Verified

Built successfully:

```text
cmake --build build\mmo_cpp_server --target mmo_udp_server --config Debug
cmake --build build --target Gothic2Notr --config Debug
```

Existing unrelated warnings remain:

- `mmo_udp_server_payload_mapper.inl`: `slot` shadowing,
- `camera.cpp`/`gamemusic.cpp`/`packedmesh.cpp`: existing MSVC warnings.

## Next good step

Add a small runtime UDP client registry and AOI send helper:

- remember active UDP endpoints by session/character key,
- remember latest player/NPC positions from movement and NPC state packets,
- broadcast `PerceptionReaction` only to clients within perception/live range,
- keep it runtime-only and independent of the temporary database schema.
