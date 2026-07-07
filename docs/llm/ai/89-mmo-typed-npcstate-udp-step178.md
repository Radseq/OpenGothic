# Step 178 - typed NPC state UDP

## Cel

Kontynuacja usuwania JSON-over-UDP z komunikacji klient-serwer.
Po typed combat, movement, inventory i world/story state ten krok przenosi obserwacje NPC do osobnego binarnego pakietu.

## Nowy packet

Dodano `PacketKind::ClientNpcState` oraz `ClientNpcStatePacket`.

Obslugiwane akcje:

- `MarkNpcDead`
- `RespawnNpc`
- `RecordNpcRoutineState`
- `RecordNpcAiState`
- `RecordNpcPathState`
- `RecordNpcFightState`
- `RecordCombatIntent`
- `RecordNpcActionState`

## Zawartosc typed payloadu

Packet niesie pola potrzebne obecnym handlerom NPC:

- NPC identity: entity key, actor/target/source aliases, persistent id, symbol, display name i world,
- lifecycle: dead, unconscious, down, health current/max,
- routine/path: routine state, schedule, route, current/next/target waypoint,
- AI/action: ai state/function/intent/target, perception state, action key/state/sync group,
- combat intent/fight: opponent, fight/attack state, weapon/body state, combo, animation names i timing windows,
- combat geometry: actor position, target position, attacker/opponent center, fight distance, yaw/range values.

## Klient

`mmosemanticactionsink.cpp` buduje `ClientNpcStatePacket` z payloadu debugowego i wysyla go binarnie UDP.
Kolejnosc transportu server-bound UDP:

1. typed combat damage,
2. typed movement/checkpoint,
3. typed inventory/equipment/container/trade,
4. typed world/story state,
5. typed NPC state,
6. legacy `ClientAction` tylko dla jeszcze nieprzeniesionych akcji.

## Serwer

`mmo_udp_server_main_loop.inl` dekoduje `ClientNpcStatePacket` i odtwarza lokalny payload zgodnosci dla istniejacych direct DB/outbox handlerow.
Nie dodano nowych widokow ani procedur SQL.

## JSON

UDP wire-format dla wymienionych akcji NPC jest binarny.
JSON moze nadal istniec jako:

- klientowy JSONL debug/audit,
- lokalny payload zgodnosci po dekodzie na serwerze,
- DB bridge evidence/log.

## Weryfikacja

- `ClientNpcStatePacket` encode/decode round-trip dla `RecordNpcFightState`.
- `mmonetprotocol.h` syntax-only C++20.
