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
#include <deque>
#include <exception>
#include <initializer_list>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>
#include <iostream>

#include "../../game/game/mmonetprotocol.h"
#include "mmo_server_snapshot_limits.h"
#include "mmo_server_types.h"
#include "mmo_server_combat_authority.h"
#include "mmo_server_combat_outcome_authority.h"
#include "mmo_server_combat_timeline_authority.h"
#include "mmo_server_conversation_authority.h"
#include "mmo_server_damage_calculator.h"
#include "mmo_server_dialog_authority.h"
#include "mmo_server_direct_apply_api.h"
#include "mmo_server_fight_intent_authority.h"
#include "mmo_server_gameplay_authority.h"
#include "mmo_server_identity.h"
#include "mmo_server_inventory_authority.h"
#include "mmo_server_movement_authority.h"
#include "mmo_server_npc_activity_authority.h"
#include "mmo_server_npc_action_authority.h"
#include "mmo_server_perception_queue.h"
#include "mmo_server_perception_reaction_planner.h"
#include "mmo_server_perception_witness_registry.h"
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
Mmo::Server::FightIntent::Registry gFightIntentRegistry;
Mmo::Server::Perception::Queue gPerceptionQueue;
Mmo::Server::Perception::WitnessRegistry gPerceptionWitnessRegistry;
std::deque<Mmo::Net::ServerLiveDeltaPacket> gPerceptionLiveDeltas;

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

[[nodiscard]] Mmo::Server::NpcActivity::ApplyResult applyNpcActivityCommand(
    std::string_view actorKey,
    std::string_view actionKey,
    std::string_view actionState,
    std::string_view targetKey,
    std::string_view syncGroup,
    std::uint64_t serverTickMs,
    std::uint32_t expectedDurationMs,
    std::string_view rejectionLogTag) {
  const auto activity = gNpcActivityRegistry.applyActivity({
    .actorKey = actorKey,
    .actionKey = actionKey,
    .actionState = actionState,
    .targetKey = targetKey,
    .syncGroup = syncGroup,
    .serverTickMs = serverTickMs,
    .expectedDurationMs = expectedDurationMs,
  });
  if(activity.accepted) {
    cancelInterruptedConversation(activity);
  } else if(!rejectionLogTag.empty()) {
    std::cerr << "[" << rejectionLogTag << "]"
              << " actor=" << actorKey
              << " target=" << targetKey
              << " action=" << actionKey
              << " reason=" << activity.reason
              << "\n";
  }
  return activity;
}

void appendPerceptionJsonString(std::string& out, std::string_view value) {
  out.push_back('"');
  for(const char ch : value) {
    switch(ch) {
      case '\\': out += "\\\\"; break;
      case '"': out += "\\\""; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if(static_cast<unsigned char>(ch) >= 0x20)
          out.push_back(ch);
        break;
    }
  }
  out.push_back('"');
}

std::string buildPerceptionReactionDebugJson(const Mmo::Server::Perception::Event& event,
                                             const Mmo::Server::Perception::WitnessSummary& witnesses,
                                             const Mmo::Server::Perception::ReactionPlan& plan) {
  std::string out;
  out.reserve(640 + event.sourceKey.size() + event.otherKey.size() +
              event.victimKey.size() + event.itemKey.size());
  out += "{\"schema\":\"mmo.server_perception_reaction.v1\"";
  out += ",\"perception_id\":";
  out += std::to_string(event.perceptionId);
  out += ",\"perception\":";
  appendPerceptionJsonString(out, event.def.eventName);
  out += ",\"reaction\":";
  appendPerceptionJsonString(out, Mmo::Server::Perception::reactionKindName(plan.kind));
  out += ",\"reason\":";
  appendPerceptionJsonString(out, plan.reason);
  out += ",\"source_key\":";
  appendPerceptionJsonString(out, event.sourceKey);
  out += ",\"other_key\":";
  appendPerceptionJsonString(out, event.otherKey);
  out += ",\"victim_key\":";
  appendPerceptionJsonString(out, event.victimKey);
  out += ",\"item_key\":";
  appendPerceptionJsonString(out, event.itemKey);
  out += ",\"comparable_witnesses\":";
  out += std::to_string(witnesses.comparable);
  out += ",\"witnessed\":";
  out += std::to_string(witnesses.witnessed);
  out += ",\"script_handler\":";
  out += plan.needsScriptHandler ? "true" : "false";
  out += ",\"interrupt\":";
  out += plan.interruptsCurrentAction ? "true" : "false";
  out += ",\"hostility\":";
  out += plan.changesHostility ? "true" : "false";
  out += "}";
  return out;
}

