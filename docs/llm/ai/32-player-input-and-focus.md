# Player Input And Focus

Files: `game/game/playercontrol.*`, `game/world/focus.*`, `game/mainwindow.cpp`.

- `MainWindow` forwards input events to `PlayerControl` after UI handling.
- `PlayerControl` turns key/mouse state into player movement, weapon actions, focus interactions, camera movement, lockpicking, and hotkeys.
- `Focus` is a local ray/selection result used to choose an NPC, item, or interactive target.
- Interaction methods call gameplay objects; they are useful client-intent boundaries for future MMO requests.

Do not save raw keys, focus pointers, button state, or mouse deltas. An MMO protocol should send validated intent and stable target identity, then let server authority decide the mutation.
