# Step 177 - typed world/story state UDP

## Cel

Kolejny krok po typed combat, movement i inventory.
Ten etap usuwa JSON z UDP dla szerokiego klastra akcji world/story/script, zostawiajac JSON jako lokalny format debug/audit oraz tymczasowy payload zgodnosci po stronie serwera.

## Nowy packet

Dodano `PacketKind::ClientWorldState` oraz `ClientWorldStatePacket`.

Obslugiwane akcje:

- `ReadyWeapon`
- `HolsterWeapon`
- `UseInteractive`
- `UpdateInteractiveState`
- `SetScriptInt`
- `UpdateQuest`
- `SetKnownDialog`
- `AdjustProgression`
- `ApplyExperienceReward`
- `ApplyCharacterResourceDelta`
- `ConsumeMana`
- `WorldTimeChanged`
- `TriggerEvent`
- `MoverStateChanged`
- `RecordNpcDialogLine`
- `RecordTriggerQueueState`
- `RecordWorldTransitionState`

## Zawartosc typed payloadu

Packet przenosi wspolne pola dla tego klastra:

- actor/character/target/world/reason,
- interactive/entity/trigger/mover keys,
- script key, global key, symbol/value indexes i script function context,
- quest key, status i entry count,
- known-dialog npc/info keys oraz flagi known/removed,
- progression/resource before/after/delta,
- world time before/after,
- trigger event type/target/emitter,
- mover/interactive state before/after, frames, masks i lock/crack/container/door/ladder flags,
- dialog conversation/speaker/listener/output/subtitle/duration,
- actor/target/source positions.

## Klient

`mmosemanticactionsink.cpp` buduje `ClientWorldStatePacket` z payloadu debugowego i wysyla go binarnie UDP.
Kolejnosc wyboru transportu server-bound UDP:

1. typed combat damage,
2. typed movement/checkpoint,
3. typed inventory/equipment/container/trade,
4. typed world/story state,
5. legacy `ClientAction` dla jeszcze nieprzeniesionych akcji.

## Serwer

`mmo_udp_server_main_loop.inl` dekoduje `ClientWorldStatePacket` i tworzy lokalny payload zgodnosci dla aktualnych direct DB/outbox handlerow.
To zachowuje istniejace walidatory i persistence bez dodawania widokow ani procedur.

## JSON

UDP wire-format dla wymienionych akcji jest binarny.
JSON moze nadal wystepowac jako:

- klientowy JSONL debug/audit,
- serwerowy lokalny payload zgodnosci,
- DB bridge evidence/log.

## Weryfikacja

- `ClientWorldStatePacket` encode/decode round-trip dla `UpdateInteractiveState`.
- `mmonetprotocol.h` syntax-only C++20.
