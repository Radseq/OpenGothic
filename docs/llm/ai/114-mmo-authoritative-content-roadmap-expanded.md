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

## Aktualny stan po Step190-202

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

ZEN daje:

- world name;
- startowe VOB-y;
- waypointy;
- freepointy;
- spatial hints;
- pozycje NPC/itemów z contentu;
- nazwy triggerów/interaktywnych obiektów.

Planowane tabele:

- `mmo_server_world_zen_imports`
- `mmo_server_zen_vobs`
- `mmo_server_zen_waypoints`
- `mmo_server_zen_freepoints`
- `mmo_server_zen_triggers`
- `mmo_server_zen_spawn_points`

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

Planowane tabele:

- `mmo_server_daedalus_symbols`
- `mmo_server_npc_templates`
- `mmo_server_npc_perceptions`
- `mmo_server_npc_routines`
- `mmo_server_dialog_infos`
- `mmo_server_script_variables`
- `mmo_server_item_templates`

Na początku nie trzeba odpalać pełnej maszyny Daedalusa. Najpierw wystarczy
indeks symboli i ekstrakcja tych danych, które są potrzebne do serwerowego NPC
interaction ticka.

## Warstwa 6 - OU/dialog output index

Przyszły krok.

OU/output daje:

- output names;
- subtitle text;
- audio references;
- mapping dialog/action -> linia mówiona.

Planowane tabele:

- `mmo_server_dialog_outputs`
- `mmo_server_dialog_output_audio`
- `mmo_server_dialog_output_subtitles`

Serwer docelowo decyduje, że NPC mówi linię X. Klient tylko odtwarza audio,
napisy i kamerę.

## Warstwa 7 - server NPC perception policy

Przyszły krok.

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
| content build DB | wynik parsowania ZEN/DAT/OU dla konkretnej rewizji |
| runtime MMO DB | aktywne sesje, postacie, NPC, world instances |
| event/audit DB | rejecty, decyzje AI, dialog events, korekty klienta |
| cache/read model | szybkie indeksy dla ticka serwera |

Najważniejsza zasada migracji: najpierw budować kontrakty i narzędzia,
potem przepisać storage pod te kontrakty. Dzięki temu obecne kroki nie są
wyrzucane, tylko stają się specyfikacją dla nowej bazy.
