# Task Brief

## Cel

Zbudowac techniczna sciezke od istniejacego mechanizmu save/load OpenGothic do bazy danych, ktora moze sluzyc jako persistent state dla produkcyjnego MMO Gothic.

Kolejnosc targetow:

1. Gothic 2 NK / NotR.
2. Gothic 1.
3. Gothic 2 vanilla.

## Hipoteza robocza

Najpierw trzeba odczytac poczatkowy stan gry po `New Game`.

Praktycznie oznacza to:

1. Uruchomic nowa gre.
2. Poczekac az swiat, skrypty, HERO i triggery startowe sa zainicjalizowane.
3. Natychmiast wykonac automatyczny save albo export.
4. Potraktowac ten stan jako `canonical baseline`.

Reczny save po pojawieniu sie gry jest dobrym eksperymentem, ale docelowo powinien powstac deterministyczny tryb w kodzie, np. `--dump-initial-world`, zeby eksport nie zalezal od refleksu uzytkownika, FPS ani losowego ticka AI.

## Definicja sukcesu pierwszego etapu

Pierwszy etap jest gotowy, gdy repo potrafi:

1. Uruchomic Gothic 2 NK w trybie eksportu.
2. Zlapac stabilny stan po `New Game`.
3. Wyeksportowac liste bytow swiata do pliku strukturalnego, najlepiej JSONL albo SQLite staging.
4. Powtorzyc eksport dwa razy i uzyskac logicznie taki sam wynik.
5. Udokumentowac roznice niedeterministyczne, jesli wystepuja.

Minimalny eksport:

- world name
- entity kind: npc, item, mob/interactable, trigger
- stable source id, jesli jest dostepny
- symbol index / instance id
- display name / script name
- position
- rotation albo matrix
- inventory dla NPC i containerow
- state flags istotne dla persistent world

## Czego nie robic na starcie

Nie projektowac od razu pelnej architektury MMO, walki sieciowej i shardingu.

Nie traktowac zip save'a jako docelowej bazy produkcyjnej. Blob save'a moze byc backupem lub formatem migracyjnym, ale nie powinien byc jedynym stanem MMO.

Nie mieszac eksportera z debugowym dopisywaniem tekstu do `logs/*.txt`. To bylo dobre do szukania danych, ale teraz potrzebny jest jawny modul z deterministycznym formatem.
