#ifndef ASIO_STANDALONE
#define ASIO_STANDALONE
#endif

#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wconversion"
#endif
#if defined(__has_include)
#  if __has_include(<asio.hpp>)
#    include <asio.hpp>
#  elif __has_include("../../thirdparty/asio/include/asio.hpp")
#    include "../../thirdparty/asio/include/asio.hpp"
#  else
#    error "mmo_udp_server requires thirdparty/asio/include/asio.hpp"
#  endif
#else
#  include <asio.hpp>
#endif
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic pop
#endif

#include <array>
#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cctype>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <exception>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>
#include <iostream>
#include <limits>

#include "../../game/game/mmonetprotocol.h"
#include "mmo_server_snapshot_limits.h"
#include "mmo_server_types.h"
#include "mmo_server_combat_authority.h"
#include "mmo_server_combat_outcome_authority.h"
#include "mmo_server_combat_timeline_authority.h"
#include "mmo_server_conversation_authority.h"
#include "mmo_server_dialog_authority.h"
#include "mmo_server_direct_apply_api.h"
#include "mmo_server_gameplay_authority.h"
#include "mmo_server_identity.h"
#include "mmo_server_inventory_authority.h"
#include "mmo_server_movement_authority.h"
#include "mmo_server_npc_activity_authority.h"
#include "mmo_server_npc_action_authority.h"
#include "mmo_server_persistence.h"
#include "mmo_server_quest_authority.h"
#include "mmo_server_script_authority.h"
#include "mmo_server_story_authority.h"
#include "mmo_server_waypoint_authority.h"
#include "mmo_server_waypoint_bootstrap.h"
#include "mmo_server_world_clock.h"

namespace {

std::atomic_bool gRunning {true};
Mmo::Server::Conversation::Registry gConversationRegistry;
Mmo::Server::NpcActivity::Registry gNpcActivityRegistry;
Mmo::Server::CombatTimeline::Registry gCombatTimelineRegistry;

struct ServerPacketLogState final {
  std::uint64_t suppressedMovementLines = 0;
  std::uint64_t nextMovementSummaryAt = 100;
  std::uint64_t suppressedWeaponStateLines = 0;
  std::uint64_t nextWeaponStateSummaryAt = 25;
};

struct LiveWorldSnapshotState final {
  bool          initialized = false;
  double        lastX = 0.0;
  double        lastY = 0.0;
  double        lastZ = 0.0;
  std::uint64_t lastTick = 0;
};

[[nodiscard]] std::optional<std::uint16_t> rawClientActionKind(std::string_view bytes) noexcept {
  constexpr std::size_t ActionKindOffset = 4 + 2 + 2 + 2;
  if(bytes.size() < ActionKindOffset + 2)
    return std::nullopt;
  const auto lo = static_cast<std::uint16_t>(static_cast<unsigned char>(bytes[ActionKindOffset]));
  const auto hi = static_cast<std::uint16_t>(static_cast<unsigned char>(bytes[ActionKindOffset + 1]));
  return static_cast<std::uint16_t>(lo | (hi << 8));
}

using Mmo::Server::BootstrapReadiness;
using Mmo::Server::DirectApplyResult;
using Mmo::Server::MySqlTarget;
using Mmo::Server::Options;
using Mmo::Server::WorldItemIdentity;
using Mmo::Server::DbBridgeVersion;
using Mmo::Server::sqlBool;
using Mmo::Server::sqlJson;
using Mmo::Server::sqlLiteral;
using Mmo::Server::parseMysqlUrl;
using Mmo::Server::runMysql;
using Mmo::Server::splitMysqlLastRow;
using Mmo::Server::mysqlSingleField;
using Mmo::Server::mysqlSingleFieldWithDiagnostic;
using Mmo::Server::mysqlJsonOr;
using Mmo::Server::mysqlJsonOrWithDiagnostic;
using Mmo::Server::concatenateJsonArrays;
using Mmo::Server::dbLogin;
using Mmo::Server::ensureActiveDbSession;
using Mmo::Server::readBootstrapReadinessWithFallback;
using Mmo::Server::readCharacterBootstrapSnapshotSlices;
using Mmo::Server::readNpcAuthoritySnapshotSlices;
using Mmo::Server::readWorldBootstrapSnapshotSlices;
using Mmo::Server::buildSaveCheckpointBootstrapSnapshotJson;

void stopHandler(int) {
  gRunning.store(false, std::memory_order_relaxed);
}

void cancelInterruptedConversation(const Mmo::Server::NpcActivity::ApplyResult& activity) {
  if(!activity.preempted || !activity.interrupted)
    return;
  if(activity.interrupted->kind != Mmo::Server::NpcActivity::Kind::Talking)
    return;
  (void)gConversationRegistry.cancel(activity.interrupted->syncGroup);
}

#include "mmo_udp_server_json_payload.inl"
#include "mmo_udp_server_payload_mapper.inl"
#include "mmo_udp_server_bootstrap_snapshot.inl"
#include "mmo_udp_server_transport_diagnostics.inl"
#include "mmo_udp_server_character_actions.inl"
#include "mmo_udp_server_story_payloads.inl"
#include "mmo_udp_server_world_npc_resolution.inl"
#include "mmo_udp_server_world_item_resolution.inl"
#include "mmo_udp_server_inventory_resolution.inl"
#include "mmo_udp_server_direct_story_apply.inl"
#include "mmo_udp_server_direct_combat_apply.inl"
#include "mmo_udp_server_direct_world_state_apply.inl"
#include "mmo_udp_server_direct_inventory_apply.inl"
#include "mmo_udp_server_direct_interactive_apply.inl"
#include "mmo_udp_server_direct_apply.inl"
#include "mmo_udp_server_main_loop.inl"
