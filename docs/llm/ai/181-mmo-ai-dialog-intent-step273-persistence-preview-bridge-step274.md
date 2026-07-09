# Step274 - Step273 Persistence Preview Bridge

## Goal

Bridge the late-observer resume commit-preflight chain to the Step273 durable
storage contract without enabling MySQL mutation or gameplay side effects.

## C++ Changes

- Added `server/cpp/mmo_ai_dialog_intent_delivery_persistence_bridge.h`.
- Added `--ai-dialog-intent-step273-persistence-preview` and a matching
  `--enable-...` alias.
- The bridge builds typed previews for:
  - conversation session upsert;
  - gameplay delivery sent procedure call;
  - conversation observer upsert;
  - future gameplay delivery receipt call;
  - future gameplay delivery dead-letter call.

## Safety Contract

The Step274 path is disabled by default. Even when enabled, it only logs a
preview summary from the late-observer resume commit-preflight path.

Hard stops:

- no MySQL connection is opened;
- no `runMysql` call is made;
- no replay UDP packet is sent;
- no runtime delivery/observer mutation is performed by this bridge;
- no client subtitle/audio is applied;
- no AI action is marked applied.

## Evidence

The server emits:

`[server_ai_dialog_intent_step273_persistence_preview]`

Expected fields include `status`, `ready`, `statements`, `statement_names`,
`execute_mysql=0` and `mutated_db=0`.

## Next Work

Add a no-execute executor/result boundary for this preview bridge. Real DB and
tools changes remain deferred until they can be applied together as one planned
batch.
