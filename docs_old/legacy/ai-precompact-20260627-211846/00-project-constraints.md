# Project Constraints

- Target: Gothic II NotR first, then a server-authoritative MMO.
- Use C++23 and Vulkan-Hpp only. Do not introduce GLEW, volk, OpenGL, or DirectX fallback code.
- Preserve regular Gothic save/load as a compatibility and comparison path until DB restore has equivalent coverage.
- Treat display names as labels, never as identity. Keep stable engine keys and expose separate human-facing references.
- Prefer incremental component writes over periodic whole-world replacement.

