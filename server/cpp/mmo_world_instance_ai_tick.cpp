#include "mmo_world_instance_ai_tick.h"

#include "mmo_world_instance_content_cache.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace Mmo::WorldInstanceAiTick {
namespace {

[[nodiscard]] WorldInstanceContent::WorldInstanceContentCache loadContentCache(
    const WorldInstanceAiTickOptions& options) {
  if(options.runtimeReadModelPath.empty()) {
    throw std::runtime_error("runtime read-model path is required for world_instance AI tick");
  }
  if(options.contentRevisionKey.empty()) {
    throw std::runtime_error("content_revision_key is required for world_instance AI tick");
  }
  if(options.worldInstanceKey.empty()) {
    throw std::runtime_error("world_instance_key is required for world_instance AI tick");
  }
  if(options.worldName.empty()) {
    throw std::runtime_error("world_name is required for world_instance AI tick");
  }

  WorldInstanceContent::WorldInstanceContentCacheOptions cacheOptions;
  cacheOptions.runtimeReadModelPath = options.runtimeReadModelPath;
  cacheOptions.contentRevisionKey = options.contentRevisionKey;
  cacheOptions.worldInstanceKey = options.worldInstanceKey;
  cacheOptions.worldName = options.worldName;
  return WorldInstanceContent::WorldInstanceContentCache::load(cacheOptions);
}

[[nodiscard]] NpcPerceptionRuntime::RuntimeActorSnapshot loadGuardedActors(
    const Server::MySqlTarget& target,
    const WorldInstanceAiTickOptions& options) {
  NpcPerceptionRuntime::RuntimeActorQueryOptions actorOptions;
  actorOptions.worldInstanceKey = options.worldInstanceKey;
  actorOptions.worldName = options.worldName;
  actorOptions.maxNpcs = options.maxNpcs;
  actorOptions.maxPlayers = options.maxPlayers;
  actorOptions.repairWeakNpcEntityKeys = options.repairWeakNpcEntityKeys;
  actorOptions.includeWeakNpcIdentity = options.includeWeakNpcIdentity;
  return NpcPerceptionRuntime::loadRuntimeActors(target, actorOptions);
}

[[nodiscard]] NpcPerception::AssessmentOptions makeAssessmentOptions(
    const WorldInstanceAiTickOptions& options,
    std::uint64_t runtimeServerTick) {
  NpcPerception::AssessmentOptions out;
  out.perceptionKind = options.perceptionKind;
  out.maxDistance = options.maxDistance;
  out.serverTick = options.serverTick == 0 ? runtimeServerTick : options.serverTick;
  out.cooldownTicks = options.cooldownTicks;
  out.priorityValue = options.priorityValue;
  out.enqueueAction = options.enqueueAction;
  return out;
}

} // namespace

WorldInstanceAiTickResult runWorldInstanceAiTick(
    const Server::MySqlTarget& target,
    const WorldInstanceAiTickOptions& options) {
  const auto cache = loadContentCache(options);
  const auto actors = loadGuardedActors(target, options);
  const auto assessmentOptions = makeAssessmentOptions(options, actors.world.serverTick);
  const auto assessment = NpcPerception::assessNpcPerception(cache, assessmentOptions, actors.npcs, actors.players);

  WorldInstanceAiTickResult out;
  out.world = actors.world;
  out.npcIdentity = actors.npcIdentity;
  out.assessment = assessment.stats;
  out.decisions = assessment.decisions;
  out.recordLimit = options.maxRecords;
  out.dryRun = options.dryRun;

  const std::size_t writableDecisionCount = std::min(options.maxRecords, out.decisions.size());
  out.skippedByRecordLimit = out.decisions.size() - writableDecisionCount;

  if(!options.dryRun) {
    out.recorded.reserve(writableDecisionCount);
    for(std::size_t i = 0; i < writableDecisionCount; ++i) {
      AiRuntime::RecordNpcPerceptionDecisionOptions recordOptions;
      recordOptions.aiDatabaseName = options.aiDatabaseName;
      recordOptions.worldInstanceUuid = actors.world.worldInstanceUuid;
      recordOptions.sessionUuid = out.decisions[i].sessionUuid;
      recordOptions.characterUuid = out.decisions[i].characterUuid;
      out.recorded.push_back({
          out.decisions[i],
          AiRuntime::recordNpcPerceptionDecision(target, recordOptions, out.decisions[i]),
      });
    }
  }

  return out;
}

} // namespace Mmo::WorldInstanceAiTick
