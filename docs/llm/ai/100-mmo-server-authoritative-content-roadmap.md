# Roadmapa: serwer jako 100% zrodlo prawdy dla contentu Gothica

Cel: MMO server ma czytac i posiadac wlasny content pack Gothica/moda, a klient ma tylko
odtwarzac wynik decyzji serwera. Klient nie moze byc zrodlem prawdy dla NPC AI, dialogow,
percepcji, rutyn, dropow, questow ani skryptow.

## Zasada podstawowa

Serwer nie czyta plikow z komputera gracza.

Serwer ma wlasna kopie contentu:

- ZEN-y,
- DAT/Daedalus,
- OU/cutscene output,
- VDF/MOD albo rozpakowana paczka moda,
- opcjonalne pliki konfiguracyjne moda.

Klient przy logowaniu moze wyslac tylko identyfikator/hash swojej wersji contentu. Serwer
porownuje to z wlasnym manifestem. Jesli wersja sie nie zgadza, sesja powinna byc odrzucona
albo wpuszczona tylko w trybie diagnostycznym.

## Etap 1: manifest content packa

Status po Step188: zaczete.

Powstaje:

- `mmo_server_content_pack_files`,
- `mmo_server_content_pack_manifests`,
- `mmo_validate_client_content_pack(...)`,
- narzedzie `tools/register_server_content_pack_manifest.py`.

To daje serwerowi odpowiedz:

- jaki content revision jest aktywny,
- jakie pliki sa w serwerowym packu,
- jaki jest hash calej paczki,
- czy klient deklaruje zgodna wersje.

## Etap 2: login gate contentu

Status po Step189: czesciowo zrobione.

Zrobione:

- opcja serwera `--require-client-content-manifest`,
- opcja serwera `--client-content-manifest-hash HASH`,
- session-scoped DB procedure `mmo_validate_client_content_pack_for_session(...)`,
- C++ waliduje manifest po loginie, recovery sesji i zmianie postaci w bootstrapie.

Do dodania:

- pole `client_content_manifest_hash` w realnym bootstrap/client hello payload,
- klientowe liczenie manifest hash z lokalnego contentu,
- diagnostyka dla klienta:
  - `content_ok`,
  - `content_hash_mismatch`,
  - `server_manifest_missing`,
  - `client_manifest_missing`.

Po Step189 serwer moze juz egzekwowac manifest, ale hash nadal jest podawany przez CLI. Pelna
wersja wymaga przeslania go przez klienta.

## Etap 3: ZEN jako server world content

Serwer powinien wyciagac z ZEN:

- swiat i nazwe swiata,
- VOB-y,
- NPC/creature/item spawn points,
- waypointy,
- freepointy,
- movers,
- triggers,
- mobsi/interactives,
- containers,
- drzwi/skrzynie i ich stany startowe,
- relacje trigger -> target.

Docelowe tabele albo read modele:

- `content_world_files`,
- `content_world_vobs`,
- `content_waypoints`,
- `content_waypoint_edges`,
- `content_freepoints`,
- `content_interactives`,
- `content_movers`,
- `content_triggers`,
- `content_container_templates`.

Aktualne `mmo_server_waypoint_read_model` jest dobrym pomostem, ale docelowo powinno byc
projekcja z content store, a nie recznie doklejany read model.

## Etap 4: DAT/Daedalus jako server script content

Serwer musi znac:

- symbole,
- instance NPC,
- instance itemow,
- guildy/fakcje,
- atrybuty i protectiony,
- AI vars,
- perception functions,
- dialog info,
- warunki dialogow,
- efekty dialogow,
- quest/story globals,
- rutyny TA/ZS,
- funkcje typu `B_AssessPlayer`, `B_AssessDamage`, `B_Attack`, `B_GivePlayerXP`.

Nie trzeba od razu wykonywac pelnego Daedalusa. Sensowna kolejnosc:

1. import symboli i instancji jako dane,
2. mapowanie perception function -> server reaction kind,
3. mapowanie dialog condition/result -> jawne server actions,
4. dopiero pozniej interpreter albo transpilacja wybranego podzbioru Daedalusa.

Docelowe tabele/warstwy:

- `content_script_symbols`,
- `content_npc_templates`,
- `content_item_templates` rozszerzone o raw Daedalus fields,
- `content_dialog_infos`,
- `content_dialog_outputs`,
- `content_perception_rules`,
- `content_routine_templates`,
- `content_guild_attitudes`.

