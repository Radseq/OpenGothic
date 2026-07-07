# MMO server live delta channel - step 167

## Cel

Dodany zostal pierwszy typowany kanal `server -> client` dla modulow, ktore sa juz wykonywane po stronie serwera przez `direct_db`. Dotychczas klient widzial glownie `ServerAck`, diagnostyke i snapshoty bootstrap/live-world. To wystarczalo do potwierdzenia pakietu, ale nie dawalo modularnego miejsca na pozniejsze korekty inventory, equipment, combat, story czy ruchu.

## Zmiany protokolu

- `PacketKind::ServerLiveDelta = 5`.
- `ServerLiveDeltaKind` grupuje delty domenowo:
  - `MovementCorrection`
  - `CharacterStats`
  - `Inventory`
  - `Equipment`
  - `WorldItem`
  - `InteractiveState`
  - `Combat`
  - `Story`
  - `Generic`
- `ServerLiveDeltaPacket` niesie:
  - `packetSequence`
  - `localSequence`
  - `kind`
  - `actionKind`
  - `payloadJson`
- Protokol ma symetryczne `encodeServerLiveDeltaPacket` i `decodeServerLiveDeltaPacket`.

## Serwer

Serwer wysyla `ServerLiveDelta` po zaakceptowanych akcjach `direct_db`, ktore sa juz obslugiwane po stronie C++. Kanal jest podlaczony po ACK/diagnostyce, przed ewentualnym snapshotem. Nie wymaga nowej procedury SQL, widoku ani zmiany schematu.

Payload ma schemat `mmo.server_live_delta.v1` i zawiera metadane korelacyjne:

- `source`
- `action_kind`
- `direct_label`
- `target_key`
- `idempotency_key`
- `client_packet_sequence`
- `client_local_sequence`
- `server_tick`
- `accepted`
- `ready`
- `delta_kind`
- opcjonalnie `client_payload`

Bardzo glosne obserwacje NPC (`record_npc_routine_state`, `record_npc_ai_state`, `record_npc_path_state`, `record_npc_fight_state`, `record_npc_action_state`) sa celowo pominiete w live-delta. Nadal moga isc przez dotychczasowe direct DB/obserwacyjne sciezki, ale nie zapychaja kanalu korekcyjnego.

## Klient

Klient rozpoznaje `ServerLiveDelta`, liczy statystyki i zapisuje pakiety do:

```text
runtime/mmo_server_live_deltas.jsonl
```

Plik jest resetowany przy nowym bootstrap receive, analogicznie do artefaktow snapshotu. Manifest bootstrapu dostal pole `live_deltas_seen`, a podsumowanie ACK loguje liczbe i bajty live-delta.

## Granica odpowiedzialnosci

Ten krok nie aplikuje jeszcze delty bezposrednio do stanu gry klienta. To swiadoma granica: najpierw powstal stabilny, typowany kanal komunikacji i audytu. Nastepny krok powinien dodawac domenowe konsumery, np.:

- `MovementCorrection` -> korekta pozycji/vel/state z progami tolerancji.
- `Inventory`/`Equipment` -> nadpisanie lokalnego widoku po accepted DB.
- `CharacterStats`/`Combat` -> HP/mana/protection/damage result z serwera.
- `Story`/`InteractiveState` -> quest/dialog/script/interactives jako server truth.

## Zmienione pliki

- `game/game/mmonetprotocol.h`
- `game/game/mmosemanticactionsink.cpp`
- `server/cpp/mmo_udp_server_transport_diagnostics.inl`
- `server/cpp/mmo_udp_server_main_loop.inl`
