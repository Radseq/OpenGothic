# Camera

Files: `game/camera.*`, `game/mainwindow.cpp`, `game/world/triggers/cscamera.*`.

- `Camera` owns local view mode, rotation, zoom, collision avoidance, projection matrices, and audio listener position.
- `MainWindow::tickCamera` advances it after gameplay/input updates.
- Native saves serialize camera state through `Camera::save` and `Camera::load` for local continuity.
- `CsCamera` can temporarily drive a cutscene view through `World::currentCs`.

Camera state is client presentation, not MMO authority. A server validates character position and action intent; clients choose their own camera and rebuild it after a DB restore.

