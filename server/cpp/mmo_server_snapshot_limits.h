#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Mmo::Server {

inline constexpr std::size_t BootstrapSnapshotChunkPayloadBytes = 8192;
inline constexpr std::size_t MaxBootstrapInventoryRows = 2048;
inline constexpr std::size_t MaxBootstrapEquipmentRows = 64;
inline constexpr std::size_t MaxBootstrapKnownDialogRows = 4096;
inline constexpr std::size_t MaxBootstrapQuestRows = 1024;
inline constexpr std::size_t MaxBootstrapScriptStateRows = 16384;
inline constexpr std::size_t MaxBootstrapWorldDeltaRows = 4096;
inline constexpr std::size_t MaxBootstrapActiveWorldItemRows = 1024;
inline constexpr std::size_t MaxBootstrapNearbyNpcRows = 256;
inline constexpr std::size_t MaxBootstrapNearbyNpcKnownDialogRows = 512;
inline constexpr std::size_t MaxBootstrapNearbyWaypointRows = 256;
inline constexpr double BootstrapActiveWorldItemRadius = 12000.0;
inline constexpr double BootstrapNearbyNpcRadius = BootstrapActiveWorldItemRadius;
inline constexpr double BootstrapNearbyWaypointRadius = BootstrapActiveWorldItemRadius;
inline constexpr double BootstrapAuthoritativeItemClearRadius = BootstrapActiveWorldItemRadius;
inline constexpr double LiveWorldItemRefreshDistance = 5000.0;
inline constexpr double LiveWorldItemRefreshMinMoveDistance = 1200.0;
inline constexpr std::uint64_t LiveWorldItemRefreshMaxIntervalMs = 20000;
inline constexpr std::size_t MaxBootstrapInteractiveStateRows = 2048;
inline constexpr std::size_t MaxBootstrapNpcLifecycleRows = 2048;
inline constexpr std::size_t MaxBootstrapInteractiveSampleRows = MaxBootstrapInteractiveStateRows;
inline constexpr std::size_t MaxBootstrapRecentEventRows = 64;
inline constexpr std::size_t MaxBootstrapMoverStateRows = 512;

inline constexpr std::string_view BootstrapSnapshotSchema = "mmo_bootstrap_snapshot_v1";
inline constexpr std::string_view BootstrapWorldItemDeltasSection = "world_item_deltas";
inline constexpr std::string_view BootstrapActiveWorldItemsSection = "active_world_items";
inline constexpr std::string_view BootstrapNearbyNpcsSection = "nearby_npcs";
inline constexpr std::string_view BootstrapNearbyNpcKnownDialogsSection = "nearby_npc_known_dialogs";
inline constexpr std::string_view BootstrapNearbyWaypointsSection = "nearby_waypoints";
inline constexpr std::string_view BootstrapRecentActionsSection = "recent_actions";
inline constexpr std::string_view BootstrapMoverStateSection = "mover_state";
inline constexpr std::string_view BootstrapServerCheckpointManifestSection = "server_checkpoint_manifest";
inline constexpr std::string_view BootstrapInteractiveStateSection = "interactive_state";
inline constexpr std::string_view BootstrapNpcLifecycleStateSection = "npc_lifecycle_state";

} // namespace Mmo::Server