void queuePerceptionReactionLiveDelta(const Mmo::Server::Perception::Event& event,
                                      const Mmo::Server::Perception::WitnessSummary& witnesses,
                                      const Mmo::Server::Perception::ReactionPlan& plan) {
  if(!plan.needsUdpReplication)
    return;

  Mmo::Net::ServerLiveDeltaPacket delta;
  delta.packetSequence = event.sequence;
  delta.serverTick = event.lastServerTickMs;
  delta.kind = Mmo::Net::ServerLiveDeltaKind::PerceptionReaction;
  delta.actionKind = std::string(event.def.eventName);
  delta.debugJson = buildPerceptionReactionDebugJson(event, witnesses, plan);
  if(event.originPosition) {
    delta.flags |= Mmo::Net::ServerLiveDeltaHasPosition;
    delta.posX = event.originPosition->x;
    delta.posY = event.originPosition->y;
    delta.posZ = event.originPosition->z;
  }

  constexpr std::size_t MaxPendingPerceptionLiveDeltas = 256;
  if(gPerceptionLiveDeltas.size() >= MaxPendingPerceptionLiveDeltas)
    gPerceptionLiveDeltas.pop_front();
  gPerceptionLiveDeltas.emplace_back(std::move(delta));
}

std::vector<Mmo::Net::ServerLiveDeltaPacket> takePerceptionReactionLiveDeltas() {
  std::vector<Mmo::Net::ServerLiveDeltaPacket> out;
  out.reserve(gPerceptionLiveDeltas.size());
  while(!gPerceptionLiveDeltas.empty()) {
    out.emplace_back(std::move(gPerceptionLiveDeltas.front()));
    gPerceptionLiveDeltas.pop_front();
  }
  return out;
}

[[nodiscard]] bool shouldPersistPerceptionReaction(const Mmo::Server::Perception::ReactionPlan& plan) noexcept {
  return plan.needsUdpReplication || plan.needsScriptHandler ||
         plan.interruptsCurrentAction || plan.changesHostility;
}

[[nodiscard]] std::string perceptionReactionTargetKey(const Mmo::Server::Perception::Event& event,
                                                      std::string_view actorKey) {
  if(!event.victimKey.empty() && event.victimKey != actorKey)
    return event.victimKey;
  if(!event.otherKey.empty() && event.otherKey != actorKey)
    return event.otherKey;
  if(!event.itemKey.empty())
    return event.itemKey;
  if(!event.sourceKey.empty() && event.sourceKey != actorKey)
    return event.sourceKey;
  if(!event.victimKey.empty())
    return event.victimKey;
  return event.sourceKey;
}

[[nodiscard]] std::string perceptionReactionIdempotencyKey(const Mmo::Server::Perception::Event& event,
                                                           const Mmo::Server::Perception::ReactionPlan& plan,
                                                           std::string_view actorKey,
                                                           std::string_view idempotencySeed) {
  std::string suffix;
  suffix.reserve(96 + actorKey.size());
  suffix += ":perc:";
  suffix += std::to_string(event.perceptionId);
  suffix += ":reaction:";
  suffix += Mmo::Server::Perception::reactionKindName(plan.kind);
  suffix += ":actor:";
  suffix += actorKey;

  std::string base;
  if(!idempotencySeed.empty())
    base = std::string(idempotencySeed);
  else {
    base = "tick:";
    base += std::to_string(event.lastServerTickMs);
    base += ":seq:";
    base += std::to_string(event.sequence);
  }

  constexpr std::size_t MaxIdempotencyKey = 255;
  constexpr std::string_view Prefix = "perception_reaction:";
  const std::size_t baseLimit = MaxIdempotencyKey > Prefix.size() + suffix.size() ?
    MaxIdempotencyKey - Prefix.size() - suffix.size() :
    0;
  if(base.size() > baseLimit)
    base.resize(baseLimit);

  std::string out(Prefix.data(), Prefix.size());
  out += base;
  out += suffix;
  if(out.size() > MaxIdempotencyKey)
    out.resize(MaxIdempotencyKey);
  return out;
}

