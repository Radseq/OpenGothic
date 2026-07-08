# Step227 - C++ world_instance content cache

## Powod

Step226 zmaterializowal indeksy runtime read-modelu, ale serwer nadal nie mial
warstwy, ktora wiaze je z konkretna rewizja contentu i `world_instance`.
Przed NPC/perception tickiem potrzebny jest immutable cache, ktory gameplay code
moze pytac bez ponownego parsowania JSON-a i bez skanowania duzych tablic.

To nadal nie jest wykonanie skryptow, Daedalus VM, live NPC movement ani DB
mutation.

## Zrobione

Dodano:

- `server/cpp/mmo_world_instance_content_cache.h`
- `server/cpp/mmo_world_instance_content_cache.cpp`
- `server/cpp/mmo_world_instance_content_cache_probe.cpp`
- target CMake `mmo_world_instance_content_cache`
- target CMake `mmo_world_instance_content_cache_probe`

Rozszerzono `mmo_runtime_read_model_loader.*` o publiczne helpery budowania
kluczy indeksow:

- `makeWorldZenEntityIndexKey`
- `makeWaypointEdgeRouteIndexKey`

Cache:

- laduje `RuntimeReadModel`;
- waliduje schemat/liczniki/hash przez istniejacy validator;
- wymaga `content_revision_key`, `world_instance_key` i `world_name`;
- odrzuca mismatch aktywnej rewizji contentu z read-modelem;
- przechowuje model przez wartosc, wiec lookupi zwracaja stabilne wskazniki lub
  zakresy do danych nalezacych do cache;
- eksponuje read-only lookup API dla world ZEN entities, waypoint edges,
  NPC/item templates, routines, perception bindings, dialog infos i dialog
  outputs.

## Zweryfikowany wynik

Build:

```bash
cmake --build build/mmo_cpp_server --target mmo_world_instance_content_cache_probe -j
cmake --build build/mmo_cpp_server --target mmo_runtime_read_model_probe -j
```

Probe cache:

```bash
build/mmo_cpp_server/mmo_world_instance_content_cache_probe \
  /tmp/opengothic_step226_runtime_read_model.json \
  gothic2-notr-steam-local \
  newworld \
  newworld
```

Wynik:

```json
{
  "status": "ready",
  "content_revision_key": "gothic2-notr-steam-local",
  "world_instance_key": "newworld",
  "world_name": "newworld",
  "cache_stats": {
    "world_zen_entities_in_world": 24917,
    "waypoint_edges_in_world": 3202,
    "npc_templates": 730,
    "item_templates": 835,
    "routines": 1186,
    "perception_bindings": 38,
    "dialog_infos": 4139,
    "dialog_outputs": 20826
  }
}
```

Oczekiwane pozostaja dwa false/warnings z Step226:

- `first_routine_by_npc_instance=false`, bo rutyny nie maja jeszcze
  `npc_instance`;
- `first_perception_binding_by_owner=false`, bo perception bindings nie maja
  jeszcze `owner_symbol`.

Negatywna walidacja dziala:

```bash
build/mmo_cpp_server/mmo_world_instance_content_cache_probe \
  /tmp/opengothic_step226_runtime_read_model.json \
  wrong-revision \
  newworld \
  newworld
```

Zwraca blad mismatchu rewizji contentu.

## Nastepny krok

Zintegrowac cache z C++ server startup/session bootstrap:

- dodac jawne opcje sciezki read-modelu i aktywnej rewizji;
- zaladowac cache raz po stronie serwera;
- bindowac cache do aktywnego `world_instance`;
- wystawic go przyszlemu NPC/perception policy;
- nadal nie wykonywac script ticka ani live NPC movement.
