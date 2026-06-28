# Resources And Assets

Files: `game/resources.h`, `game/resources.cpp`, `game/gothic.cpp`.

- `Resources` is a process-global asset cache backed by the Gothic virtual filesystem.
- `Gothic` detects installation/mod settings, then calls `Resources::loadVdfs` and `Resources::mountWork` before world content is loaded.
- `Resources` loads textures, meshes, skeletons, animations, particle meshes, fonts, sounds, and VOB bundles.
- Asset cache keys are content names; they are not player, character, or world-instance identifiers.

Database rows should reference canonical game definitions and immutable content revisions where needed. Do not persist cache pointers, Vulkan/Tempest resource objects, or VFS internals.

