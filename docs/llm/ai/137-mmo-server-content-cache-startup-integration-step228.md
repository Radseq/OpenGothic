# Step228 - C++ server content cache startup integration

## Powod

Step227 dodal read-only `WorldInstanceContentCache`, ale byl on dostepny tylko
przez osobny probe. Serwer UDP nie ladowal jeszcze cache podczas startu ani nie
przekazywal informacji o cache do sesyjnego bootstrapu.

Ten krok integruje cache z procesem startu serwera. To nadal nie jest NPC tick,
Daedalus VM ani DB mutation.

## Zrobione

Rozszerzono:

- `server/cpp/mmo_server_types.h`
- `server/cpp/mmo_udp_server.cpp`
- `server/cpp/CMakeLists.txt`

Nowe opcje `mmo_udp_server`:

```text
--runtime-read-model PATH
--runtime-read-model-path PATH
--content-revision-key KEY
--world-instance-key KEY
--world-name NAME
--require-runtime-read-model-content-revision-match
--no-require-runtime-read-model-content-revision-match
--startup-check-only
```

`mmo_udp_server`:

- linkuje `mmo_world_instance_content_cache`;
- laduje cache podczas startu, gdy podano `--runtime-read-model*`;
- twardo odrzuca mismatch `content_revision_key`, chyba ze jawnie wylaczono
  ten check;
- loguje `world_instance_content_cache_ready` z licznikami cache;
- dodaje `world_instance_content_cache` metadata do bootstrap/live snapshot JSON,
  gdy snapshot jest budowany;
- wspiera `--startup-check-only`, zeby walidowac start/cache bez odpalania
  nieskonczonej petli UDP.

## Zweryfikowane komendy

Build:

```bash
cmake --build build/mmo_cpp_server --target mmo_udp_server -j
cmake --build build/mmo_cpp_server --target mmo_runtime_read_model_probe -j
cmake --build build/mmo_cpp_server --target mmo_world_instance_content_cache_probe -j
```

Startup check:

```bash
build/mmo_cpp_server/mmo_udp_server \
  --no-direct-db \
  --runtime-read-model-path /tmp/opengothic_step227_runtime_read_model.json \
  --content-revision-key gothic2-notr-steam-local \
  --world-instance-key newworld \
  --world-name newworld \
  --startup-check-only
```

Wynik:

```text
world_instance_content_cache=ready
world_zen_entities_in_world=24917
waypoint_edges_in_world=3202
npc_templates=730
item_templates=835
routines=1186
perception_bindings=38
dialog_infos=4139
dialog_outputs=20826
```

Negatywny check z `--content-revision-key wrong-revision` konczy sie bledem
mismatchu rewizji contentu.

## Nastepny krok

Rozpoczac server NPC/perception policy jako read-only assessment pass:

- pobrac aktywnych graczy z `world_instance`;
- pobrac aktywne NPC z runtime DB/projekcji;
- uzyc `WorldInstanceContentCache` do lookupow perception/routine/dialog;
- zapisac decyzje do `mmo_ai_runtime`;
- nie broadcastowac jeszcze dialogu/ruchu, dopoki dispatch/fan-out contract nie
  zostanie domkniety.
