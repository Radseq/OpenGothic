# Step 179 - typed economy and session-control UDP

## Cel

Domkniecie kolejnej grupy akcji, ktore nadal mogly uzywac legacy `ClientAction` z JSON payloadem na UDP.
Ten etap dodaje osobne typed pakiety dla ekonomii oraz kontroli sesji/systemu.

## Nowe pakiety

Dodano:

- `PacketKind::ClientEconomy`
- `PacketKind::ClientSessionControl`

## ClientEconomy

Obslugiwane akcje:

- `WalletDelta`
- `GrantGold`
- `SpendGold`

Typed pola:

- actor/character/target,
- currency key/display name,
- amount, delta amount,
- wallet before/after,
- optional item template symbol,
- world/reason,
- optional actor position.

## ClientSessionControl

Obslugiwane akcje:

- `ClientBootstrapRequest`
- `ClientCorrectionAck`
- `SaveCheckpointManifest`

Typed pola:

- actor/character/display/world,
- server endpoint i server-bound-client-mode,
- correction ack: action kind + acknowledged local sequence,
- save checkpoint: manifest key, checkpoint kind, save slot, native path, display name,
- native-save/db-save flags.

## Klient

`mmosemanticactionsink.cpp` wybiera teraz server-bound UDP w kolejnosci:

1. typed combat damage,
2. typed movement/checkpoint,
3. typed inventory/equipment/container/trade,
4. typed world/story state,
5. typed NPC state,
6. typed economy,
7. typed session-control,
8. legacy `ClientAction` tylko dla nieprzeniesionych akcji.

## Serwer

`mmo_udp_server_main_loop.inl` dekoduje nowe pakiety i tworzy lokalny payload zgodnosci dla obecnych direct DB/outbox handlerow.
Nie dodano procedur ani widokow SQL.

## JSON

UDP wire-format dla tych akcji jest binarny.
JSON pozostaje jako:

- klientowy JSONL debug/audit,
- serwerowy lokalny payload zgodnosci,
- DB bridge evidence/log.

## Weryfikacja

- `ClientEconomyPacket` encode/decode round-trip dla `GrantGold`.
- `ClientSessionControlPacket` encode/decode round-trip dla `ClientBootstrapRequest`.
- `mmonetprotocol.h` syntax-only C++20.
