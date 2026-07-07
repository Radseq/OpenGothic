# MMO typed dialog UDP - step 180

Ten krok wydziela dialogi z ogolnego `ClientWorldState` do osobnego binarnego pakietu UDP.

## Cel

JSON nie powinien byc formatem komunikacji klient <=> serwer. Zostaje tylko jako:

- lokalny zapis debug/jsonl po stronie klienta,
- lokalny compatibility bridge po stronie serwera do istniejacych handlerow DB/outbox.

## Nowy pakiet

Dodano:

- `PacketKind::ClientDialogState`
- `ClientDialogStatePacket`
- `encodeClientDialogStatePacket`
- `decodeClientDialogStatePacket`
- `isDialogStatePacketAction`

Obslugiwane akcje:

- `SetKnownDialog`
- `RecordNpcDialogLine`

## Zakres danych

Pakiet przenosi binarnie:

- klucze sesji, celu i idempotencji,
- `character_key`, `actor_key`, `world`, `reason`,
- `npc_key`, `npc_symbol`, `npc_symbol_name`,
- `info_key`, `info_symbol`, `info_symbol_name`,
- `conversation_key`, `sync_group`,
- `speaker_key`, `listener_key`,
- `output_name`, `message_name`, `subtitle_text`,
- `dialog_state`, `topic_key`,
- `line_index`, `output_index`, `duration_ms`,
- flagi: `known`, `player_line`, `npc_line`, `important`, `ambient`.

## Klient

`QueuedSemanticActionSink` probuje teraz budowac `ClientDialogStatePacket` przed `ClientWorldStatePacket`.
Dzieki temu dialogi nie sa juz przejmowane przez modul world-state.

Kolejnosc typed UDP:

1. combat damage,
2. movement,
3. inventory,
4. dialog,
5. world/story,
6. NPC state,
7. economy,
8. session-control,
9. legacy `ClientAction` tylko jako ostatni fallback.

## Serwer

`mmo_udp_server_main_loop.inl` dekoduje `ClientDialogState` i buduje lokalny payload zgodny z dotychczasowym `ClientActionPacket`.
Ten payload nie jest formatem UDP; to pomost do obecnej sciezki wykonania po stronie serwera.

## Weryfikacja

Sprawdzone:

- round-trip `ClientDialogStatePacket` dla `RecordNpcDialogLine`,
- round-trip `ClientDialogStatePacket` dla `SetKnownDialog`,
- `mmonetprotocol.h` przez `g++ -std=c++20 -fsyntax-only`.

