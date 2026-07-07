# MMO server perception reaction DB journal - step 182

Ten krok domyka kolejny maly fragment drogi do serwera jako zrodla prawdy dla NPC:
planowana reakcja percepcji nie istnieje juz tylko jako log i live delta UDP, ale moze byc
utrwalona w serwerowym journalu przez istniejaca procedure `mmo_record_npc_reaction_started`.

## Co zostalo dodane

- `recordPerceptionEvent` przyjmuje teraz opcjonalny kontekst DB:
  - `MySqlTarget*`,
  - `sessionUuid`,
  - seed idempotencji z pakietu klienta.
- Po zaplanowaniu reakcji serwer wywoluje `recordPerceptionReactionStarted`.
- Zapis DB jest wykonywany tylko dla materialnych reakcji:
  - live delta UDP,
  - script handler,
  - interrupt,
  - hostility change.
- Reakcje oparte o swiadkow sa zapisywane per realny `witnessKey` z `WitnessSummary.results`.
- Eventy bez wymaganego swiadka, np. bezposrednie `assess_damage`, dostaja fallback aktora z victim/source.
- DB failure jest fail-open:
  - oryginalna akcja direct-apply nadal idzie dalej,
  - live delta nadal moze zostac wyslana,
  - serwer loguje `[perception_reaction_db_failed]`.

## Podpiete zrodla percepcji

| Plik | Zdarzenia |
|---|---|
| `server/cpp/mmo_udp_server_direct_combat_apply.inl` | combat intent, character damage, world entity damage, others damage |
| `server/cpp/mmo_udp_server_direct_interactive_apply.inl` | draw weapon, remove weapon |
| `server/cpp/mmo_udp_server_direct_inventory_apply.inl` | container take, world item pickup |
| `server/cpp/mmo_udp_server.cpp` | centralny zapis `mmo_record_npc_reaction_started` |

## Dlaczego to jest wazne dla MMO

W multi-user Gothic MMO reakcja NPC nie moze byc tylko lokalnym efektem klienta jednego gracza.
Ten sam NPC moze widziec kilku graczy, kilku graczy moze widziec tego NPC, a decyzja musi byc
jedna i autorytatywna. Journal DB daje serwerowi trwaly slad:

- ktory event percepcji zaszedl,
- ktory NPC byl swiadkiem/reagujacym aktorem,
- jaki byl typ reakcji,
- jaki byl target,
- w ktorym ticku serwera to nastapilo,
- z jakim payloadem debugowym i idempotencja.

To jeszcze nie jest pelny Daedalus ani pelna AI NPC. To jest pomost: serwer zaczyna zapisywac
konkretne decyzje reakcji, ktore pozniej beda napedzac NPC activity, dialog, walke i skrypty.

## Granice tego kroku

- Serwer nadal nie wybiera finalnej akcji skryptowej Daedalusa.
- Reakcja DB nie blokuje jeszcze akcji klienta, jesli journal sie nie powiedzie.
- `mmo_record_npc_reaction_started` jest nadal czescia obecnego mostka MySQL.
- Docelowo baza zostanie przepisana: obecne procedury sa etapem przejsciowym, a przyszly model
  powinien rozdzielic immutable event journal, read modele swiata, stan runtime NPC i kolejki
  server actions.

## Nastepny sensowny krok

Najbardziej naturalny kolejny wycinek to podlaczenie journala reakcji do server actions:

1. `mmo_record_npc_reaction_started` zapisuje event.
2. Serwer na podstawie `ReactionPlan` enqueue'uje `npc_action_request`.
3. NPC activity registry przyjmuje akcje typu `turn_to`, `approach`, `warn`, `start_combat`.
4. Klient dostaje tylko wynik: live delta + snapshot stanu NPC.

Wtedy reakcja NPC zaczyna przechodzic z "obserwowalnego faktu" do "autorytatywnej akcji swiata".
