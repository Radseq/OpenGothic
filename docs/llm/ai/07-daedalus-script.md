# Daedalus Script State

Files: `game/game/gamescript.h`, `game/game/gamescript.cpp`.

- `saveQuests` / `loadQuests`: quest log, known dialog pairs, guild-attitude matrix.
- `saveVar` / `loadVar`: mutable Daedalus symbol values.
- `restoreQuestLogForPersistence`, `restoreKnownDialogsForPersistence`, `restoreGlobal*ForPersistence`, and `restoreGuildAttitudeForPersistence` are the only supported DB restore APIs for script-owned state.
- `knownDialogInfos()` and `dialogInfos()` are capture sources.
- `goldId()` identifies the currency item; `currencyName()` gives its localized display name.

Never write VM internals directly from persistence code. Use the restore APIs to preserve script ownership and validation.

