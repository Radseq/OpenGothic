# Step213 - AI runtime action dispatch contracts

Step212 dodał osobną bazę `mmo_ai_runtime` i kolejkę
`npc_perception_action_queue`. Step213 robi z tej kolejki kontrakt, który może
obsłużyć przyszły serwerowy tick/worker.

To jest celowo osobne od `mmo_server_action_outbox`. W obecnej roadmapie outbox
jest ścieżką debug/fallback, a NPC perception powinno iść przez własny runtime
AI, bo ma inne priorytety, cooldowny, retry i fan-out.

## Nowe elementy

| Obiekt | Cel |
|---|---|
| `npc_perception_action_dispatch_log` | audyt claim/apply/fail/skip dla akcji NPC |
| `v_npc_perception_pending_actions` | akcje gotowe do pobrania przez worker |
| `v_npc_perception_action_dispatch_log` | czytelny log dispatchu |
| `v_npc_perception_action_dispatch_health` | health kolejki dispatchu |
| `mmo_ai_claim_next_npc_perception_action(...)` | atomowe pobranie kolejnej akcji |
| `mmo_ai_mark_npc_perception_action_applied(...)` | oznaczenie akcji jako wykonanej |
| `mmo_ai_mark_npc_perception_action_failed(...)` | błąd z opcjonalnym retry |
| `mmo_ai_skip_npc_perception_action(...)` | pominięcie akcji, np. target zniknął |
| `tools/simulate_ai_npc_perception_dispatch.py` | dev-test całego cyklu record -> claim -> finish |

## Co to daje dla NPC zaczepiającego gracza

Minimalny przepływ po Step213:

1. Przyszły tick AI wykrywa `PERC_ASSESSPLAYER`.
2. Tick zapisuje decyzję przez `mmo_ai_record_npc_perception_decision(...)`.
3. Jeżeli decyzja wymaga prezentacji, powstaje pending action, np.
   `npc_greet_player`.
4. Worker serwera woła `mmo_ai_claim_next_npc_perception_action(...)`.
5. Worker wysyła typed packet/broadcast do klienta albo obserwatorów.
6. Worker oznacza akcję jako `applied`, `failed` albo `skipped`.

To nadal nie jest pełna symulacja AI. To jest kontrakt kolejki, który umożliwia
bezpieczne dołożenie C++ ticka bez zgadywania, gdzie mają trafiać efekty NPC.

## Minimalny flow

1. Zastosuj SQL:

```bash
tools/apply_ai_runtime_database.py \
  --url "$MYSQL_URL" \
  --output runtime/step213_ai_runtime_action_dispatch/apply.json
```

`apply_ai_runtime_database.py` aplikuje teraz Step212 i Step213 w kolejności.

2. Sprawdź Step213:

```bash
tools/check_mmo_step213_ai_runtime_action_dispatch.py \
  --url "$MYSQL_URL" \
  --output runtime/step213_ai_runtime_action_dispatch/check.json
```

3. Raport:

```bash
tools/mmo_ai_runtime_report.py \
  --url "$MYSQL_URL" \
  --output runtime/step213_ai_runtime_action_dispatch/report.json
```

## Dev-test bez C++ ticka

Step213 zawiera mały dev-tool symulujący decyzję NPC:

- wstawia `greet_player` dla podanego `world_instance_uuid`, `npc_entity_key`
  i `target_key`;
- claimuje akcję workerem testowym;
- oznacza ją jako `applied`;
- pokazuje health i dispatch log.

Przykład:

```bash
tools/simulate_ai_npc_perception_dispatch.py \
  --url "$MYSQL_URL" \
  --finish applied \
  --output runtime/step213_ai_runtime_action_dispatch/simulate.json
```

Dopiero po tym warto podpinać realny C++ tick, bo będziemy mieli sprawdzony
kontrakt DB bez mieszania go z parserami ZEN/DAT/OU.




