# Renderer

Files: `game/graphics/renderer.h`, `game/graphics/renderer.cpp`, `game/mainwindow.cpp`.

- `MainWindow::render` owns command buffers, fences, frame index, and swapchain presentation.
- `Renderer::draw` records the scene and UI composition for one frame.
- `Renderer` creates and resizes transient render targets through `usesImage*`, `usesZBuffer`, and `usesSsbo`.
- Render stages include visibility, Hi-Z, shadows, G-buffer, lighting, water, sky, fog, postprocessing, and UI.

GPU attachments, descriptor state, command buffers, and frame indices are transient. Never serialize or replicate them. New graphics code must remain on the Vulkan-Hpp path; do not add OpenGL, DirectX, GLEW, or volk compatibility code.

