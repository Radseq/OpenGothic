# Save Serialization

Files: `game/game/serialize.h`, `game/game/serialize.cpp`, `game/game/savegameheader.*`.

`Serialize` is the compatibility format adapter for normal Gothic saves. It addresses named archive entries and tracks global/world save versions.

- `GameSession::save` sets `Serialize::Version::Current` in the header.
- `GameSession(Serialize&)` reads the header and calls `setGlobalVersion` before loading state.
- World-object persistence uses named entries under `worlds/<world>/...`.

Use this path to compare DB restore against native behavior. Do not make serialized blobs the authoritative MMO database model.

