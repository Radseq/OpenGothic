# Expanded roadmap - authoritative server content

Cel końcowy: klient nie jest źródłem prawdy dla świata, NPC, dialogów ani
percepcji. Klient pokazuje wynik. Serwer ma własną kopię contentu gry/moda,
czyta ją, buduje swoje indeksy i tickuje logikę dla wielu graczy jednocześnie.

## Zasada bazowa

Serwer nigdy nie czyta plików z komputera gracza.

Serwer ma własny content pack:

- ZEN;
- DAT / compiled Daedalus;
- OU / output units;
- VDF/MOD albo pre-extracted content root;
- konfigurację content revision;
- hash manifestu;
- DB indeksów contentu.

Klient przy logowaniu wysyła tylko deklarację wersji, np. manifest hash.
Serwer sprawdza ją względem swojej aktywnej rewizji contentu.

## Aktualny stan po Step190-229

| Obszar | Status |
|---|---|
| Klient wysyła `client_content_manifest_hash` | zrobione |
| Serwer waliduje hash przez DB | zrobione |
| Bootstrap reject ma diagnostykę dla klienta | zrobione |
| Klient zapisuje ostatni reject do runtime JSON | zrobione |
| Menu blokuje ładowanie przy content reject | zrobione |
| Serwer zapisuje reject do JSONL | zrobione |
| Serwer zapisuje reject do DB audit | zrobione |
| DB health views dla rejectów | zrobione |
| Healthcheck rejectów | zrobione |
| Step188 manifest plików content packa | przywrócony do paczki |
| Step189 session content gate | przywrócony do paczki |
| Step201 inventory ról plików | zrobione |
| Step202 generator inventory JSON/SQL | zrobione |
| Step203 archive mount/pre-extract registry | zrobione |
| Step206 content import job queue | zrobione |
| Step208 content build indexes w runtime DB | zrobione jako etap przejściowy |
| Step211 osobna baza `mmo_content_build` | zrobione |
| Step212 importer snapshotu parsera do `mmo_content_build` | zrobione |
| Step213 check/report dla `mmo_content_build` | zrobione |
| Step212 AI runtime DB `mmo_ai_runtime` | zrobione w tej paczce jako osobny tor dla percepcji NPC |
| Step213 AI action dispatch contracts | zrobione jako kolejka claim/apply/fail/skip dla efektów NPC |
| Step214 C++ ZenKit content-build importer | zrobione jako pierwszy izolowany reader ZEN/DAT/OU do parser snapshot JSON |
| Step215 NPC observation protocol alignment | zrobione jako fail-open most dla combat intent, action state i dialog line bez przedwczesnego DB contract |
| Step216 server script authority boundary | doprecyzowane: skrypty stają się wspólnym serwerowym kontekstem po content read modelu i server NPC ticku, nie per-klientowym wykonaniem |
| Step217 DAT/OU candidate read model | zrobione jako symbol-table read model dla NPC/item/dialog/routine/perception bez uruchamiania Daedalusa |
| Step218 Gothic content source discovery | zrobione jako scanner katalogu Gothic II/pre-extracted root i generator skryptu dla `mmo_content_build_importer` |
| Step219 world ZEN discovery fix + OU fail-open | zrobione: scanner nie wybiera presetów jako world ZEN, a importer zapisuje błędy źródeł do `parser_errors` |
| Step220 content-build run script guard | zrobione: częściowy DAT/OU snapshot wymaga jawnej flagi, a DB apply nie odpala się domyślnie |
| Step221 content-build item templates compat | zrobione: dodaje brakującą tabelę `daedalus_item_templates`, widok i `item_template_count` dla starszych baz `mmo_content_build` |
| Step222 VDF world ZEN probe/extract | zrobione: izolowany ZenKit VFS probe wskazuje i opcjonalnie wyciąga world ZEN z VDF/MOD do loose root dla obecnego importera |
| Step223 VDF probe API hotfix | zrobione: probe nie używa już niepublicznego `getKnownFiles()`, tylko publiczne `vfs.find(...)` na znanych nazwach world ZEN |
| Step224 content-build runtime read model export | zrobione: snapshot albo `mmo_content_build` może zostać wyeksportowany do `server_cache_read_model_json_v1` z markerem `content_build_runtime_exports` |
| Step225 C++ runtime read model probe | zrobione: serwerowy C++23 loader/probe waliduje schema, hash, summary i faktyczne liczniki sekcji read-modelu |
| Step226 C++ runtime read model indexes | zrobione: loader materializuje read-only indeksy NPC/item/routine/perception/dialog/waypoint/world entity pod przyszły `world_instance` cache |
| Step227 C++ world_instance content cache | zrobione: read-only cache wiąże `RuntimeReadModel` z aktywną rewizją contentu, `world_instance_key` i `world_name` oraz wystawia typed lookup API |
| Step228 server content cache startup integration | zrobione: `mmo_udp_server` ładuje cache za explicit flags, waliduje rewizję contentu i dopisuje metadata cache do bootstrap snapshotu |
| Step229 NPC perception policy candidate pass | zrobione: C++ policy ocenia kandydackie pary NPC/gracz przez cache i generuje deterministyczne decyzje bez DB mutacji |

