# Step216 - server script authority boundary

Ten krok doprecyzowuje, kiedy serwer zacznie korzystać ze skryptów dla wielu
graczy jednocześnie.

## Odpowiedź

Serwer zacznie używać skryptów jako wspólnej prawdy dopiero po opublikowaniu
runtime read modelu z `mmo_content_build` i po dodaniu server-side
NPC/script ticka.

To nie powinno działać tak, że serwer odpala osobny świat/skrypty dla każdego
gracza. Docelowo serwer ma jedną autorytatywną `world_instance`, w której tick
widzi wszystkich aktywnych graczy i NPC.

## Dlaczego nie teraz

Po Step214/215 serwer potrafi już:

- walidować content manifest klienta;
- czytać serwerowy content pack przez izolowany importer;
- zapisać parser snapshot do `mmo_content_build`;
- przyjmować obserwacje NPC jako fail-open bridge.

Serwer nadal nie ma jednak kompletnego runtime read modelu:

- NPC templates z DAT nie są jeszcze wyciągnięte jako stabilny model;
- rutyny i perception bindings nie są jeszcze gotowe dla ticka;
- dialog infos i warunki dialogów nie są jeszcze podpięte do serwerowej decyzji;
- nie ma server-side symulacji rutyn/percepcji/fan-out dla wielu graczy.

Dlatego pełne wykonanie skryptów teraz byłoby przedwczesne. Najpierw trzeba
zrobić content read model, potem server tick.

## Docelowy flow

1. Operator/server wskazuje własny content pack.
2. Importer buduje snapshot ZEN/DAT/OU.
3. `mmo_content_build` zapisuje i waliduje snapshot.
4. Zatwierdzony content revision publikuje runtime read model.
5. Server materializuje world instance: NPC, gracze, waynet, rutyny, dialogi,
   script state.
6. Server NPC/script tick ocenia reguły dla wszystkich aktywnych graczy.
7. Wyniki trafiają do DB, `mmo_ai_runtime` i pakietów fan-out.

## Przykład

Jeżeli dwóch graczy stoi obok tego samego NPC:

- serwer ma jedną kopię NPC w world instance;
- tick sprawdza obu graczy jako potencjalne targety;
- cooldown/perception/dialog availability są liczone per NPC/target;
- decyzja NPC jest jedna autorytatywna i rozgłaszana zainteresowanym klientom;
- UI dialogu może dostać tylko uczestnik, ale animacja/głos/napisy mogą iść do
  obserwatorów według zasięgu.

## Następny techniczny krok

Najbliższy praktyczny krok to nie pełna VM Daedalusa, tylko mocniejszy DAT/OU
read model:

- pełniejszy indeks symboli DAT;
- NPC templates;
- item templates;
- dialog infos;
- routine/perception binding candidates;
- eksport read modelu z `mmo_content_build` do runtime/server cache.




