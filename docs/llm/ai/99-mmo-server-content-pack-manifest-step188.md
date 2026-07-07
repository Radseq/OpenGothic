# MMO server content pack manifest - step 188

Ten krok zaczyna praktyczna realizacje roadmapy: serwer ma miec wlasne pliki contentu gry/moda
i nie moze czytac ani ufac plikom z komputera gracza.

## Co zostalo dodane

- `server/sql/step188_server_content_pack_manifest.sql`
  - `mmo_server_content_pack_files` - lista plikow content packa serwera:
    - logiczna sciezka,
    - typ zrodla (`zen`, `dat`, `ou`, `vdf`, `script`, `ini`, itd.),
    - rozmiar,
    - SHA-256,
    - payload diagnostyczny.
  - `mmo_server_content_pack_manifests` - hash calej paczki dla `content_revision`.
  - `v_mmo_server_content_pack_manifests` i `v_mmo_server_content_pack_files`.
  - `mmo_upsert_server_content_pack_file(...)`.
  - `mmo_set_server_content_pack_manifest(...)`.
  - `mmo_validate_client_content_pack(...)`.
- `tools/register_server_content_pack_manifest.py`
  - wrapper na `tools/bootstrap/register_server_content_pack_manifest.py`.
  - skanuje serwerowy katalog Gothic/mod,
  - hashuje pliki contentowe, domyslnie m.in. `.zen`, `.dat`, `.bin`, `.ou`, `.vdf`, `.mod`,
    `.d`, `.src`, `.ini`,
  - zapisuje JSON manifest,
  - generuje SQL,
  - opcjonalnie rejestruje wynik w MySQL.
- `tools/check_mmo_step188_server_content_pack_manifest.py`
  - wrapper na check walidujacy tabele, procedury i opcjonalny hash klienta.
- `tools/bootstrap/apply_current_mmo_db_state.py`
  - dostal nowy SQL surface `server_content_pack_manifest`.

## Dlaczego to jest wazne

NPC zaczepiajacy gracza, percepcja, dialogi, rutyny i walka nie moga zalezec od lokalnych plikow
klienta. Docelowo klient ma tylko prezentowac wynik decyzji serwera.

Ten krok daje pierwsza twarda zasade:

1. Serwer ma wlasny content pack.
2. Serwer zapisuje hash kazdego istotnego pliku.
3. Serwer zapisuje hash calej paczki.
4. Klient przy logowaniu bedzie mogl wyslac swoj manifest hash.
5. DB potrafi powiedziec `ok`, `content_hash_mismatch`, `client_manifest_missing` albo
   `server_manifest_missing`.

To jeszcze nie parsuje semantyki ZEN/DAT/OU, ale daje bramke wersji contentu. Bez tej bramki
serwerowa AI moglaby podejmowac decyzje na innych danych niz klient wyswietla.

## Przykladowe uzycie

Najpierw zastosowac SQL:

```bash
python3 tools/apply_current_mmo_db_state.py \
  --url "mysql://gothic:gothic_dev_password@127.0.0.1:3306/gothic_mmo_ch1_clean" \
  --output runtime/current_mmo_db_state/apply.json
```

Potem zarejestrowac content pack z katalogu serwera:

```bash
python3 tools/register_server_content_pack_manifest.py \
  --url "mysql://gothic:gothic_dev_password@127.0.0.1:3306/gothic_mmo_ch1_clean" \
  --content-root "/mnt/windows-games/Games/Steam/steamapps/common/Gothic II" \
  --content-revision-key "runtime-sqlite:g2notr:..." \
  --source-root-label "g2notr-clean-server-pack" \
  --output runtime/step188_server_content_pack_manifest/manifest.json \
  --sql-output runtime/step188_server_content_pack_manifest/register.sql
```

Sprawdzenie:

```bash
python3 tools/check_mmo_step188_server_content_pack_manifest.py \
  --url "mysql://gothic:gothic_dev_password@127.0.0.1:3306/gothic_mmo_ch1_clean" \
  --realm-key "default" \
  --client-manifest-hash "<hash-z-manifestu-klienta>" \
  --output runtime/step188_server_content_pack_manifest/check.json
```

## Granice tego kroku

- Nie ma jeszcze klientowego wysylania `client_manifest_hash`.
- Nie ma jeszcze C++ login gate wywolujacego `mmo_validate_client_content_pack`.
- Narzedzie hashuje pliki, ale jeszcze nie parsuje ZEN/DAT/OU do normalized authority tables.
- Hash manifestu jest bramka wersji contentu, nie antycheat.
- Domyslny skan celuje w pliki logicznie istotne dla serwera. Pelny hash calej paczki mozna
  wymusic przez `--include-all`, ale wtedy wejda tez assety prezentacyjne.
- Obecna baza nadal jest pomostem. W przyszlosci baza powinna zostac przepisana pod jawniejszy
  model: content store, runtime world service, command/event stream i projekcje read-model.

## Nastepny krok

Najblizszy sensowny krok techniczny:

- dodac do protokolu/logowania klienta `client_content_manifest_hash`,
- w C++ serwerze wywolac `mmo_validate_client_content_pack`,
- odrzucic albo ograniczyc sesje z niezgodnym hashem,
- zaczac parser/import ZEN jako `content_world_files`, `content_waypoints`, `content_freepoints`,
  `content_vobs`, `content_movers`.
