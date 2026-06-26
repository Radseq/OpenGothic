# Application Lifecycle

Files: `game/main.cpp`, `game/gothic.h`, `game/gothic.cpp`, `game/mainwindow.*`.

- `main` creates the graphics device, global `Resources`, `Gothic`, `GameMusic`, and then `MainWindow`.
- `Gothic` owns the current `GameSession` and asynchronous native save/load transitions.
- `MainWindow` owns the OS window, frame loop, input dispatch, UI widgets, swapchain, and `Renderer`.
- `startGame`, `loadGame`, and `saveGame` connect `Gothic` signals to native session transitions.

Keep durable MMO state out of `MainWindow`, `Renderer`, and global singletons. Database restore belongs after a `GameSession` has constructed its world and scripts.

