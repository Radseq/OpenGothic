# Step224 - content-build runtime read model export

## Powód

Po imporcie ZEN/DAT/OU z Gothic II raport `check_mmo_step211_content_build_database.py`
pokazał poprawne dane content-build:

- `zen_entities=24917`
- `waypoint_edges=3202`
- `daedalus_symbols=74651`
- `npc_templates=730`
- `item_templates=835`
- `routines=1186`
- `perception_bindings=38`
- `dialog_infos=4139`
- `dialog_outputs=20826`

Jedyny brakujący licznik w raporcie to `runtime_exports=0`. To jest właściwy
następny etap przed serwerowym tickiem NPC: content build jest już w bazie, ale
runtime/server cache nie ma jeszcze zatwierdzonego read-modelu, którego jedna
instancja świata mogłaby używać dla wszystkich graczy.

## Zrobione

Dodano narzędzie:

- `tools/export_content_build_runtime_read_model.py`
- `tools/bootstrap/export_content_build_runtime_read_model.py`

Narzędzie eksportuje sekcje:

- `world_zen_entities`
- `waypoint_edges`
- `npc_templates`
- `item_templates`
- `routines`
- `perception_bindings`
- `dialog_infos`
- `dialog_outputs`

Wynikiem jest JSON `mmo.content_build_runtime_read_model.v1` oraz SQL marker
`server_cache_read_model_json_v1` do tabeli
`mmo_content_build.content_build_runtime_exports`.

Marker nie jest aplikowany automatycznie. Domyślnie powstaje tylko plik SQL,
żeby etap publikacji był jawny i odwracalny.

## Tryb snapshot

Ten tryb działa bez połączenia z MySQL i używa parser snapshotu wygenerowanego
przez importer:

```bash
tools/export_content_build_runtime_read_model.py \
  --snapshot runtime/content_build/parser_snapshot.json \
  --output runtime/content_build/runtime_read_model.json \
  --sql-output runtime/content_build/register_runtime_read_model_export.sql
```

Zweryfikowany wynik dla ostatniego importu:

```json
{
  "status": "generated",
  "content_revision_key": "gothic2-notr-steam-local",
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
  }
}
```

## Tryb DB

Ten tryb czyta zatwierdzone dane z widoków `mmo_content_build`:

```bash
tools/export_content_build_runtime_read_model.py \
  --url "$MYSQL_URL" \
  --content-db-name mmo_content_build \
  --content-revision-key gothic2-notr-steam-local \
  --output runtime/content_build/runtime_read_model.json \
  --sql-output runtime/content_build/register_runtime_read_model_export.sql
```

Jeżeli marker ma zostać od razu zapisany w DB:

```bash
tools/export_content_build_runtime_read_model.py \
  --url "$MYSQL_URL" \
  --content-db-name mmo_content_build \
  --content-revision-key gothic2-notr-steam-local \
  --apply-marker
```

## Znaczenie dla serwera

To jest krok publikacji wspólnego contentu, nie wykonanie skryptów. Po nim
następny sensowny etap to materializacja `world_instance` po stronie serwera:
ładowanie read-modelu, utworzenie indeksów NPC/rutyn/percepcji/dialogów i
dopiero potem jeden server NPC/script tick oceniający reguły dla wielu graczy w
tej samej instancji świata.

Docelowo klient nie odpala osobnej prawdy skryptowej dla swojej sesji. Klient
może nadal robić kompatybilnościową prezentację, ale autorytatywny wynik
dialogu, rutyny, percepcji i reakcji NPC powinien pochodzić z serwera.

## Weryfikacja

```bash
python3 -m py_compile \
  tools/bootstrap/export_content_build_runtime_read_model.py \
  tools/export_content_build_runtime_read_model.py
```

```bash
tools/export_content_build_runtime_read_model.py \
  --snapshot runtime/content_build/parser_snapshot.json \
  --output /tmp/opengothic_step224_runtime_read_model.json \
  --sql-output /tmp/opengothic_step224_register_runtime_export.sql
```

```bash
python3 -m json.tool /tmp/opengothic_step224_runtime_read_model.json
```

```bash
rg -n "content_build_runtime_exports|server_cache_read_model_json_v1|payload_sha256" \
  /tmp/opengothic_step224_register_runtime_export.sql
```
