# Code Map

## New Game

Plik: `game/game/gamesession.cpp`

Wazne miejsca:

- `GameSession::GameSession(std::string file)` - tworzy nowa sesje gry.
- Tworzy `GameScript`, laduje `World`, tworzy gracza przez `wrld->createPlayer(hero)`.
- Wykonuje `wrld->postInit()`.
- Dla normalnej gry wykonuje `initScripts(true)`.
- Wykonuje `wrld->triggerOnStart(true)`.
- Resetuje kamere przez `cam->reset(wrld->player())`.
- Ustawia `ticks = 1`.

To jest najlepszy punkt startowy do automatycznego eksportu initial state. Eksporter powinien dzialac po zakonczeniu inicjalizacji new game, najlepiej po pierwszym kontrolowanym ticku albo w jasno oznaczonym miejscu po `triggerOnStart(true)`.

## Save Game

Plik: `game/game/gamesession.cpp`

Wazne miejsca:

- `GameSession::GameSession(Serialize& fin)` - ladowanie zapisu gry.
- `GameSession::save(Serialize& fout, std::string_view name, const Pixmap& screen)` - zapis calej sesji.

`GameSession::save` zapisuje:

- header
- preview
- `game/session`
- kamera
- visited worlds
- aktywny world przez `wrld->save(fout)`
- percepcje
- questy
- zmienne Daedalusa

## World Snapshot

Plik: `game/game/worldstatestorage.cpp`

Wazne miejsce:

- `WorldStateStorage::WorldStateStorage(World& w)`

Ta klasa serializuje `World` do bufora pamieci przez:

```cpp
Tempest::MemWriter wr{storage};
Serialize sr{wr};
w.save(sr);
```

To jest wazny precedens: silnik juz umie traktowac swiat jako snapshot. Dla MMO lepiej jednak zrobic exporter strukturalny obok snapshotu, nie tylko trzymac blob.

## World Save/Load

Plik: `game/world/world.cpp`

Wazne miejsca:

- `World::load(Serialize& fin)`
- `World::save(Serialize& fout)`

`World::save` zapisuje:

- dane sektorow BSP / guild sector state
- `wobj.save(fout)`, czyli obiekty swiata

## World Objects

Plik: `game/world/worldobjects.cpp`

Wazne miejsca:

- `WorldObjects::load(Serialize& fin)`
- `WorldObjects::save(Serialize& fout)`

`WorldObjects::save` zapisuje:

- wersje save dla swiata
- `npcArr`
- `npcInvalid`
- `itemArr`
- root vobs / mobsi przez `saveVobTree`
- trigger events
- routines

To jest glowne miejsce do rozpoczecia eksportera bytow swiata.

## NPC

Plik: `game/world/objects/npc.cpp`

Wazne miejsca:

- `Npc::save(Serialize& fout, size_t id, std::string_view directory)`
- `Npc::load(Serialize& fin, size_t id, std::string_view directory)`
- `Npc::saveAiState`
- `Npc::saveTrState`

`Npc::save` zawiera duzo danych potrzebnych dla MMO:

- `hnpc` / instancja skryptowa
- body/head/visual
- pozycja, angle, scale
- walking mode, guild, talents
- attitude
- perception functions
- spell/combat state
- transform state
- AI state
- current interact/current victim/current target
- waypoint/path
- movement/fight algos
- torch state
- physics position
- visual
- inventory

Na potrzeby initial baseline nie trzeba eksportowac wszystkiego naraz. Najpierw wystarczy stabilna identyfikacja, pozycja, symbol, nazwa, guild, hp, inventory, routine/AI marker.

## Items

Plik: `game/world/objects/item.cpp`

Wazne miejsca:

- `Item::Item(World& owner, Serialize& fin, Type type)`
- `Item::save(Serialize& fout) const`

`Item::save` zapisuje:

- symbol index
- pola `IItem`
- ilosc
- pozycja
- equipped / slot
- local transform

To powinno mapowac sie dobrze na `entity_items` i `inventory_items`.

## Existing Debug Export Attempts

W `game/world/world.cpp`, `game/world/worldobjects.cpp` i `game/world/objects/npc.cpp` sa lokalne helpery typu `append_unique` / `append_obj`, ktore dopisuja dane do plikow tekstowych.

Traktuj je jako slady eksploracji:

- potwierdzaja, ze szukane dane sa w runtime,
- nie sa dobrym formatem docelowym,
- powinny zostac zastapione przez jawny exporter z JSONL/SQLite/Postgres staging.