[[nodiscard]] std::string perceptionReactionOutboxIdempotencyKey(const Mmo::Server::Perception::Event& event,
                                                                 const Mmo::Server::Perception::ReactionPlan& plan,
                                                                 std::string_view actorKey,
                                                                 std::string_view idempotencySeed) {
  std::string suffix;
  suffix.reserve(96 + actorKey.size());
  suffix += ":perc:";
  suffix += std::to_string(event.perceptionId);
  suffix += ":reaction:";
  suffix += Mmo::Server::Perception::reactionKindName(plan.kind);
  suffix += ":actor:";
  suffix += actorKey;

  std::string base;
  if(!idempotencySeed.empty())
    base = std::string(idempotencySeed);
  else {
    base = "tick:";
    base += std::to_string(event.lastServerTickMs);
    base += ":seq:";
    base += std::to_string(event.sequence);
  }

  constexpr std::size_t MaxIdempotencyKey = 191;
  constexpr std::string_view Prefix = "npc_action:";
  const std::size_t baseLimit = MaxIdempotencyKey > Prefix.size() + suffix.size() ?
    MaxIdempotencyKey - Prefix.size() - suffix.size() :
    0;
  if(base.size() > baseLimit)
    base.resize(baseLimit);

  std::string out(Prefix.data(), Prefix.size());
  out += base;
  out += suffix;
  if(out.size() > MaxIdempotencyKey)
    out.resize(MaxIdempotencyKey);
  return out;
}

[[nodiscard]] bool shouldEnqueuePerceptionReactionAction(const Mmo::Server::Perception::ReactionPlan& plan) noexcept {
  return plan.needsScriptHandler || plan.interruptsCurrentAction || plan.changesHostility;
}

[[nodiscard]] std::string_view perceptionReactionActionKey(Mmo::Server::Perception::ReactionKind kind) noexcept {
  switch(kind) {
    case Mmo::Server::Perception::ReactionKind::QueueScript: return "queue_script";
    case Mmo::Server::Perception::ReactionKind::Interrupt: return "interrupt";
    case Mmo::Server::Perception::ReactionKind::Warn: return "warn";
    case Mmo::Server::Perception::ReactionKind::SuspectCrime: return "suspect_crime";
    case Mmo::Server::Perception::ReactionKind::CallHelp: return "call_help";
    case Mmo::Server::Perception::ReactionKind::StartCombat: return "start_combat";
    case Mmo::Server::Perception::ReactionKind::Observe:
    case Mmo::Server::Perception::ReactionKind::Ignore:
      return "observe";
  }
  return "unknown";
}

[[nodiscard]] int perceptionReactionActionPriority(const Mmo::Server::Perception::ReactionPlan& plan) noexcept {
  if(plan.changesHostility)
    return 20;
  if(plan.interruptsCurrentAction)
    return 30;
  if(plan.needsScriptHandler)
    return 50;
  return 80;
}

[[nodiscard]] std::string perceptionReactionOutboxTargetKey(std::string_view actorKey) {
  constexpr std::size_t MaxOutboxTargetKey = 191;
  if(actorKey.size() <= MaxOutboxTargetKey)
    return std::string(actorKey);
  return std::string(actorKey.substr(0, MaxOutboxTargetKey));
}

[[nodiscard]] std::string buildPerceptionReactionNpcActionPayload(
    const Mmo::Server::Perception::Event& event,
    const Mmo::Server::Perception::WitnessSummary& witnesses,
    const Mmo::Server::Perception::ReactionPlan& plan,
    std::string_view actorKey,
    std::string_view targetKey) {
  std::string out;
  out.reserve(900 + actorKey.size() + targetKey.size() + event.sourceKey.size() +
              event.victimKey.size() + event.itemKey.size());
  out += "{\"schema\":\"mmo.npc_action_request.v1\"";
  out += ",\"actor_key\":";
  appendPerceptionJsonString(out, actorKey);
  out += ",\"target_key\":";
  appendPerceptionJsonString(out, targetKey);
  out += ",\"action_target_key\":";
  appendPerceptionJsonString(out, targetKey);
  out += ",\"action_key\":";
  appendPerceptionJsonString(out, perceptionReactionActionKey(plan.kind));
  out += ",\"action_state\":\"queued\"";
  out += ",\"sync_group\":";
  appendPerceptionJsonString(out, actorKey);
  out += ",\"server_tick\":";
  out += std::to_string(event.lastServerTickMs);
  out += ",\"perception_id\":";
  out += std::to_string(event.perceptionId);
  out += ",\"perception\":";
  appendPerceptionJsonString(out, event.def.eventName);
  out += ",\"reaction\":";
  appendPerceptionJsonString(out, Mmo::Server::Perception::reactionKindName(plan.kind));
  out += ",\"reason\":";
  appendPerceptionJsonString(out, plan.reason);
  out += ",\"source_key\":";
  appendPerceptionJsonString(out, event.sourceKey);
  out += ",\"other_key\":";
  appendPerceptionJsonString(out, event.otherKey);
  out += ",\"victim_key\":";
  appendPerceptionJsonString(out, event.victimKey);
  out += ",\"item_key\":";
  appendPerceptionJsonString(out, event.itemKey);
  out += ",\"comparable_witnesses\":";
  out += std::to_string(witnesses.comparable);
  out += ",\"witnessed\":";
  out += std::to_string(witnesses.witnessed);
  out += ",\"script_handler\":";
  out += plan.needsScriptHandler ? "true" : "false";
  out += ",\"interrupt\":";
  out += plan.interruptsCurrentAction ? "true" : "false";
  out += ",\"hostility\":";
  out += plan.changesHostility ? "true" : "false";
  out += "}";
  return out;
}

