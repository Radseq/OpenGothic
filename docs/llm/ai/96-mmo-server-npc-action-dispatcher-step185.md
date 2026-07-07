# MMO server NPC action dispatcher - step 185

Ten krok wydziela wspolny runtime dispatcher dla akcji NPC. Celem jest to, zeby reakcje
percepcji, obserwowane `RecordNpcActionState` i przyszly worker `npc_action_request` uzywaly
tej samej logiki aktualizacji `NpcActivityRegistry`.

## Co zostalo dodane

- Nowy helper `applyNpcActivityCommand(...)` w `mmo_udp_server.cpp`.
- Helper:
  - przyjmuje znormalizowane pola akcji NPC,
  - wywoluje `gNpcActivityRegistry.applyActivity`,
  - anuluje przerwana rozmowe przez `cancelInterruptedConversation`,
  - opcjonalnie loguje odrzucenie aktywnosci.
- Nowy helper `buildNpcActionCommandFromPayload(...)` parsuje JSON payload akcji NPC do
  `NpcAction::ActionCommand`.
- Perception reaction fast-path po enqueue `npc_action_request` uzywa teraz tego helpera.
- `RecordNpcActionState` takze uzywa tego helpera zamiast wlasnej kopii `applyActivity`.
- Payload `mmo.npc_action_request.v1` ma teraz jawne `action_target_key`, zeby worker nie
  musial zgadywac, czy `target_key` jest aktorem outboxa, czy celem akcji.

## Dlaczego to jest wazne

`npc_action_request` ma byc w przyszlosci claimowany z DB outboxa. Worker bedzie musial
zrobic dokladnie to samo co obecny fast-path:

1. sparsowac payload,
2. ustalic aktora, akcje, target i sync group,
3. zaktualizowac `NpcActivityRegistry`,
4. anulowac rozmowe, jesli akcja ja preemptuje,
5. zwrocic wynik do DB worker result.

Po tym kroku centralny punkt wykonania i parser payloadu juz istnieja. Brakuje jeszcze samego
claim-loopa i zapisu `mmo_record_server_action_worker_result`.

## Granice tego kroku

- Nie ma jeszcze wywolania `mmo_claim_next_server_action`.
- Nie ma jeszcze `mmo_mark_server_action_applied/failed`.
- Helper dziala na runtime state procesu serwera, nie jest jeszcze pelnym systemem AI.
- Przy przyszlym przepisaniu bazy ten helper powinien zostac po stronie runtime world/NPC
  service, a DB outbox powinien byc tylko trwalym command streamem.

## Nastepny krok

Najblizszy praktyczny krok to dodac worker claimujacy pending `npc_action_request` z DB:

1. `mmo_claim_next_server_action`,
2. `buildNpcActionCommandFromPayload(...)`,
3. `applyNpcActivityCommand(...)`,
4. `mmo_record_server_action_worker_result`,
5. `mmo_mark_server_action_applied/failed`.
