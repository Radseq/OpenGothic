# Next Work

## Immediate Step

Build the first read-only `world_instance` content cache on top of the Step226
`RuntimeReadModel` indexes.

This is a server-side content/cache step. It should not add live NPC AI yet and
should not add new client authority.

## Target Inputs

- active content revision;
- exported `mmo.content_build_runtime_read_model.v1` payload;
- Step226 `RuntimeReadModel` indexed data;
- server world instance key/world name.

## Target Cache APIs

Expose read-only queries for future server systems:

- resolve world ZEN entities by world/kind/key;
- resolve waypoint edges by world/from/to;
- resolve NPC templates by instance;
- resolve item templates by instance;
- enumerate routines by NPC instance and by routine symbol;
- enumerate perception bindings by kind and owner;
- resolve dialog infos and dialog outputs by symbol/name.

## Expected Result

- Server code has one immutable content cache per selected `world_instance`.
- Cache construction consumes Step226 indexes without re-parsing JSON in gameplay code.
- Cache owns or explicitly references data with clear lifetimes.
- Probe/check code can verify deterministic counts/lookups.
- No live NPC/script tick yet.

## Acceptance Checks

- Existing `mmo_runtime_read_model_probe` still reports `status=ready`.
- Existing Step226 index counts and deterministic lookup checks remain stable.
- New cache checks fail loudly when required data is missing.
- No new gameplay authority is added on the client.
- No DB mutation is introduced for this cache step.
- Native single-player build still compiles.

## Then

Integrate the cache into server startup/session bootstrap:

- select approved content revision;
- load runtime read-model once;
- bind cache to active world instances;
- expose read-only queries to future NPC/perception systems.

## After That

Start server NPC/perception policy as a read-only assessment pass:

- active players in world instance;
- active NPCs in world instance;
- spatial/distance query;
- perception binding lookup;
- cooldown/readiness check;
- write decision to `mmo_ai_runtime`;
- do not broadcast movement/dialog until dispatch contract is integrated.

## Defer

Do not implement yet:

- full Daedalus VM execution for all scripts;
- per-player script VMs;
- live NPC path/movement replication;
- trade/economy rewrite;
- final production DB rewrite.

These require the read-model cache and `world_instance` boundary first.
