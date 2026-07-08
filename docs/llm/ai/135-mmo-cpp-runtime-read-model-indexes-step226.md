# Step226 - C++ runtime read model indexes

## Powód

Step225 potrafił otworzyć runtime read-model i zweryfikować spójność liczników.
Następny krok przed `world_instance` cache to materializacja read-only indeksów,
żeby przyszły tick serwera nie skanował dużych tablic JSON-a.

To nadal nie jest wykonanie skryptów, nie jest VM Daedalusa i nie jest live NPC
tick. To jest serwerowy cache contentu.

## Zrobione

Rozszerzono:

- `server/cpp/mmo_runtime_read_model_loader.h`
- `server/cpp/mmo_runtime_read_model_loader.cpp`
- `server/cpp/mmo_runtime_read_model_probe.cpp`

Nowy loader buduje rekordy i indeksy:

- `world_zen_entity_by_key`
- `waypoint_edge_by_route`
- `npc_template_by_instance`
- `item_template_by_instance`
- `routine_by_npc_instance`
- `routine_by_symbol`
- `perception_binding_by_kind`
- `perception_binding_by_owner`
- `dialog_info_by_symbol`
- `dialog_output_by_name`

Probe nadal zwraca `status=ready`, jeśli walidacja schematu, hash format,
sekcje i liczniki przechodzą. Indeksowe braki wynikające z kandydackiego
read-modelu są ostrzeżeniami, nie błędami.

## Zweryfikowany wynik

Dla obecnego `runtime/content_build/parser_snapshot.json`:

```json
{
  "status": "ready",
  "index_counts": {
    "world_zen_entity_by_key": 24917,
    "waypoint_edge_by_route": 3202,
    "npc_template_by_instance": 730,
    "item_template_by_instance": 835,
    "routine_by_npc_instance": 0,
    "routine_by_symbol": 1186,
    "perception_binding_by_kind": 6,
    "perception_binding_by_owner": 0,
    "dialog_info_by_symbol": 4139,
    "dialog_output_by_name": 20826
  },
  "warnings": [
    "routines not indexed by npc_instance: 1186",
    "perception bindings not indexed by owner_symbol: 38"
  ],
  "validation_errors": []
}
```

Te dwa ostrzeżenia są zgodne z obecnym etapem: Step217/224 daje jeszcze
kandydacki model DAT, w którym rutyny i perception bindings nie są stabilnie
przypięte do właścicieli. Indeksy po symbolach i typach już działają.

## Komendy

```bash
cmake --build build/mmo_cpp_server --target mmo_runtime_read_model_probe -j
```

```bash
tools/export_content_build_runtime_read_model.py \
  --snapshot runtime/content_build/parser_snapshot.json \
  --output /tmp/opengothic_step226_runtime_read_model.json \
  --sql-output /tmp/opengothic_step226_register_runtime_export.sql
```

```bash
build/mmo_cpp_server/mmo_runtime_read_model_probe \
  /tmp/opengothic_step226_runtime_read_model.json
```

## Następny krok

Następny etap to `world_instance` content cache:

- ładuje jeden runtime read-model;
- wiąże go z aktywną rewizją contentu i światem;
- wystawia typed read-only lookup API;
- pozostaje immutable po załadowaniu;
- nie wykonuje jeszcze skryptów ani NPC ticka.
