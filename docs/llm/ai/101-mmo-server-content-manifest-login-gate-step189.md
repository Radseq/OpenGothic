# MMO server content manifest login gate - step 189

Ten krok robi kolejny praktyczny fragment roadmapy: C++ UDP server potrafi walidowac
client-declared content manifest hash po DB loginie.

## Co zostalo dodane

- `server/sql/step189_server_content_pack_session_gate.sql`
  - dodaje procedure `mmo_validate_client_content_pack_for_session(...)`,
  - resolver idzie przez `server_sessions -> realm_realms -> content_revisions`,
  - nie trzeba zgadywac `realm_key` po stronie C++.
- `tools/bootstrap/apply_current_mmo_db_state.py`
  - dostal SQL surface `server_content_pack_session_gate`,
  - Step189 idzie po Step188.
- `server/cpp/mmo_server_types.h`
  - `clientContentManifestHash`,
  - `requireClientContentManifest`.
- `server/cpp/mmo_server_persistence.cpp/.h`
  - `validateClientContentManifestForSession(...)`.
- `server/cpp/mmo_udp_server_main_loop.inl`
  - CLI:
    - `--client-content-manifest-hash HASH`,
    - `--require-client-content-manifest`,
    - `--no-require-client-content-manifest`,
  - walidacja po starcie,
  - walidacja po session recovery,
  - walidacja po zmianie postaci w bootstrapie.

## Tryby pracy

Domyslnie gate jest `off`, zeby nie zepsuc obecnego dev flow.

Jesli podasz tylko:

```bash
--client-content-manifest-hash <hash>
```

serwer robi walidacje ostrzegawcza. Loguje:

```text
[content_manifest_validation] accepted=... required=0 reason=...
```

Jesli walidacja DB rzuci blad w trybie ostrzegawczym, serwer loguje
`[content_manifest_validation_failed_open]` i dziala dalej.

Jesli podasz:

```bash
--client-content-manifest-hash <hash> --require-client-content-manifest
```

serwer traktuje hash jako twarda bramke. Mismatch, brak manifestu klienta, brak manifestu serwera
albo brak aktywnej sesji konczy sie bledem.

Przyklad z hashem wygenerowanym przez Step188:

```bash
CONTENT_HASH="$(python3 -c "import json; print(json.load(open('runtime/step188_server_content_pack_manifest/manifest.json'))['manifest_hash'])")"

./build/mmo_cpp_server/mmo_udp_server \
  --bind 127.0.0.1:29777 \
  --mysql-url "mysql://gothic:gothic_dev_password@127.0.0.1:3306/gothic_mmo_ch1_clean" \
  --session-key local-dev-PC_HERO_TEST \
  --character-key PC_HERO \
  --client-content-manifest-hash "$CONTENT_HASH" \
  --require-client-content-manifest
```

## Dlaczego to jest wazne

To jest pierwszy realny enforcement po stronie C++:

1. DB ma serwerowy content pack manifest.
2. Klient deklaruje hash.
3. Serwer po loginie pyta DB, czy hash pasuje do aktywnego content revision realmu.
4. W trybie required sesja z innym contentem nie przechodzi.

To nadal nie jest pelny antycheat. To kontrakt zgodnosci wersji contentu, potrzebny zanim serwer
zacznie wykonywac ZEN/DAT/OU jako prawde dla NPC AI, dialogow i perception.

## Granice tego kroku

- Hash jest jeszcze podawany przez CLI, nie przez realny pakiet klienta.
- Klient OpenGothic nie liczy jeszcze lokalnego manifestu contentu.
- Serwer nie rozsyla jeszcze oczekiwanego hash/manifest reason w typed diagnostic packet.
- Step189 zaklada, ze Step188 zostal zastosowany i manifest serwera jest zarejestrowany.

## Nastepny krok

Nastepny dobry krok:

- dodac `client_content_manifest_hash` do bootstrap/client hello payload,
- klient liczy ten hash z lokalnego content manifestu,
- serwer bierze hash z pakietu zamiast z CLI,
- diagnostic packet informuje klienta o mismatchu i oczekiwanym `server_manifest_hash`.