void enqueuePerceptionReactionNpcAction(const MySqlTarget& target,
                                        std::string_view sessionUuid,
                                        const Mmo::Server::Perception::Event& event,
                                        const Mmo::Server::Perception::WitnessSummary& witnesses,
                                        const Mmo::Server::Perception::ReactionPlan& plan,
                                        std::string_view actorKey,
                                        std::string_view idempotencySeed) {
  if(actorKey.empty() || !shouldEnqueuePerceptionReactionAction(plan))
    return;

  const auto targetKey = perceptionReactionTargetKey(event, actorKey);
  const auto outboxTargetKey = perceptionReactionOutboxTargetKey(actorKey);
  const auto payload = buildPerceptionReactionNpcActionPayload(event, witnesses, plan, actorKey, targetKey);
  const auto idempotencyKey = perceptionReactionOutboxIdempotencyKey(event, plan, actorKey, idempotencySeed);
  const Mmo::Server::OutboxActionRecord record {
    .sessionUuid = sessionUuid,
    .actionName = "npc_action_request",
    .targetKey = outboxTargetKey,
    .dbPayload = payload,
    .idempotencyKey = idempotencyKey,
    .priority = perceptionReactionActionPriority(plan),
    .maxAttempts = 5,
  };
  Mmo::Server::enqueueOutboxAction(target, record);
  (void)applyNpcActivityCommand(actorKey,
                                perceptionReactionActionKey(plan.kind),
                                "queued",
                                targetKey,
                                actorKey,
                                event.lastServerTickMs,
                                0,
                                "perception_reaction_activity_rejected");
  std::cerr << "[perception_reaction_action_enqueued]"
            << " actor=" << actorKey
            << " target=" << targetKey
            << " action=" << perceptionReactionActionKey(plan.kind)
            << " perc=" << event.def.eventName
            << "\n";
}

void persistPerceptionReactionStarted(const MySqlTarget& target,
                                      std::string_view sessionUuid,
                                      const Mmo::Server::Perception::Event& event,
                                      const Mmo::Server::Perception::WitnessSummary& witnesses,
                                      const Mmo::Server::Perception::ReactionPlan& plan,
                                      std::string_view actorKey,
                                      std::string_view idempotencySeed) {
  if(actorKey.empty())
    return;

  const auto targetKey = perceptionReactionTargetKey(event, actorKey);
  const auto metadata = buildPerceptionReactionDebugJson(event, witnesses, plan);
  const auto idempotencyKey = perceptionReactionIdempotencyKey(event, plan, actorKey, idempotencySeed);

  std::string sql;
  sql += "SET @mmo_reaction_event_id=NULL; ";
  sql += "CALL mmo_record_npc_reaction_started(UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1),";
  sql += sqlLiteral(actorKey);
  sql += ",";
  sql += sqlLiteral(targetKey);
  sql += ",";
  sql += sqlLiteral(Mmo::Server::Perception::reactionKindName(plan.kind));
  sql += ",";
  sql += std::to_string(event.lastServerTickMs);
  sql += ",";
  sql += sqlJson(metadata);
  sql += ",";
  sql += sqlLiteral(idempotencyKey);
  sql += ",@mmo_reaction_event_id);";
  (void)runMysql(target, sql);
}

