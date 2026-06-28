# C++ Semantic Event Registry Parity

`game/game/mmosemanticevents.h` now matches the SQL dispatch/replay registry more closely.

The registry includes convenience actions `grant_gold`, `spend_gold` and `apply_experience_reward`, and exposes `procedureName(...)` alongside action kind, event type and event class.

Event class parity fixes:

- wallet actions use `inventory`;
- NPC death/respawn actions use `combat`.

This avoids future false mismatches between C++ action envelopes, MySQL dispatch contracts and replay validation.
