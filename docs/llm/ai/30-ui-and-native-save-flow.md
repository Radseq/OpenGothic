# UI And Native Save Flow

Files: `game/mainwindow.*`, `game/ui/menuroot.*`, `game/ui/gamemenu.*`, `game/ui/dialogmenu.*`, `game/ui/inventorymenu.*`.

- `MainWindow::setupUi` installs the menu, dialog, inventory, document, chapter, video, and console widgets.
- `MenuRoot` owns the Daedalus menu VM and the menu stack.
- `GameMenu` renders script-defined menus and invokes normal save/load commands through `Gothic`.
- Dialog and inventory widgets present gameplay state; `GameSession` and `GameScript` own the mutations.

Keep native save/load operational while the database path reaches equivalence. UI must request a gameplay action, not write MMO rows directly.

