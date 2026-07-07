# MMO client live delta main-thread apply - step 169

## Cel

Krok 168 dal klientowi cache `runtime/mmo_server_live_delta_latest_<domain>.json` z autorytatywnymi slice'ami serwera. Krok 169 zaczyna je konsumowac na glownym watku gry, bez pisania stanu gry z watku UDP.

## Co jest aplikowane

`GameSession::pollMmoServerLiveDeltas` odpala sie po dotychczasowym pollingu snapshotow i czyta domenowe pliki latest:

- `movement`
- `character`
- `inventory`
- `equipment`
- `combat`

Na razie aplikuje tylko domeny, ktore maja juz sprawdzone restore/applier path:

- `authoritative_character.stats` -> `Npc::restorePersistentStats`
- `authoritative_inventory` + `authoritative_equipment` -> `Npc::restorePersistentInventory`
- `authoritative_character.position` -> pozycja HERO, ale tylko przy duzym drifcie albo duzej roznicy yaw

Wszystko idzie przez `Mmo::Hooks::ScopedCaptureSuppression`, zeby materializacja stanu z serwera nie wracala od razu jako nowa akcja klienta.

## Bezpieczenstwo movement

Accepted movement delta nie jest traktowana jak kazdy-frame teleport. Pozycja jest nakladana tylko, gdy lokalny HERO odbiegnie mocno od autorytatywnego slice'a:

- dystans >= 1500 jednostek, albo
- yaw delta >= 45 stopni

Mniejsze roznice zostaja lokalnie plynne. Rejected/correction path dalej zostaje obslugiwany przez dotychczasowy snapshot correction flow.

## Co zostaje tylko cache'owane

Story, interactive/world item i pelniejsze world-state nadal sa cache'owane przez krok 168, ale nie sa jeszcze aplikowane tutaj. Te domeny maja wieksze ryzyko konfliktu z lokalnym skryptem, dialogiem, moverami i aktywnym stanem swiata, wiec powinny dostac osobne, waskie apply policy.

## Zmienione pliki

- `game/game/gamesession.cpp`
- `game/game/gamesession.h`