## Warstwa 1 - content identity

Ta warstwa odpowiada na pytanie: czy klient i serwer mówią o tej samej wersji
gry/moda?

Elementy:

- `content_game_targets`
- `content_revisions`
- `realm_realms.active_content_revision_id`
- `mmo_server_content_pack_files`
- `mmo_server_content_pack_manifests`
- `mmo_validate_client_content_pack(...)`
- `mmo_validate_client_content_pack_for_session(...)`

Wynik:

- klient pasuje do serwera i może wejść;
- albo klient dostaje reject z powodem i wymaganym hashem.

## Warstwa 2 - content inventory

Ta warstwa odpowiada na pytanie: jakie pliki serwer ma czytać później?

Nowe obiekty:

- `mmo_server_content_pack_inventory`
- `v_mmo_server_content_pack_inventory`
- `v_mmo_server_content_pack_inventory_health`
- `mmo_upsert_server_content_pack_inventory(...)`
- `tools/analyze_server_content_pack_inventory.py`

Najważniejsze role:

- `archive_vdf`
- `archive_mod`
- `world_zen`
- `scripts_dat`
- `dialog_ou`
- `script_source`
- `config_ini`
- `asset_texture`
- `asset_mesh`
- `asset_sound`
- `asset_video`
- `font`
- `other`

## Warstwa 3 - archive mount/pre-extract

Zrobione jako Step203/204/205.

Serwer musi umieć pracować z contentem w dwóch trybach:

1. Content root jest już rozpakowany.
2. Serwer ma VDF/MOD i sam mountuje albo rozpakowuje potrzebne pliki.

Planowane obiekty:

- `mmo_server_content_archive_mounts`
- `mmo_server_content_extracted_files`
- `tools/plan_server_content_archive_mounts.py`
- `tools/mmo_content_archive_mount_report.py`
- `tools/check_mmo_step203_server_content_archive_mounts.py`

Minimalny kontrakt:

- archive hash musi zgadzać się z manifestem;
- extracted file hash musi zgadzać się z manifestem;
- wynik mount/extract jest przypisany do `content_revision_key`;
- serwer może odtworzyć build contentu deterministycznie.

## Warstwa 4 - ZEN world import

Przyszły krok.

Przed właściwym parserem dodano Step206/207: `mmo_server_content_import_jobs`.
Ta kolejka tworzy joby `zen_world_import`, `daedalus_dat_index`,
`dialog_ou_index` i `archive_mount_verify`, więc przyszłe importery dostaną
konkretną listę prac zamiast ręcznie skanować content.

Dodano też Step208/209/210: `mmo_server_content_build_imports`,
`mmo_server_world_zen_entities`, `mmo_server_daedalus_symbols`,
`mmo_server_daedalus_npc_templates` i `mmo_server_dialog_outputs`. To jest
pierwszy przejściowy kontrakt zapisu wyników parserów w runtime DB.

Step211/212/213 rozdziela ten temat poprawniej: powstaje osobna baza
`mmo_content_build`. Parsery ZEN/DAT/OU mają pisać do niej, nie do runtime DB.
Runtime DB ma później dostać tylko zatwierdzony, gotowy content revision albo
read model wyeksportowany z `mmo_content_build`.

