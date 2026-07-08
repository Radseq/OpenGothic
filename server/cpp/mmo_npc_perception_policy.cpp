#include "mmo_npc_perception_policy.h"

#include <cmath>
#include <sstream>
#include <utility>

namespace Mmo::NpcPerception {
namespace {

[[nodiscard]] std::string makeIdempotencyKey(
    const WorldInstanceContent::WorldInstanceContentCache& cache,
    const AssessmentOptions& options,
    const NpcActor& npc,
    const PlayerActor& player) {
  std::string out;
  out.reserve(
      cache.contentRevisionKey().size() + cache.worldInstanceKey().size() + npc.entityKey.size() +
      player.targetKey.size() + options.perceptionKind.size() + 64);
  out += "npc_perception:";
  out += cache.contentRevisionKey();
  out.push_back(':');
  out += cache.worldInstanceKey();
  out.push_back(':');
  out += std::to_string(options.serverTick);
  out.push_back(':');
  out += npc.entityKey;
  out.push_back(':');
  out += player.targetKey;
  out.push_back(':');
  out += options.perceptionKind;
  return out;
}

[[nodiscard]] std::string jsonEscape(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 8);
  out.push_back('"');
  for(const char ch : text) {
    switch(ch) {
      case '\\':
        out += "\\\\";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if(static_cast<unsigned char>(ch) < 0x20U) {
          out.push_back(' ');
        } else {
          out.push_back(ch);
        }
        break;
    }
  }
  out.push_back('"');
  return out;
}

void appendJsonField(std::string& out, std::string_view key, std::string_view value) {
  out.push_back(',');
  out += jsonEscape(key);
  out.push_back(':');
  out += jsonEscape(value);
}

void appendJsonNumberField(std::string& out, std::string_view key, double value) {
  out.push_back(',');
  out += jsonEscape(key);
  out.push_back(':');
  out += std::to_string(value);
}

void appendJsonU64Field(std::string& out, std::string_view key, std::uint64_t value) {
  out.push_back(',');
  out += jsonEscape(key);
  out.push_back(':');
  out += std::to_string(value);
}

void appendJsonIntField(std::string& out, std::string_view key, int value) {
  out.push_back(',');
  out += jsonEscape(key);
  out.push_back(':');
  out += std::to_string(value);
}

void appendJsonBoolField(std::string& out, std::string_view key, bool value) {
  out.push_back(',');
  out += jsonEscape(key);
  out.push_back(':');
  out += value ? "true" : "false";
}

} // namespace

double distanceSquared(Vec3 lhs, Vec3 rhs) noexcept {
  const double dx = lhs.x - rhs.x;
  const double dy = lhs.y - rhs.y;
  const double dz = lhs.z - rhs.z;
  return dx * dx + dy * dy + dz * dz;
}

std::string decisionKindForPerception(std::string_view perceptionKind) {
  if(perceptionKind == "PERC_ASSESSWARN") {
    return "warn_player";
  }
  if(perceptionKind == "PERC_ASSESSENEMY" || perceptionKind == "PERC_ASSESSFIGHTER") {
    return "attack_player";
  }
  if(perceptionKind == "PERC_ASSESSPLAYER") {
    return "greet_player";
  }
  if(perceptionKind == "PERC_ASSESSBODY" || perceptionKind == "PERC_ASSESSMAGIC" || perceptionKind == "PERC_CANDIDATE") {
    return "assess_player";
  }
  return "noop";
}

AssessmentResult assessNpcPerception(
    const WorldInstanceContent::WorldInstanceContentCache& cache,
    const AssessmentOptions& options,
    const std::vector<NpcActor>& npcs,
    const std::vector<PlayerActor>& players) {
  AssessmentResult result;
  result.stats.npcActors = npcs.size();
  result.stats.playerActors = players.size();

  const auto bindings = cache.perceptionBindingsByKind(options.perceptionKind);
  if(bindings.empty()) {
    result.stats.missingPerceptionBindingPairs = npcs.size() * players.size();
    return result;
  }

  const auto& binding = *bindings.begin();
  const std::string decisionKind = decisionKindForPerception(options.perceptionKind);
  const double maxDistanceSquared = options.maxDistance * options.maxDistance;

  for(const auto& npc : npcs) {
    for(const auto& player : players) {
      ++result.stats.evaluatedPairs;
      if(!npc.active || !player.active || npc.entityKey.empty() || player.targetKey.empty()) {
        ++result.stats.inactivePairsSkipped;
        continue;
      }
      const double distSquared = distanceSquared(npc.position, player.position);
      if(options.maxDistance >= 0.0 && distSquared > maxDistanceSquared) {
        ++result.stats.distancePairsSkipped;
        continue;
      }

      DecisionCandidate decision;
      decision.npcEntityKey = npc.entityKey;
      decision.npcInstance = npc.npcInstance;
      decision.targetKey = player.targetKey;
      decision.characterKey = player.characterKey;
      decision.sessionUuid = player.sessionUuid;
      decision.characterUuid = player.characterUuid;
      decision.contentRevisionKey = cache.contentRevisionKey();
      decision.worldInstanceKey = cache.worldInstanceKey();
      decision.worldName = cache.worldName();
      decision.ruleKey = binding.functionSymbol.empty() ? binding.perceptionKind : binding.functionSymbol;
      decision.perceptionKind = options.perceptionKind;
      decision.functionSymbol = binding.functionSymbol;
      decision.decisionKind = decisionKind;
      decision.idempotencyKey = makeIdempotencyKey(cache, options, npc, player);
      decision.distance = std::sqrt(distSquared);
      decision.serverTick = options.serverTick;
      decision.cooldownTicks = options.cooldownTicks;
      decision.priorityValue = options.priorityValue;
      decision.enqueueAction = options.enqueueAction && decisionKind != "noop" && decisionKind != "ignore_player";
      result.decisions.push_back(std::move(decision));
    }
  }

  result.stats.decisions = result.decisions.size();
  return result;
}

std::string decisionPayloadJson(const DecisionCandidate& decision) {
  std::string out;
  out.reserve(1024);
  out += "{\"source\":\"mmo_npc_perception_policy\"";
  appendJsonField(out, "npc_entity_key", decision.npcEntityKey);
  appendJsonField(out, "npc_instance", decision.npcInstance);
  appendJsonField(out, "target_key", decision.targetKey);
  appendJsonField(out, "character_key", decision.characterKey);
  appendJsonField(out, "session_uuid", decision.sessionUuid);
  appendJsonField(out, "character_uuid", decision.characterUuid);
  appendJsonField(out, "content_revision_key", decision.contentRevisionKey);
  appendJsonField(out, "world_instance_key", decision.worldInstanceKey);
  appendJsonField(out, "world_name", decision.worldName);
  appendJsonField(out, "rule_key", decision.ruleKey);
  appendJsonField(out, "perception_kind", decision.perceptionKind);
  appendJsonField(out, "function_symbol", decision.functionSymbol);
  appendJsonField(out, "decision_kind", decision.decisionKind);
  appendJsonNumberField(out, "distance", decision.distance);
  appendJsonU64Field(out, "server_tick", decision.serverTick);
  appendJsonU64Field(out, "cooldown_ticks", decision.cooldownTicks);
  appendJsonIntField(out, "priority_value", decision.priorityValue);
  appendJsonBoolField(out, "enqueue_action", decision.enqueueAction);
  out.push_back('}');
  return out;
}

} // namespace Mmo::NpcPerception
