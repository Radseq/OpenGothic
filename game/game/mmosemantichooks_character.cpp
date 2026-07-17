#include "mmosemantichooks_internal.h"

#include <array>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "mmoclientadapter.h"
#include "mmoclientbridge.h"
#include "world/world.h"
#include "world/objects/interactive.h"
#include "world/objects/item.h"
#include "world/objects/npc.h"
#include "world/waypoint.h"
#include "commandline.h"
#include "utils/versioninfo.h"

namespace Mmo::Hooks::Detail {


void onClientBootstrapRequest(World& world,
                              const char* sourceLocation,
                              const char* reason) noexcept {
  if(!isServerBoundClientModeEnabled())
    return;

  const auto target = playerOrDefaultKey(world);
  const ClientBootstrapRequest request{
      .clientTick = world.tickCount(),
      .targetKey = target,
      .source = sourceLocation != nullptr ? std::string_view(sourceLocation)
                                          : std::string_view("unknown"),
      .actorKey = target,
      .characterKey = characterKey(),
      .displayName = CommandLine::inst().mmoCharacterDisplayName(),
      .world = world.name(),
      .serverEndpoint = CommandLine::inst().mmoServerEndpoint(),
      .clientContentManifestHash =
          CommandLine::inst().mmoClientContentManifestHash(),
      .reason = reason != nullptr ? std::string_view(reason)
                                  : std::string_view("client_bootstrap_request"),
  };
  (void)submitClientBootstrap(request);

  if(isClientMmoDiagnosticsEnabled()) {
    std::string payload = "{\"typed_intent\":true,\"character_key\":";
    payload.append(jsonEscape(characterKey()));
    payload.append(",\"world\":");
    payload.append(jsonEscape(world.name()));
    payload.push_back('}');
    submit(SemanticActionKind::ClientBootstrapRequest, target,
           std::move(payload), world.tickCount());
  }
}

void onCharacterMovementProposal(Npc& actor,
                                 std::uint64_t fromTick,
                                 float fromX,
                                 float fromY,
                                 float fromZ,
                                 float fromYaw,
                                 std::int32_t fromHealthCurrent,
                                 std::int32_t fromHealthMax,
                                 std::int32_t fromManaCurrent,
                                 std::int32_t fromManaMax,
                                 bool fromInAir,
                                 bool fromFalling,
                                 bool fromFallingDeep,
                                 bool fromSlide,
                                 bool fromJump,
                                 bool fromJumpUp,
                                 bool fromSwim,
                                 bool fromDive,
                                 bool fromInWater,
                                 const char* sourceLocation,
                                 const char* reason) noexcept {
  if((!isClientMmoDiagnosticsEnabled() && !isServerBoundClientModeEnabled()) || !shouldCapturePlayerAction(actor))
    return;

  auto& world = actor.world();
  auto target = characterTargetKey("movement-proposal");
  const auto pos = actor.position();
  const auto* wp = actor.currentWayPoint();
  const auto& cmd = CommandLine::inst();
  const std::uint64_t toTick = world.tickCount();
  const std::uint64_t deltaTick = toTick >= fromTick ? toTick - fromTick : 0;

  if(isServerBoundClientModeEnabled()) {
    const auto actorIdentity = actorKey(actor);
    const auto characterIdentity = characterKey();
    ClientMovementIntent intent;
    intent.from = {
        .tick = fromTick,
        .x = fromX,
        .y = fromY,
        .z = fromZ,
        .yaw = fromYaw,
        .state = movementState(fromInAir, fromFalling, fromFallingDeep,
                               fromSlide, fromJump, fromJumpUp, fromSwim,
                               fromDive, fromInWater),
    };
    intent.to = {
        .tick = toTick,
        .x = pos.x,
        .y = pos.y,
        .z = pos.z,
        .yaw = actor.rotationY(),
        .state = movementState(actor.isInAir(), actor.isFalling(),
                               actor.isFallingDeep(), actor.isSlide(),
                               actor.isJump(), actor.isJumpUp(), actor.isSwim(),
                               actor.isDive(), actor.isInWater()),
    };
    intent.cadence = {
        .intervalMs = cmd.mmoActionMovementProposalIntervalMs(),
        .minimumDistance = cmd.mmoActionMovementProposalMinDistance(),
        .minimumYawDegrees = cmd.mmoActionMovementProposalMinYawDeg(),
    };
    intent.targetKey = target;
    intent.source = sourceLocation != nullptr ? std::string_view(sourceLocation)
                                              : std::string_view("unknown");
    intent.actorKey = actorIdentity;
    intent.characterKey = characterIdentity;
    intent.world = world.name();
    intent.waypointKey = wp != nullptr ? std::string_view(wp->name)
                                       : std::string_view{};
    intent.reason = reason != nullptr ? std::string_view(reason)
                                      : std::string_view("movement_delta_proposal");
    (void)submitClientMovement(intent);
  }
  if(!isClientMmoDiagnosticsEnabled())
    return;

  std::string payload;
  payload.reserve(1536);
  payload.append("{\"source\":"); appendEscaped(payload, sourceLocation);
  payload.append(",\"actor_key\":"); appendEscaped(payload, actorKey(actor));
  payload.append(",\"character_key\":"); appendEscaped(payload, characterKey());
  payload.append(",\"target_key\":"); appendEscaped(payload, target);
  payload.append(",\"proposal_version\":1");
  payload.append(",\"input_model\":\"checkpoint_delta_v1\"");
  payload.append(",\"movement_intent\":\"delta_transform\"");
  payload.append(",\"from_tick\":"); appendUInt(payload, fromTick);
  payload.append(",\"to_tick\":"); appendUInt(payload, toTick);
  payload.append(",\"delta_ms\":"); appendUInt(payload, deltaTick);
  payload.append(",\"from_pos_x\":"); appendFloat(payload, fromX);
  payload.append(",\"from_pos_y\":"); appendFloat(payload, fromY);
  payload.append(",\"from_pos_z\":"); appendFloat(payload, fromZ);
  payload.append(",\"to_pos_x\":"); appendFloat(payload, pos.x);
  payload.append(",\"to_pos_y\":"); appendFloat(payload, pos.y);
  payload.append(",\"to_pos_z\":"); appendFloat(payload, pos.z);
  payload.append(",\"from_rotation_yaw\":"); appendFloat(payload, fromYaw);
  payload.append(",\"to_rotation_yaw\":"); appendFloat(payload, actor.rotationY());
  payload.append(",\"vertical_axis\":\"y\"");
  payload.append(",\"from_health_current\":"); appendInt(payload, fromHealthCurrent);
  payload.append(",\"from_health_max\":"); appendInt(payload, fromHealthMax);
  payload.append(",\"from_mana_current\":"); appendInt(payload, fromManaCurrent);
  payload.append(",\"from_mana_max\":"); appendInt(payload, fromManaMax);
  payload.append(",\"from_is_in_air\":"); appendBool(payload, fromInAir);
  payload.append(",\"from_is_falling\":"); appendBool(payload, fromFalling);
  payload.append(",\"from_is_falling_deep\":"); appendBool(payload, fromFallingDeep);
  payload.append(",\"from_is_slide\":"); appendBool(payload, fromSlide);
  payload.append(",\"from_is_jump\":"); appendBool(payload, fromJump);
  payload.append(",\"from_is_jump_up\":"); appendBool(payload, fromJumpUp);
  payload.append(",\"from_is_swim\":"); appendBool(payload, fromSwim);
  payload.append(",\"from_is_dive\":"); appendBool(payload, fromDive);
  payload.append(",\"from_is_in_water\":"); appendBool(payload, fromInWater);
  payload.append(",\"to_is_in_air\":"); appendBool(payload, actor.isInAir());
  payload.append(",\"to_is_falling\":"); appendBool(payload, actor.isFalling());
  payload.append(",\"to_is_falling_deep\":"); appendBool(payload, actor.isFallingDeep());
  payload.append(",\"to_is_slide\":"); appendBool(payload, actor.isSlide());
  payload.append(",\"to_is_jump\":"); appendBool(payload, actor.isJump());
  payload.append(",\"to_is_jump_up\":"); appendBool(payload, actor.isJumpUp());
  payload.append(",\"to_is_swim\":"); appendBool(payload, actor.isSwim());
  payload.append(",\"to_is_dive\":"); appendBool(payload, actor.isDive());
  payload.append(",\"to_is_in_water\":"); appendBool(payload, actor.isInWater());
  payload.append(",\"current_waypoint_key\":"); appendEscaped(payload, wp != nullptr ? std::string_view(wp->name) : std::string_view{});
  payload.append(",\"level\":"); appendInt(payload, actor.level());
  payload.append(",\"experience\":"); appendInt(payload, actor.experience());
  payload.append(",\"experience_next\":"); appendInt(payload, actor.experienceNext());
  payload.append(",\"learning_points\":"); appendInt(payload, actor.learningPoints());
  payload.append(",\"health_current\":"); appendInt(payload, actor.attribute(ATR_HITPOINTS));
  payload.append(",\"health_max\":"); appendInt(payload, actor.attribute(ATR_HITPOINTSMAX));
  payload.append(",\"mana_current\":"); appendInt(payload, actor.attribute(ATR_MANA));
  payload.append(",\"mana_max\":"); appendInt(payload, actor.attribute(ATR_MANAMAX));
  payload.append(",\"strength\":"); appendInt(payload, actor.attribute(ATR_STRENGTH));
  payload.append(",\"dexterity\":"); appendInt(payload, actor.attribute(ATR_DEXTERITY));
  payload.append(",\"guild\":"); appendInt(payload, actor.guild());
  payload.append(",\"true_guild\":"); appendInt(payload, actor.trueGuild());
  payload.append(",\"permanent_attitude\":"); appendInt(payload, static_cast<std::int32_t>(actor.attitude()));
  payload.append(",\"temporary_attitude\":"); appendInt(payload, static_cast<std::int32_t>(actor.tempAttitude()));
  payload.append(",\"reason\":"); appendEscaped(payload, reason != nullptr ? std::string_view(reason) : std::string_view("movement_delta_proposal"));
  payload.append(",\"proposal_interval_ms\":"); appendUInt(payload, cmd.mmoActionMovementProposalIntervalMs());
  payload.append(",\"proposal_min_distance\":"); appendFloat(payload, cmd.mmoActionMovementProposalMinDistance());
  payload.append(",\"proposal_min_yaw_deg\":"); appendFloat(payload, cmd.mmoActionMovementProposalMinYawDeg());
  appendWorld(payload, world);
  appendVec3(payload, "actor_position", pos);
  payload.push_back('}');

  if(isClientMmoDiagnosticsEnabled())
    submit(SemanticActionKind::MovementProposal, std::move(target), std::move(payload), world.tickCount());
}

void onCharacterCheckpoint(Npc& actor,
                           const char* sourceLocation,
                           const char* reason) noexcept {
  if((!isClientMmoDiagnosticsEnabled() && !isServerBoundClientModeEnabled()) || !shouldCapturePlayerAction(actor))
    return;

  auto& world = actor.world();
  const auto& cmd = CommandLine::inst();
  auto target = characterTargetKey("checkpoint");
  const auto pos = actor.position();
  const auto* wp = actor.currentWayPoint();

  if(isServerBoundClientModeEnabled()) {
    const auto actorIdentity = actorKey(actor);
    ClientMovementIntent intent;
    intent.kind = ClientMovementRequestKind::CharacterCheckpoint;
    intent.from = {
        .tick = world.tickCount(),
        .x = pos.x,
        .y = pos.y,
        .z = pos.z,
        .yaw = actor.rotationY(),
    };
    intent.to = intent.from;
    intent.cadence = {
        .intervalMs = cmd.mmoActionCheckpointIntervalMs(),
        .minimumDistance = cmd.mmoActionCheckpointMinDistance(),
        .minimumYawDegrees = cmd.mmoActionCheckpointMinYawDeg(),
    };
    intent.checkpointForceIntervalMs =
        cmd.mmoActionCheckpointForceIntervalMs();
    intent.targetKey = target;
    intent.source = sourceLocation != nullptr ? std::string_view(sourceLocation)
                                              : std::string_view("unknown");
    intent.actorKey = actorIdentity;
    intent.characterKey = characterKey();
    intent.world = world.name();
    intent.waypointKey = wp != nullptr ? std::string_view(wp->name)
                                       : std::string_view{};
    intent.reason = reason != nullptr ? std::string_view(reason)
                                      : std::string_view("periodic_checkpoint");
    (void)submitClientMovement(intent);
  }
  if(!isClientMmoDiagnosticsEnabled())
    return;

  std::string payload;
  payload.reserve(1152);
  payload.append("{\"source\":"); appendEscaped(payload, sourceLocation);
  payload.append(",\"actor_key\":"); appendEscaped(payload, actorKey(actor));
  payload.append(",\"character_key\":"); appendEscaped(payload, characterKey());
  payload.append(",\"target_key\":"); appendEscaped(payload, target);
  payload.append(",\"pos_x\":"); appendFloat(payload, pos.x);
  payload.append(",\"pos_y\":"); appendFloat(payload, pos.y);
  payload.append(",\"pos_z\":"); appendFloat(payload, pos.z);
  payload.append(",\"rotation_yaw\":"); appendFloat(payload, actor.rotationY());
  payload.append(",\"current_waypoint_key\":"); appendEscaped(payload, wp != nullptr ? std::string_view(wp->name) : std::string_view{});
  payload.append(",\"level\":"); appendInt(payload, actor.level());
  payload.append(",\"experience\":"); appendInt(payload, actor.experience());
  payload.append(",\"experience_next\":"); appendInt(payload, actor.experienceNext());
  payload.append(",\"learning_points\":"); appendInt(payload, actor.learningPoints());
  payload.append(",\"health_current\":"); appendInt(payload, actor.attribute(ATR_HITPOINTS));
  payload.append(",\"health_max\":"); appendInt(payload, actor.attribute(ATR_HITPOINTSMAX));
  payload.append(",\"mana_current\":"); appendInt(payload, actor.attribute(ATR_MANA));
  payload.append(",\"mana_max\":"); appendInt(payload, actor.attribute(ATR_MANAMAX));
  payload.append(",\"strength\":"); appendInt(payload, actor.attribute(ATR_STRENGTH));
  payload.append(",\"dexterity\":"); appendInt(payload, actor.attribute(ATR_DEXTERITY));
  payload.append(",\"guild\":"); appendInt(payload, actor.guild());
  payload.append(",\"true_guild\":"); appendInt(payload, actor.trueGuild());
  payload.append(",\"permanent_attitude\":"); appendInt(payload, static_cast<std::int32_t>(actor.attitude()));
  payload.append(",\"temporary_attitude\":"); appendInt(payload, static_cast<std::int32_t>(actor.tempAttitude()));
  payload.append(",\"reason\":"); appendEscaped(payload, reason != nullptr ? std::string_view(reason) : std::string_view("step39_periodic_movement_checkpoint"));
  payload.append(",\"checkpoint_interval_ms\":"); appendUInt(payload, cmd.mmoActionCheckpointIntervalMs());
  payload.append(",\"checkpoint_min_distance\":"); appendFloat(payload, cmd.mmoActionCheckpointMinDistance());
  payload.append(",\"checkpoint_min_yaw_deg\":"); appendFloat(payload, cmd.mmoActionCheckpointMinYawDeg());
  payload.append(",\"checkpoint_force_interval_ms\":"); appendUInt(payload, cmd.mmoActionCheckpointForceIntervalMs());
  appendWorld(payload, world);
  appendVec3(payload, "actor_position", pos);
  payload.push_back('}');

  if(isClientMmoDiagnosticsEnabled())
    submit(SemanticActionKind::CharacterCheckpoint, std::move(target), std::move(payload), world.tickCount());
}

void onSaveCheckpointManifest(World& world,
                              std::string_view slotPath,
                              std::string_view displayName,
                              const char* sourceLocation,
                              const char* reason) noexcept {
  if(!isClientMmoDiagnosticsEnabled() || isCaptureSuppressed())
    return;
  if(!isLiveWorldTick(world))
    return;

  const std::string target = characterTargetKey("save-checkpoint");

  std::string payload;
  payload.reserve(1280);
  payload.append("{\"source\":"); appendEscaped(payload, sourceLocation != nullptr ? std::string_view(sourceLocation) : std::string_view("unknown"));
  payload.append(",\"actor_key\":"); appendEscaped(payload, characterEntityKey());
  payload.append(",\"character_key\":"); appendEscaped(payload, characterKey());
  payload.append(",\"target_key\":"); appendEscaped(payload, target);
  payload.append(",\"manifest_key\":"); appendEscaped(payload, target);
  payload.append(",\"checkpoint_kind\":\"native_save\"");
  payload.append(",\"reason\":"); appendEscaped(payload, reason != nullptr ? std::string_view(reason) : std::string_view("native_save_checkpoint_manifest"));
  payload.append(",\"save_slot_key\":"); appendEscaped(payload, slotPath.empty() ? std::string_view("native-save:unknown") : slotPath);
  payload.append(",\"slot_path\":"); appendEscaped(payload, slotPath);
  payload.append(",\"native_save_path\":"); appendEscaped(payload, slotPath);
  payload.append(",\"slot_display_name\":"); appendEscaped(payload, displayName);
  payload.append(",\"display_name\":"); appendEscaped(payload, displayName.empty() ? slotPath : displayName);
  payload.append(",\"client_world_name\":"); appendEscaped(payload, world.name());
  payload.append(",\"native_save_present\":true");
  payload.append(",\"db_save_snapshot_requested\":true");
  appendWorld(payload, world);
  payload.push_back('}');

  submit(SemanticActionKind::SaveCheckpointManifest, std::move(target), std::move(payload), world.tickCount());
}

void onCharacterProgressionChanged(Npc& actor,
                                   std::uint32_t scriptFunctionSymbol,
                                   std::string_view scriptFunctionName,
                                   std::int32_t levelBefore,
                                   std::int32_t levelAfter,
                                   std::int32_t experienceBefore,
                                   std::int32_t experienceAfter,
                                   std::int32_t experienceNextBefore,
                                   std::int32_t experienceNextAfter,
                                   std::int32_t learningPointsBefore,
                                   std::int32_t learningPointsAfter,
                                   const char* sourceLocation) noexcept {
  if(!isClientMmoDiagnosticsEnabled() || !shouldCapturePlayerAction(actor))
    return;
  const auto experienceDelta = experienceAfter - experienceBefore;
  const auto learningPointsDelta = learningPointsAfter - learningPointsBefore;
  const auto levelDelta = levelAfter - levelBefore;
  if(experienceDelta == 0 && learningPointsDelta == 0 && levelDelta == 0 && experienceNextAfter == experienceNextBefore)
    return;

  auto& world = actor.world();
  std::string target = characterTargetKey("progression");

  std::string payload;
  payload.reserve(768);
  payload.append("{\"source\":"); appendEscaped(payload, sourceLocation);
  payload.append(",\"actor_key\":"); appendEscaped(payload, actorKey(actor));
  payload.append(",\"character_key\":"); appendEscaped(payload, characterKey());
  appendScriptContext(payload, scriptFunctionSymbol, scriptFunctionName);
  payload.append(",\"level_before\":"); appendInt(payload, static_cast<std::int64_t>(levelBefore));
  payload.append(",\"level_after\":"); appendInt(payload, static_cast<std::int64_t>(levelAfter));
  payload.append(",\"level_delta\":"); appendInt(payload, static_cast<std::int64_t>(levelDelta));
  payload.append(",\"experience_before\":"); appendInt(payload, static_cast<std::int64_t>(experienceBefore));
  payload.append(",\"experience_after\":"); appendInt(payload, static_cast<std::int64_t>(experienceAfter));
  payload.append(",\"experience_delta\":"); appendInt(payload, static_cast<std::int64_t>(experienceDelta));
  payload.append(",\"experience_next_before\":"); appendInt(payload, static_cast<std::int64_t>(experienceNextBefore));
  payload.append(",\"experience_next_after\":"); appendInt(payload, static_cast<std::int64_t>(experienceNextAfter));
  payload.append(",\"learning_points_before\":"); appendInt(payload, static_cast<std::int64_t>(learningPointsBefore));
  payload.append(",\"learning_points_after\":"); appendInt(payload, static_cast<std::int64_t>(learningPointsAfter));
  payload.append(",\"learning_points_delta\":"); appendInt(payload, static_cast<std::int64_t>(learningPointsDelta));
  payload.append(",\"reason\":\"script_progression\"");
  appendWorld(payload, world);
  appendVec3(payload, "actor_position", actor.position());
  payload.push_back('}');

  submit(SemanticActionKind::AdjustProgression, std::move(target), std::move(payload), world.tickCount());
}

} // namespace Mmo::Hooks::Detail