Step218 dodaje praktyczny etap przed importerem: `tools/discover_gothic_content_sources.py`
skanuje wskazany katalog Gothic II albo pre-extracted content root, wybiera
luźne ZEN/DAT/OU, liczy ich SHA-256 i generuje
`runtime/content_build/run_content_build_importer.sh`. Jeżeli instalacja ma
tylko `.vdf/.mod`, raport dostaje status `needs_extract_or_vfs_mount`, bo obecny
C++ importer czyta jeszcze konkretne luźne pliki.

Step222 dodaje przejściowy most do ZEN z archiwów: osobny target
`mmo_vdf_world_zen_probe` montuje VDF/MOD przez `zenkit::Vfs`, listuje
kandydatów `.ZEN` i opcjonalnie wyciąga wybrany świat, np. `newworld.zen`, do
`runtime/content_build/vfs_extracted`. Discovery dostało `--extra-root`, więc
obecny importer może nadal czytać luźne pliki bez przepisywania całego pipeline:
DAT/OU z instalacji Steam, a world ZEN z ekstraktu VFS.

ZEN daje:

- world name;
- startowe VOB-y;
- waypointy;
- freepointy;
- spatial hints;
- pozycje NPC/itemów z contentu;
- nazwy triggerów/interaktywnych obiektów.

Docelowe tabele w `mmo_content_build`:

- `content_build_revisions`
- `content_build_files`
- `content_build_imports`
- `world_zen_entities`
- `world_waypoint_edges`
- późniejsze specjalizacje dla VOB/triggers/movers/spawns, jeśli generic
  `world_zen_entities` okaże się za mało precyzyjne.

Pierwsza wersja importera nie musi renderować świata. Ma wyciągnąć stabilne
identyfikatory i pozycje, które serwer wykorzysta do AI, rutyn, spawnów i
walidacji ruchu.

## Warstwa 5 - Daedalus/DAT symbol index

Przyszły krok.

DAT daje:

- NPC templates;
- guildy;
- aivar/state;
- rutyny;
- funkcje percepcji;
- warunki dialogów;
- quest/script variables;
- item templates;
- dialog info IDs.

Docelowe tabele w `mmo_content_build`:

- `daedalus_symbols`
- `daedalus_npc_templates`
- `daedalus_perception_bindings`
- `daedalus_routines`
- `dialog_infos`
- `daedalus_item_templates`
- późniejsze `script_variables`, guild attitudes i inne indeksy, kiedy parser
  DAT zacznie wydobywać pełniejszy model.

Na początku nie trzeba odpalać pełnej maszyny Daedalusa. Najpierw wystarczy
indeks symboli i ekstrakcja tych danych, które są potrzebne do serwerowego NPC
interaction ticka.

Step217 rozwija ten etap: C++ importer nadal nie wykonuje skryptów, ale z
symbol table DAT emituje kandydatów do `npc_templates`, `item_templates`,
`dialog_infos`, `routines` i `perception_bindings`. Te wpisy są oznaczone w
raw payload jako ekstrakcja kandydacka i mają być później wzmacniane przez
kontrolowaną inicjalizację instancji albo analizę bytecode.

## Warstwa 5.5 - server script authority boundary

To jest odpowiedź na pytanie: kiedy serwer zacznie korzystać ze skryptów dla
wszystkich graczy, a nie tylko dla jednego klienta?

Serwer nie powinien odpalać osobnej kopii skryptów dla każdego gracza. Docelowy
model to jedna autorytatywna symulacja `world_instance`, w której:

- content build dostarcza indeks DAT/OU/ZEN;
- runtime read model daje serwerowi NPC templates, rutyny, perception bindings,
  dialog infos, item templates i script variables;
- server tick widzi wszystkich aktywnych graczy w tej samej instancji świata;
- warunki dialogów, rutyny, percepcja i reakcje NPC są oceniane względem
  konkretnego aktora/targetu, ale wynik zapisuje i rozgłasza serwer;
- klient wykonuje input, predykcję i prezentację, ale nie jest źródłem prawdy
  dla skryptowego świata.

Kolejność jest celowo rozdzielona:

1. `mmo_content_build` czyta skrypty jako content i buduje indeksy.
2. Zatwierdzony content revision publikuje read model dla runtime.
3. Serwer materializuje world instance z tego read modelu.
4. Server NPC/script tick ocenia reguły dla listy graczy i NPC.
5. Wyniki ticka trafiają do runtime DB/`mmo_ai_runtime` i do fan-out pakietów.

