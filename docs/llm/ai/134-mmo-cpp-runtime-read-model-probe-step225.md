# Step225 - C++ runtime read model probe

## Powód

Step224 eksportuje wspólny read-model contentu do JSON-a. Zanim serwer zacznie
materializować `world_instance` i tick NPC dla wielu graczy, C++ musi mieć
własny punkt wejścia, który potrafi taki artefakt otworzyć i odrzucić
niespójne dane.

To nie jest jeszcze wykonanie skryptów. To jest serwerowy gate: read-model
musi być poprawny, kompletny i spójny licznikowo, zanim trafi do cache ticka.

## Zrobione

Dodano:

- `server/cpp/mmo_runtime_read_model_loader.h`
- `server/cpp/mmo_runtime_read_model_loader.cpp`
- `server/cpp/mmo_runtime_read_model_probe.cpp`
- target CMake `mmo_runtime_read_model_loader`
- target CMake `mmo_runtime_read_model_probe`

Loader jest C++23 i nie dodaje nowej zewnętrznej biblioteki JSON. Używa małego
czytnika JSON wystarczającego do serwerowej walidacji kontraktu Step224:

- `schema == mmo.content_build_runtime_read_model.v1`
- `content_revision_key` istnieje
- `game_code` istnieje
- `payload_sha256` ma 64 znaki lowercase hex
- wszystkie znane sekcje istnieją
- wszystkie znane liczniki `summary` istnieją
- liczniki `summary` zgadzają się z faktyczną liczbą elementów w tablicach

Sekcje sprawdzane przez probe:

- `world_zen_entities`
- `waypoint_edges`
- `npc_templates`
- `item_templates`
- `routines`
- `perception_bindings`
- `dialog_infos`
- `dialog_outputs`

## Komendy

Build:

```bash
cmake --build build/mmo_cpp_server --target mmo_runtime_read_model_probe -j
```

Eksport read-modelu:

```bash
tools/export_content_build_runtime_read_model.py \
  --snapshot runtime/content_build/parser_snapshot.json \
  --output /tmp/opengothic_step225_runtime_read_model.json \
  --sql-output /tmp/opengothic_step225_register_runtime_export.sql
```

Probe:

```bash
build/mmo_cpp_server/mmo_runtime_read_model_probe \
  /tmp/opengothic_step225_runtime_read_model.json
```

Zweryfikowany wynik:

```json
{
  "status": "ready",
  "content_revision_key": "gothic2-notr-steam-local",
  "game_code": "gothic2-notr",
  "payload_sha256": "e374f5329a7a4194594f57a766ab3d14f7626c24d27b513bda08b64cc3c76f83",
  "summary": {
    "world_zen_entity_count": 24917,
    "waypoint_edge_count": 3202,
    "npc_template_count": 730,
    "item_template_count": 835,
    "routine_count": 1186,
    "perception_binding_count": 38,
    "dialog_info_count": 4139,
    "dialog_output_count": 20826
  },
  "section_counts": {
    "world_zen_entity_count": 24917,
    "waypoint_edge_count": 3202,
    "npc_template_count": 730,
    "item_template_count": 835,
    "routine_count": 1186,
    "perception_binding_count": 38,
    "dialog_info_count": 4139,
    "dialog_output_count": 20826
  },
  "warnings": [],
  "validation_errors": []
}
```

## Następny krok

Następny sensowny etap to nie VM Daedalusa jeszcze. Najpierw trzeba rozwinąć
ten loader z walidacji liczników do materializacji indeksów runtime:

- indeks NPC template po `npc_instance`;
- indeks rutyn po `npc_instance`;
- indeks perception bindings po `perception_kind` i `owner_symbol`;
- indeks dialog info/output po symbolach;
- indeks waypointów/world entities dla przyszłego spatial query.

Dopiero po tym jedna `world_instance` po stronie serwera może oceniać rutyny,
percepcję i dialogi względem wielu graczy, zamiast traktować skrypty jako
per-klientowy stan.