void recordPerceptionReactionStarted(const MySqlTarget* target,
                                     std::string_view sessionUuid,
                                     const Mmo::Server::Perception::Event& event,
                                     const Mmo::Server::Perception::WitnessSummary& witnesses,
                                     const Mmo::Server::Perception::ReactionPlan& plan,
                                     std::string_view idempotencySeed) {
  if(target == nullptr || sessionUuid.empty() || !shouldPersistPerceptionReaction(plan))
    return;

  try {
    bool persistedWitness = false;
    for(const auto& result : witnesses.results) {
      if(!result.decision.witnessed)
        continue;
      persistPerceptionReactionStarted(*target, sessionUuid, event, witnesses, plan,
                                       result.witnessKey, idempotencySeed);
      enqueuePerceptionReactionNpcAction(*target, sessionUuid, event, witnesses, plan,
                                         result.witnessKey, idempotencySeed);
      persistedWitness = true;
    }

    if(!persistedWitness && !event.def.needsWitness) {
      const std::string_view actorKey = event.def.needsVictim && !event.victimKey.empty() ?
        std::string_view(event.victimKey) :
        std::string_view(event.sourceKey);
      persistPerceptionReactionStarted(*target, sessionUuid, event, witnesses, plan,
                                       actorKey, idempotencySeed);
      enqueuePerceptionReactionNpcAction(*target, sessionUuid, event, witnesses, plan,
                                         actorKey, idempotencySeed);
    }
  } catch(const std::exception& error) {
    std::cerr << "[perception_reaction_side_effect_failed]"
              << " perc=" << event.def.eventName
              << " reaction=" << Mmo::Server::Perception::reactionKindName(plan.kind)
              << " source=" << event.sourceKey
              << " victim=" << event.victimKey
              << " reason=" << error.what()
              << "\n";
  }
}

void recordPerceptionEvent(const Mmo::Server::Perception::EventInput& input,
                           const MySqlTarget* target = nullptr,
                           std::string_view sessionUuid = {},
                           std::string_view idempotencySeed = {}) {
  const auto result = gPerceptionQueue.enqueue(input);
  if(!result.accepted) {
    std::cerr << "[perception_rejected]"
              << " perc=" << int(input.perceptionId)
              << " source=" << input.sourceKey
              << " other=" << input.otherKey
              << " victim=" << input.victimKey
              << " reason=" << result.reason
              << "\n";
    return;
  }
  if(result.coalesced)
    return;

  const auto def = Mmo::Server::Perception::byId(input.perceptionId);
  if(!def)
    return;
  std::cerr << "[perception_queued]"
            << " seq=" << result.sequence
            << " perc=" << def->eventName
            << " domain=" << Mmo::Server::Perception::domainName(def->domain)
            << " need=" << Mmo::Server::Perception::serverNeedName(def->need)
            << " source=" << input.sourceKey
            << " other=" << input.otherKey
            << " victim=" << input.victimKey
            << " item=" << input.itemKey
            << " has_pos=" << (input.originPosition.has_value() ? 1 : 0)
            << " reason=" << input.reason
            << "\n";

  gPerceptionWitnessRegistry.expire(input.serverTickMs);
  const auto event = gPerceptionQueue.latest();
  if(!event)
    return;
  const auto witnesses = gPerceptionWitnessRegistry.evaluate(*event, input.serverTickMs);
  std::cerr << "[perception_witnessed]"
            << " seq=" << result.sequence
            << " perc=" << def->eventName
            << " comparable=" << witnesses.comparable
            << " witnessed=" << witnesses.witnessed
            << "\n";
  const auto plan = Mmo::Server::Perception::planReaction(*event, witnesses);
  std::cerr << "[perception_reaction_planned]"
            << " seq=" << result.sequence
            << " perc=" << def->eventName
            << " reaction=" << Mmo::Server::Perception::reactionKindName(plan.kind)
            << " udp_replication=" << (plan.needsUdpReplication ? 1 : 0)
            << " script_handler=" << (plan.needsScriptHandler ? 1 : 0)
            << " interrupt=" << (plan.interruptsCurrentAction ? 1 : 0)
            << " hostility=" << (plan.changesHostility ? 1 : 0)
            << " reason=" << plan.reason
            << "\n";
  recordPerceptionReactionStarted(target, sessionUuid, *event, witnesses, plan, idempotencySeed);
  queuePerceptionReactionLiveDelta(*event, witnesses, plan);
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