Step224 realizuje punkt 2 w wersji bootstrapowej. Narzędzie
`tools/export_content_build_runtime_read_model.py` bierze parser snapshot albo
widoki z `mmo_content_build`, zapisuje jeden JSON read-modelu dla cache serwera
i generuje SQL marker dla `content_build_runtime_exports`. To jeszcze nie
uruchamia skryptów ani ticka NPC; jest to kontrakt publikacji wspólnego contentu,
z którego później jedna `world_instance` będzie korzystać dla wszystkich graczy.

Step225 dodaje pierwszy C++ serwerowy punkt wejścia do tego kontraktu:
`mmo_runtime_read_model_loader` i target `mmo_runtime_read_model_probe`.
Probe czyta JSON z Step224, weryfikuje wersję schematu, format
`payload_sha256`, obecność wszystkich sekcji i zgodność liczników `summary` z
rzeczywistą liczbą rekordów w tablicach. To nadal jest read-only gate, ale od
tego momentu następny krok może budować cache indeksów NPC/rutyn/percepcji już
po stronie C++ serwera.

Step226 wykonuje ten następny krok bez uruchamiania symulacji: ten sam loader
materializuje rekordy i indeksy read-only po stabilnych kluczach. Probe raportuje
rozmiary indeksów i deterministyczne lookup-checki. Obecny kandydacki DAT model
nie ma jeszcze `npc_instance` dla rutyn ani `owner_symbol` dla perception
bindings, więc indeksy po tych polach są puste i raportowane jako ostrzeżenia,
nie jako błąd kontraktu.

Step227 buduje pierwszą warstwę `world_instance` content cache. Cache ładuje
`RuntimeReadModel`, twardo waliduje aktywny `content_revision_key`, wiąże dane z
`world_instance_key/world_name` i wystawia read-only lookup API dla ZEN entities,
waypoint edges, NPC/item templates, routines, perception bindings oraz
dialogów. To nadal nie wykonuje skryptów i nie mutuje DB.

Step228 wpina ten cache w start `mmo_udp_server`. Serwer dostaje jawne flagi
ścieżki read-modelu, aktywnej rewizji contentu, `world_instance_key` i
`world_name`; ładuje cache raz przy starcie, odrzuca mismatch rewizji i dopisuje
metadata `world_instance_content_cache` do bootstrap/live snapshot JSON, gdy
snapshot jest budowany.

Step229 dodaje pierwsza C++ warstwe policy dla percepcji NPC. Dostaje explicit
actor sets, filtruje NPC/gracz po dystansie, robi lookup `perception_kind` w
`WorldInstanceContentCache` i generuje kandydacka decyzje, np.
`PERC_ASSESSPLAYER -> greet_player`.

Step230 dodaje C++ actor-source/runtime recording probe. Potrafi pobrac
aktywnych graczy/NPC z runtime DB, uruchomic Step229 policy i zapisac decyzje
przez `mmo_ai_record_npc_perception_decision(...)`. To nadal nie jest
automatyczny tick UDP ani broadcast; obecny live DB sample ma slaba tozsamosc
NPC (`creature:None`, puste `npc_instance`), wiec przed wlaczeniem ticka trzeba
uszczelnic runtime NPC identity.

Do tego momentu klientowy Gothic nadal może wykonywać własne natywne skrypty w
trybie kompatybilności/server-bound bridge. To jest etap przejściowy, nie
docelowy model MMO.

## Warstwa 6 - OU/dialog output index

Przyszły krok.

OU/output daje:

- output names;
- subtitle text;
- audio references;
- mapping dialog/action -> linia mówiona.

Docelowe tabele w `mmo_content_build`:

- `dialog_outputs`
- późniejsze specjalizacje audio/subtitle, jeśli pojedyncza tabela przestanie
  wystarczać.

Serwer docelowo decyduje, że NPC mówi linię X. Klient tylko odtwarza audio,
napisy i kamerę.

## Warstwa 7 - server NPC perception policy

Rozpoczęte przez osobną bazę `mmo_ai_runtime`.

Tutaj zaczyna się zachowanie NPC względem wielu graczy.

Serwer musi mieć:

- listę aktywnych NPC w world instance;
- listę aktywnych graczy w world instance;
- spatial query dla zasięgu;
- line-of-sight albo uproszczony visibility test;
- cooldown percepcji per NPC/player;
- priorytety: walka > dialog > rutyna > zaczepka;
- pamięć tego, czy NPC już zaczepił danego gracza.

