# MMO server NPC action worker batch controls - step 187

Ten krok rozbudowuje inline worker `npc_action_request` tak, zeby byl praktyczniejszy do
testowania MMO i bezpieczniejszy operacyjnie.

## Co zostalo dodane

- Nowe opcje serwera:
  - `--npc-action-worker`,
  - `--no-npc-action-worker`,
  - `--npc-action-worker-max-per-packet <N>`.
- Domyslnie worker jest wlaczony, a limit wynosi `4` akcje NPC na obsluzony pakiet gracza.
- Startup log pokazuje:
  - czy worker jest wlaczony,
  - jaki ma limit per packet.
- Main loop zbiera metryki:
  - `npc_action_worker_claimed`,
  - `npc_action_worker_applied`,
  - `npc_action_worker_failed`.
- Kazdy claimowany request loguje `[npc_action_worker_result]` z:
  - statusem,
  - retryable,
  - action UUID,
  - aktorem,
  - action key,
  - targetem,
  - powodem.
- `runNpcActionRequestWorkerOnce(...)` zwraca teraz strukture `NpcActionWorkerResult`, a nie samo `bool`.
- Jesli worker rzuci wyjatek po claimie, probuje oznaczyc akcje jako failed/retryable, zeby wpis
  nie utknal na zawsze w `claimed`.

## Zachowanie po tym kroku

Po kazdym nie-bootstrapowym pakiecie, przy `directDb=on`, serwer moze wykonac:

1. claim pending `npc_action_request`,
2. parse payload,
3. apply runtime NPC activity,
4. mark applied/failed,
5. powtorzyc do limitu `npcActionWorkerMaxPerPacket`.

Limit `0` albo `--no-npc-action-worker` wylacza worker bez ruszania outboxa.

## Dlaczego to jest wazne

Wczesniejszy worker robil maksymalnie jedna akcje na pakiet i nie wystawial metryk. To bylo dobre
jako pierwszy proof-of-path, ale przy MMO szybko pojawia sie kolejka wielu NPC, ktorzy zareagowali
na ten sam event. Batch limit pozwala drenowac male serie reakcji bez osobnego procesu workerowego.

Metryki sa rownie wazne jak sama funkcja, bo przy przenoszeniu logiki z klienta na serwer trzeba
widziec, czy reakcje NPC:

- sa tylko enqueue'owane,
- sa claimowane,
- faktycznie przechodza do runtime state,
- wpadaja w retryable conflicts.

## Granice tego kroku

- To nadal inline worker, nie osobny proces.
- Worker nadal obsluguje tylko `npc_action_request`.
- Nie ma jeszcze live delta specjalnie dla applied worker activity.
- Nie ma jeszcze pathfindingu, dialogu ani Daedalusa po stronie workera.
- Obecny MySQL outbox jest pomostem; docelowo przy przepisaniu bazy powinien zostac zastapiony
  jawniejszym command streamem i runtime service dla swiata/NPC.

## Nastepny krok

Najwiekszy kolejny skok to rozdzielic `npc_action_request` na konkretne serwerowe wykonania:

- `warn` -> NPC alert/turn_to/short dialogue hook,
- `queue_script` -> server script handler queue,
- `call_help` -> broadcast do pobliskich NPC,
- `start_combat` -> FightIntent/CombatTimeline authority,
- `interrupt` -> przerwanie rutyny i live delta dla klientow.
