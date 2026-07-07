# MMO server perception reaction action outbox - step 183

Ten krok rozwija poprzedni zapis `mmo_record_npc_reaction_started`: reakcja percepcji NPC
moze teraz utworzyc takze zadanie w `mmo_server_action_outbox`.

## Co zostalo dodane

- Materialna reakcja percepcji enqueue'uje `npc_action_request`.
- `target_key` outboxa jest kluczem reagujacego NPC przycietym do limitu schematu outboxa;
  pelny `actor_key` zostaje w payloadzie.
- Payload ma schemat `mmo.npc_action_request.v1` i zawiera:
  - `actor_key`,
  - `target_key`,
  - `action_key`,
  - `action_state`,
  - `sync_group`,
  - `server_tick`,
  - perception/reaction/reason,
  - source/other/victim/item keys,
  - witness counts,
  - flagi `script_handler`, `interrupt`, `hostility`.
- Idempotencja outboxa jest oddzielona od journala reakcji prefiksem `npc_action:`
  i ograniczona do limitu `mmo_server_action_outbox.idempotency_key`.

## Mapping reakcji na action request

| ReactionKind | action_key | Priorytet |
|---|---:|---:|
| `StartCombat` | `start_combat` | 20 |
| `CallHelp` | `call_help` | 20/30 zalezne od flag planu |
| `Warn` | `warn` | 30 |
| `Interrupt` | `interrupt` | 30 |
| `SuspectCrime` | `suspect_crime` | 50/80 zalezne od flag planu |
| `QueueScript` | `queue_script` | 50 |

Priorytet wynika z flag planu:

- hostility change: `20`,
- interrupt: `30`,
- script handler: `50`,
- reszta: `80`.

## Dlaczego to jest wazne

Do tej pory serwer mogl:

1. zauwazyc event percepcji,
2. wybrac `ReactionPlan`,
3. zapisac journal reakcji,
4. wyslac live delta do klienta.

Po tym kroku serwer dodatkowo tworzy prace dla przyszlego ticka/workerow NPC:

1. NPC widzi event,
2. planner wybiera reakcje,
3. journal zapisuje fakt reakcji,
4. outbox zapisuje intencje wykonania akcji NPC,
5. przyszly worker moze zamienic `npc_action_request` na NPC activity, walke, dialog albo skrypt.

To jest wazny most miedzy "NPC cos zauwazyl" a "NPC autorytatywnie cos robi".

## Granice tego kroku

- Outbox nie wykonuje jeszcze akcji NPC.
- Nie ma jeszcze claim/worker loop dla `npc_action_request`.
- DB bridge nadal jest przejsciowy; przy przyszlym przepisaniu bazy ten kontrakt powinien
  przejsc w jawna kolejke domenowa albo event-sourced command stream.
- Blad journala albo outboxa jest fail-open dla glownego gameplayu, bo ten krok nie powinien
  jeszcze blokowac combat/inventory/weapon direct-apply.

## Nastepny krok

Nastepny praktyczny wycinek to worker/dispatcher:

1. claimuje `npc_action_request`,
2. waliduje, czy NPC nadal istnieje i moze wykonac akcje,
3. zamienia request na `NpcActivityRegistry.applyActivity`,
4. zapisuje wynik worker run/result,
5. emituje live delta/snapshot NPC state.