Planowane eventy:

- `npc_assess_player`
- `npc_turn_to_player`
- `npc_approach_player`
- `npc_greet_player`
- `npc_warn_player`
- `npc_start_dialog`
- `npc_attack_player`
- `npc_ignore_player`

Ten stan nie powinien trafiać do `mmo_content_build`, bo nie jest wynikiem
parsowania contentu. Nie powinien też być docelowo wrzucony bezpośrednio w
runtime DB, bo AI tick ma własny rytm, kolejki, cooldowny i dziennik decyzji.
Dlatego Step212 AI dodaje osobną bazę `mmo_ai_runtime`:

| Obiekt | Rola |
|---|---|
| `npc_perception_rule_catalog` | przyszła konfiguracja reguł percepcji |
| `npc_perception_cooldowns` | cooldown per world/NPC/target/perception |
| `npc_perception_decisions` | autorytatywny dziennik decyzji NPC |
| `npc_perception_action_queue` | kolejka skutków do broadcastu/wykonania |

Pierwszy kontrakt proceduralny to
`mmo_ai_record_npc_perception_decision(...)`. Przyszły serwerowy tick będzie
wywoływał go po wyliczeniu, że NPC powinien gracza zaczepić, ostrzec, rozpocząć
dialog albo zignorować.

Step213 rozwija to o dispatch queue contract:

| Procedura | Rola |
|---|---|
| `mmo_ai_claim_next_npc_perception_action(...)` | atomowo pobiera akcję NPC dla workera |
| `mmo_ai_mark_npc_perception_action_applied(...)` | potwierdza wysłanie/wykonanie skutku |
| `mmo_ai_mark_npc_perception_action_failed(...)` | zapisuje błąd i opcjonalnie retry |
| `mmo_ai_skip_npc_perception_action(...)` | pomija akcję, gdy target albo kontekst zniknął |

To jest bezpieczniejszy tor niż używanie `mmo_server_action_outbox` jako
domyślnej ścieżki NPC AI, bo outbox w obecnym systemie jest debug/fallback,
a percepcja NPC będzie miała własne retry, priorytety i fan-out.

Step215 domyka niespójność między klientowym feedem obserwacji a wspólnym
protokołem: `record_combat_intent`, `record_npc_action_state` i
`record_npc_dialog_line` mają już stabilne `SemanticActionKind`, a serwer
akceptuje je jako obserwacje fail-open. To nie jest jeszcze autorytatywny tick
NPC ani zapis do `mmo_ai_runtime`; pełny zapis powinien powstać dopiero po
opublikowaniu read modelu z `mmo_content_build`.

## Warstwa 8 - multiplayer fan-out

Przyszły krok.

To jest różnica między single-player klientem i MMO:

- jeden NPC może widzieć wielu graczy;
- wielu graczy może widzieć jednego NPC;
- dialog jednego gracza z NPC nie może psuć stanu innych graczy;
- animacja NPC musi być broadcastowana obserwatorom;
- UI dialogu dostaje tylko gracz uczestniczący;
- audio/napisy mogą dostać obserwatorzy według dystansu.

Serwer powinien emitować dwa typy skutków:

| Skutek | Dostaje |
|---|---|
| gameplay decision | uczestnik i DB |
| presentation delta | uczestnik + obserwatorzy w zasięgu |

## Warstwa 9 - DB rewrite

Obecna baza jest celowo ewolucyjna. Jest dobra do inkrementalnych kroków, ale
docelowo zostanie przepisana/utwardzona.

Docelowy podział:

| Baza/obszar | Rola |
|---|---|
| `mmo_content_build` | wynik parsowania ZEN/DAT/OU dla konkretnej rewizji |
| runtime MMO DB | aktywne sesje, postacie, NPC, world instances |
| `mmo_ai_runtime` | decyzje AI/percepcji, cooldowny i kolejka akcji NPC |
| event/audit DB | rejecty, dialog events, korekty klienta, długie audyty |
| cache/read model | szybkie indeksy dla ticka serwera |

Najważniejsza zasada migracji: najpierw budować kontrakty i narzędzia,
potem przepisać storage pod te kontrakty. Dzięki temu obecne kroki nie są
wyrzucane, tylko stają się specyfikacją dla nowej bazy.
