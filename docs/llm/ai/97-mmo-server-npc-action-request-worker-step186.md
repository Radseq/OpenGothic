# MMO server NPC action request worker - step 186

Ten krok dodaje pierwszy maly worker dla `npc_action_request`. Worker dziala inline w
`mmo_udp_server`: po obsludze pakietu gracza serwer probuje claimnac maksymalnie jedna pending
akcje NPC z outboxa.

## Co zostalo dodane

- `claimNextNpcActionRequest(...)`:
  - claimuje tylko `action_kind='npc_action_request'`,
  - nie przejmuje innych typow outboxa,
  - ustawia status `claimed`,
  - zwraca `action_uuid`, `target_key`, `idempotency_key` i payload.
- `ensureNpcActionWorkerRun(...)`:
  - tworzy albo odswieza worker run `mmo_udp_server:npc_action_request`.
- `runNpcActionRequestWorkerOnce(...)`:
  - parsuje payload przez `buildNpcActionCommandFromPayload(...)`,
  - aplikuje runtime state przez `applyNpcActivityCommand(...)`,
  - zapisuje `mmo_record_server_action_worker_result`,
  - oznacza akcje przez `mmo_mark_server_action_applied` albo `mmo_mark_server_action_failed`.
- `mmo_udp_server_main_loop.inl` wywoluje worker fail-open:
  - blad workera loguje `[npc_action_worker_failed_open]`,
  - pakiet gracza nie jest odrzucany przez blad workera.

## Dlaczego to jest wazne

Poprzednie kroki tworzyly outbox `npc_action_request`, ale wpis mogl zostac pending bez
server-side wykonania. Teraz serwer ma pierwszy domkniety przeplyw:

1. NPC widzi event percepcji.
2. Planner tworzy reakcje.
3. Serwer zapisuje journal reakcji.
4. Serwer enqueue'uje `npc_action_request`.
5. Worker claimuje request z DB.
6. Worker aktualizuje `NpcActivityRegistry`.
7. Worker zapisuje wynik i markuje akcje jako applied/failed.

To nadal jest maly inline worker, ale juz zaczyna laczyc trwaly command stream z runtime state
NPC.

## Granice tego kroku

- Worker obsluguje tylko jeden pending `npc_action_request` na obrot petli pakietu.
- Nie wykonuje jeszcze pathfindingu, animacji, dialogu ani walki.
- Nie robi jeszcze osobnego procesu workerowego.
- Konflikt aktywnosci jest traktowany jako retryable failed action.
- Obecny MySQL outbox pozostaje mostkiem; przy przyszlym przepisaniu bazy powinien zostac
  zastapiony jawniejszym command/event streamem runtime world service.

## Nastepny krok

Kolejny sensowny wycinek to zrobic worker bardziej deterministic:

- limit claimow per tick,
- metryki `npc_action_worker_applied/failed`,
- osobny tryb CLI dla worker-only,
- snapshot/live delta po applied activity,
- rozdzielenie `warn/queue_script/call_help/start_combat` na konkretne serwerowe akcje AI.
