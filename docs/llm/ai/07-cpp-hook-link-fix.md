# 07 C++ Hook Link Fix

Symptom:

```text
undefined reference to `Mmo::Hooks::onItemUnequipped(...)`
undefined reference to `Mmo::Hooks::onItemEquipped(...)`
undefined reference to `Mmo::Hooks::onInventoryTransfer(...)`
undefined reference to `Mmo::configureSemanticActionSink(...)`
undefined reference to `Mmo::shutdownSemanticActionSink(...)`
undefined reference to `Mmo::Hooks::onWorldItemPickedUp(...)`
undefined reference to `Mmo::Hooks::onWorldItemRemoved(...)`
```

Cause: hook call sites compiled, but new implementation files are not part of the `Gothic2Notr` CMake source list.

Required target sources:

```text
game/game/mmosemanticevents.cpp
game/game/mmosemanticactionsink.cpp
game/game/mmosemantichooks.cpp
```

Fix script:

```bash
python3 tools/apply_mmo_hook_cmake_fix.py --root . --apply
cmake --build build -j"$(nproc)"
```

The script searches project CMake files for an existing game source anchor such as `game/game/mmoruntimesqlite.cpp` and inserts the missing hook sources next to it. It writes a `.before-mmo-hooks-link-fix` backup before modifying the CMake file.

Design rule remains unchanged: client code must only emit semantic envelopes/sink entries; no gameplay thread MySQL/network blocking is allowed.