## Etap 5: OU/dialog output

Serwer powinien znac:

- output id/name,
- speaker,
- text/subtitle,
- audio id/path,
- czas/priorytet, jesli dostepny.

Klient powinien dostawac od serwera:

- `start_dialog`,
- `dialog_line`,
- `dialog_options`,
- `dialog_end`,
- audio/subtitle id, a nie decyzje logiczna.

Klient moze lokalnie odtworzyc glos i UI, ale nie powinien decydowac, czy dialog jest dostepny
ani jakie skutki dialogu sa zapisane.

## Etap 6: server perception i NPC zaczepiajacy gracza

Minimalny server-side przeplyw:

1. Serwer ma pozycje gracza i NPC.
2. Serwer zna z contentu zmysly/range/faction/perception scripts NPC.
3. Tick perception szuka kandydatow w poblizu.
4. Planner wybiera reakcje:
   - `ignore`,
   - `turn_to_player`,
   - `approach_player`,
   - `warn`,
   - `start_dialog`,
   - `call_help`,
   - `start_combat`.
5. Serwer zapisuje reaction journal.
6. Serwer enqueue'uje/wykonuje `npc_action_request`.
7. Klient dostaje live delta i prezentuje animacje/dialog/audio.

Aktualny worker `npc_action_request` jest pierwszym mostkiem do punktow 5-7.

## Etap 7: wiele graczy naraz

W single player NPC reaguje na jednego `PC_HERO`. W MMO musi byc inaczej:

- perception tick liczy kandydatow per NPC, nie per gracz,
- NPC ma aktualny focus target, aggro table i cooldowny,
- reakcje dialogowe maja lock albo lease, zeby NPC nie zaczepial 10 graczy jednoczesnie,
- walka ma threat/assist logic,
- rutyna NPC moze byc przerwana przez event wyzszego priorytetu,
- live delta idzie tylko do graczy w interest area,
- DB zapisuje trwaly stan, ale transient AI tick powinien byc w runtime world service/cache.

Wazne rozdzielenie:

- DB: stan trwaly, content, checkpointy, event log.
- Runtime world service: tick NPC, aggro, path, cooldowny, tymczasowe decyzje.
- Client: render, animacje, audio, UI, input.

## Etap 8: przepisanie bazy

Obecna baza jest bardzo uzytecznym mostkiem, ale docelowo powinna zostac przepisana.

Powod:

- za duzo funkcji/procedur jest teraz pomostem z kolejnych etapow,
- content/static i runtime/current sa miejscami wymieszane,
- outbox/journal/checkpoint/read-model powinny miec jasniejsze granice,
- transient AI nie powinno zyc w relacyjnych tabelach jako glowny mechanizm ticku,
- server-authoritative MMO potrzebuje jawnego command/event streamu i projekcji.

Docelowy podzial:

- `content_store`:
  - immutable game/mod revisions,
  - ZEN/DAT/OU parsed content,
  - templates, graphy, dialogi, rutyny.
- `world_runtime`:
  - aktywne instancje swiata,
  - aktualne NPC/world item states,
  - leases/locks,
  - tick snapshots.
- `event_stream`:
  - accepted commands,
  - durable gameplay events,
  - idempotency,
  - replay/debug.
- `player_state`:
  - postacie,
  - inventory,
  - questy,
  - known dialogs,
  - character-scoped script state.
- `read_models`:
  - bootstrap snapshot,
  - nearby interest windows,
  - admin/debug views.

## Najblizsza kolejnosc prac

1. Step188 manifest content packa - zrobione.
2. Step189 C++ content hash gate po DB session - zrobione w trybie CLI/warn/required.
3. Dodac realny `client_content_manifest_hash` do bootstrap/client hello.
4. Parser/import ZEN waypoint/freepoint/VOB/mover/trigger.
5. Server NPC interest/perception tick korzystajacy z content waypoints i live player positions.
6. Parser/import DAT symboli NPC/item/dialog/perception.
7. Pierwsze server dialog availability bez wykonywania pelnego Daedalusa.
8. Osobny runtime world service dla NPC AI zamiast inline workera.
9. Przepisanie bazy na czystszy content/runtime/event/read-model split.


