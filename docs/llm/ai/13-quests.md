# Quest State

Files: `game/game/questlog.h`, `game/game/questlog.cpp`.

`QuestLog::Quest` contains name, section, status, and ordered text entries.

- Statuses: `Running=1`, `Success=2`, `Failed=3`, `Obsolete=4`.
- `add` is idempotent by quest name.
- `setStatus` creates a missing non-obsolete quest in the Mission section.
- `replace` is the DB restore primitive used through `GameScript`.
- Native save/load serializes the complete ordered vector.

For MMO data, preserve section, status, entry order, and text. A missing quest is semantically different from an obsolete one.

