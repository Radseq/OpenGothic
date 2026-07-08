# Step230 - C++ AI runtime perception recording probe

## Powod

Step229 potrafil wyliczyc kandydacka decyzje NPC perception, ale nie zapisywal
jej jeszcze do `mmo_ai_runtime`. Step230 dodaje pierwszy C++ most:

```text
runtime DB actors
-> WorldInstanceContentCache
-> mmo_npc_perception_policy
-> mmo_ai_record_npc_perception_decision(...)
```

To nadal nie jest automatyczny tick serwera UDP, Daedalus VM, ruch NPC ani
broadcast. Zapis jest jawny przez probe albo przyszly scheduler world_instance.

## Zrobione

Dodano:

- `server/cpp/mmo_npc_perception_runtime_source.h`
- `server/cpp/mmo_npc_perception_runtime_source.cpp`
- `server/cpp/mmo_ai_runtime_persistence.h`
- `server/cpp/mmo_ai_runtime_persistence.cpp`
- `server/cpp/mmo_npc_perception_record_probe.cpp`
- `server/cpp/mmo_server_world_clock.h`
- target CMake `mmo_server_persistence`
- target CMake `mmo_npc_perception_runtime_source`
- target CMake `mmo_ai_runtime_persistence`
- target CMake `mmo_npc_perception_record_probe`

Zmodyfikowano:

- `mmo_npc_perception_policy` przenosi `session_uuid` i `character_uuid` z
  `PlayerActor` do `DecisionCandidate` i payloadu.
- `mmo_server_types::Options` dostal brakujace pola manifestu contentu, z
  defaultami bez zmiany zachowania.

## Runtime actor source

`mmo_npc_perception_runtime_source`:

- rozpoznaje `realm_world_instances` po `world_instance_key` albo `world_name`;
- czyta aktywnych graczy z `server_sessions` + `characters` +
  `character_positions`;
- czyta aktywne NPC/creature z `world_entity_state` +
  `content_entity_templates`;
- zwraca `world_instance_uuid`, `server_tick`, `NpcActor[]`, `PlayerActor[]`.

Ten actor-source nie zapisuje DB.

## AI runtime persistence

`mmo_ai_runtime_persistence`:

- kwalifikuje bezpieczna nazwe bazy AI, domyslnie `mmo_ai_runtime`;
- waliduje wymagane pola decyzji;
- wywoluje `mmo_ai_record_npc_perception_decision(...)`;
- zwraca `decision_uuid`, `decision_status`, `action_queue_uuid`;
- uzywa idempotency key wygenerowanego przez Step229 policy.

## Probe

Kontrakt:

```bash
mmo_npc_perception_record_probe \
  <mysql_url> \
  <runtime_read_model.json> \
  <content_revision_key> \
  <world_instance_key> \
  <world_name>
```

Wazne opcje:

- `--dry-run` - nie zapisuje do DB;
- `--synthetic` - uzywa kontrolowanej jednej pary NPC/player;
- `--world-instance-uuid UUID` - wymagane dla syntetycznego zapisu bez lookupu;
- `--max-npcs`, `--max-players`, `--max-records`;
- `--max-distance`;
- `--ai-db-name`.

## Zweryfikowane

Build:

```bash
cmake --build build/mmo_cpp_server --target mmo_npc_perception_record_probe -j
cmake --build build/mmo_cpp_server --target mmo_udp_server -j
```

Dry-run syntetyczny:

```bash
build/mmo_cpp_server/mmo_npc_perception_record_probe \
  mysql://user:pass@127.0.0.1:3306/gothic_mmo_ch1_clean \
  /tmp/opengothic_step227_runtime_read_model.json \
  gothic2-notr-steam-local \
  newworld \
  newworld \
  --synthetic \
  --dry-run \
  --world-instance-uuid 00000000-0000-0000-0000-000000000000
```

Wynik: `status=ready_dry_run`, `decisions=1`, `decision_kind=greet_player`.

Dry-run live actors przez lokalny MySQL:

- `world_instance_uuid=bbd96c28-77b0-11f1-8d6a-30560f155bba`
- `npc_actors=1`
- `player_actors=3`
- domyslny dystans odrzucil wszystkie pary;
- `--max-distance 1000000` dal `decisions=3`.

Uwaga: aktualny live NPC z DB ma slaba tozsamosc
`entity_key=creature:None` i puste `npc_instance`, wiec nie uzyto go jako
trwalego dowodu zapisu produkcyjnego.

Syntetyczny idempotentny zapis do `mmo_ai_runtime`:

```text
recorded_count=1
decision_status=queued
action_queue_uuid=623689fc-7a3f-11f1-896d-30560f155bba
```

Step213 health check po zapisie:

```text
status=passed
pending_count=1
```

Regresja:

```bash
build/mmo_cpp_server/mmo_udp_server \
  --no-direct-db \
  --runtime-read-model-path /tmp/opengothic_step227_runtime_read_model.json \
  --content-revision-key gothic2-notr-steam-local \
  --world-instance-key newworld \
  --world-name newworld \
  --startup-check-only
```

Wynik: `startup_check=ok`, `world_instance_content_cache=ready`.

## Nastepny krok

Step231 powinien uszczelnic runtime NPC identity:

- nie opierac live ticka na `creature:None` / pustym `npc_instance`;
- doprowadzic runtime NPC actor do stabilnego content-backed template key albo
  `npc_instance`;
- dopiero potem wlaczyc jawny scheduler `world_instance` tick, ktory wywola
  Step230 writer dla aktywnych aktorow;
- nadal bez broadcastu/dialogu, dopoki dispatcher/fan-out nie jest gotowy.
