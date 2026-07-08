#include "mmo_ai_runtime_persistence.h"
#include "mmo_npc_perception_runtime_source.h"
#include "mmo_server_persistence.h"
#include "mmo_world_instance_content_cache.h"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

struct ProbeOptions final {
  std::string mysqlUrl;
  std::filesystem::path runtimeReadModelPath;
  std::string contentRevisionKey;
  std::string worldInstanceKey;
  std::string worldName;
  std::string aiDatabaseName = "mmo_ai_runtime";
  std::string perceptionKind = "PERC_ASSESSPLAYER";
  double maxDistance = 1500.0;
  std::uint64_t serverTick = 0;
  std::uint64_t cooldownTicks = 250;
  int priorityValue = 100;
  std::size_t maxNpcs = 32;
  std::size_t maxPlayers = 16;
  std::size_t maxRecords = 16;
  bool enqueueAction = true;
  bool dryRun = false;
  bool synthetic = false;
  std::string worldInstanceUuid;
  std::string npcEntityKey = "npc:probe";
  std::string npcInstance;
  std::string targetKey = "PC_HERO";
  std::string characterKey = "PC_HERO";
  double targetDistance = 100.0;
};

struct RecordResult final {
  Mmo::NpcPerception::DecisionCandidate decision;
  Mmo::AiRuntime::RecordedNpcPerceptionDecision recorded;
};

void jsonEscape(std::ostream& out, std::string_view text) {
  out << '"';
  for(const unsigned char c : text) {
    switch(c) {
      case '"':
        out << "\\\"";
        break;
      case '\\':
        out << "\\\\";
        break;
      case '\b':
        out << "\\b";
        break;
      case '\f':
        out << "\\f";
        break;
      case '\n':
        out << "\\n";
        break;
      case '\r':
        out << "\\r";
        break;
      case '\t':
        out << "\\t";
        break;
      default:
        if(c < 0x20U) {
          static constexpr char Hex[] = "0123456789abcdef";
          out << "\\u00" << Hex[(c >> 4U) & 0xFU] << Hex[c & 0xFU];
        } else {
          out << static_cast<char>(c);
        }
        break;
    }
  }
  out << '"';
}

void jsonField(std::ostream& out, std::string_view name, std::string_view value, bool comma = true) {
  jsonEscape(out, name);
  out << ": ";
  jsonEscape(out, value);
  if(comma) {
    out << ",";
  }
  out << "\n";
}

void jsonRawField(std::ostream& out, std::string_view name, std::string_view value, bool comma = true) {
  jsonEscape(out, name);
  out << ": " << value;
  if(comma) {
    out << ",";
  }
  out << "\n";
}

void jsonCountField(std::ostream& out, std::string_view name, std::size_t value, bool comma = true) {
  jsonEscape(out, name);
  out << ": " << value;
  if(comma) {
    out << ",";
  }
  out << "\n";
}

[[nodiscard]] std::optional<double> parseDouble(std::string_view text) {
  std::string owned(text);
  char* end = nullptr;
  const double value = std::strtod(owned.c_str(), &end);
  if(end == nullptr || *end != '\0') {
    return std::nullopt;
  }
  return value;
}

[[nodiscard]] std::optional<std::uint64_t> parseU64(std::string_view text) {
  std::uint64_t value = 0;
  const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
  if(result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

[[nodiscard]] std::optional<std::size_t> parseSize(std::string_view text) {
  std::size_t value = 0;
  const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
  if(result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

[[nodiscard]] bool parseBoolish(std::string_view text, bool fallback) {
  if(text == "1" || text == "true" || text == "yes" || text == "on") {
    return true;
  }
  if(text == "0" || text == "false" || text == "no" || text == "off") {
    return false;
  }
  return fallback;
}

[[nodiscard]] std::string need(int& i, int argc, char** argv, std::string_view flag) {
  if(i + 1 >= argc) {
    throw std::runtime_error(std::string(flag) + " requires value");
  }
  return argv[++i];
}

[[nodiscard]] ProbeOptions parseArgs(int argc, char** argv) {
  if(argc < 6) {
    throw std::runtime_error(
        "usage: " + std::string(argv[0]) +
        " <mysql_url> <runtime_read_model.json> <content_revision_key> <world_instance_key> <world_name> [options]");
  }

  ProbeOptions options;
  options.mysqlUrl = argv[1];
  options.runtimeReadModelPath = argv[2];
  options.contentRevisionKey = argv[3];
  options.worldInstanceKey = argv[4];
  options.worldName = argv[5];

  for(int i = 6; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if(arg == "--ai-db-name") {
      options.aiDatabaseName = need(i, argc, argv, arg);
    } else if(arg == "--perception-kind") {
      options.perceptionKind = need(i, argc, argv, arg);
    } else if(arg == "--max-distance") {
      options.maxDistance = parseDouble(need(i, argc, argv, arg)).value_or(options.maxDistance);
    } else if(arg == "--server-tick") {
      options.serverTick = parseU64(need(i, argc, argv, arg)).value_or(options.serverTick);
    } else if(arg == "--cooldown-ticks") {
      options.cooldownTicks = parseU64(need(i, argc, argv, arg)).value_or(options.cooldownTicks);
    } else if(arg == "--priority") {
      options.priorityValue = static_cast<int>(parseU64(need(i, argc, argv, arg)).value_or(options.priorityValue));
    } else if(arg == "--max-npcs") {
      options.maxNpcs = parseSize(need(i, argc, argv, arg)).value_or(options.maxNpcs);
    } else if(arg == "--max-players") {
      options.maxPlayers = parseSize(need(i, argc, argv, arg)).value_or(options.maxPlayers);
    } else if(arg == "--max-records") {
      options.maxRecords = parseSize(need(i, argc, argv, arg)).value_or(options.maxRecords);
    } else if(arg == "--enqueue-action") {
      options.enqueueAction = parseBoolish(need(i, argc, argv, arg), options.enqueueAction);
    } else if(arg == "--dry-run") {
      options.dryRun = true;
    } else if(arg == "--synthetic") {
      options.synthetic = true;
    } else if(arg == "--world-instance-uuid") {
      options.worldInstanceUuid = need(i, argc, argv, arg);
    } else if(arg == "--npc-entity-key") {
      options.npcEntityKey = need(i, argc, argv, arg);
    } else if(arg == "--npc-instance") {
      options.npcInstance = need(i, argc, argv, arg);
    } else if(arg == "--target-key") {
      options.targetKey = need(i, argc, argv, arg);
    } else if(arg == "--character-key") {
      options.characterKey = need(i, argc, argv, arg);
    } else if(arg == "--target-distance") {
      options.targetDistance = parseDouble(need(i, argc, argv, arg)).value_or(options.targetDistance);
    } else {
      throw std::runtime_error("unknown argument: " + std::string(arg));
    }
  }

  return options;
}

void jsonStats(std::ostream& out, const Mmo::NpcPerception::AssessmentStats& stats, bool comma = true) {
  out << "{\n";
  jsonCountField(out, "npc_actors", stats.npcActors);
  jsonCountField(out, "player_actors", stats.playerActors);
  jsonCountField(out, "evaluated_pairs", stats.evaluatedPairs);
  jsonCountField(out, "inactive_pairs_skipped", stats.inactivePairsSkipped);
  jsonCountField(out, "distance_pairs_skipped", stats.distancePairsSkipped);
  jsonCountField(out, "missing_perception_binding_pairs", stats.missingPerceptionBindingPairs);
  jsonCountField(out, "decisions", stats.decisions, false);
  out << "}";
  if(comma) {
    out << ",";
  }
  out << "\n";
}

void jsonDecision(std::ostream& out, const Mmo::NpcPerception::DecisionCandidate& decision, bool comma = true) {
  out << "{\n";
  jsonField(out, "npc_entity_key", decision.npcEntityKey);
  jsonField(out, "npc_instance", decision.npcInstance);
  jsonField(out, "target_key", decision.targetKey);
  jsonField(out, "character_key", decision.characterKey);
  jsonField(out, "session_uuid", decision.sessionUuid);
  jsonField(out, "character_uuid", decision.characterUuid);
  jsonField(out, "rule_key", decision.ruleKey);
  jsonField(out, "perception_kind", decision.perceptionKind);
  jsonField(out, "decision_kind", decision.decisionKind);
  jsonField(out, "idempotency_key", decision.idempotencyKey);
  jsonRawField(out, "server_tick", std::to_string(decision.serverTick));
  jsonRawField(out, "enqueue_action", decision.enqueueAction ? "true" : "false", false);
  out << "}";
  if(comma) {
    out << ",";
  }
  out << "\n";
}

void jsonRecordResult(std::ostream& out, const RecordResult& result, bool comma = true) {
  out << "{\n";
  jsonField(out, "npc_entity_key", result.decision.npcEntityKey);
  jsonField(out, "target_key", result.decision.targetKey);
  jsonField(out, "idempotency_key", result.decision.idempotencyKey);
  jsonField(out, "decision_uuid", result.recorded.decisionUuid);
  jsonField(out, "decision_status", result.recorded.decisionStatus);
  jsonField(out, "action_queue_uuid", result.recorded.actionQueueUuid, false);
  out << "}";
  if(comma) {
    out << ",";
  }
  out << "\n";
}

[[nodiscard]] std::string firstNpcInstanceOrThrow(
    const Mmo::WorldInstanceContent::WorldInstanceContentCache& cache,
    std::string_view explicitNpcInstance) {
  if(!explicitNpcInstance.empty()) {
    return std::string(explicitNpcInstance);
  }
  if(!cache.readModel().npcTemplates.empty()) {
    return cache.readModel().npcTemplates.front().npcInstance;
  }
  throw std::runtime_error("no npc instance available for synthetic perception probe");
}

} // namespace

int main(int argc, char** argv) {
  try {
    const ProbeOptions options = parseArgs(argc, argv);
    const auto target = Mmo::Server::parseMysqlUrl(options.mysqlUrl);

    Mmo::WorldInstanceContent::WorldInstanceContentCacheOptions cacheOptions;
    cacheOptions.runtimeReadModelPath = options.runtimeReadModelPath;
    cacheOptions.contentRevisionKey = options.contentRevisionKey;
    cacheOptions.worldInstanceKey = options.worldInstanceKey;
    cacheOptions.worldName = options.worldName;
    const auto cache = Mmo::WorldInstanceContent::WorldInstanceContentCache::load(cacheOptions);

    Mmo::NpcPerception::AssessmentOptions assessmentOptions;
    assessmentOptions.perceptionKind = options.perceptionKind;
    assessmentOptions.maxDistance = options.maxDistance;
    assessmentOptions.serverTick = options.serverTick;
    assessmentOptions.cooldownTicks = options.cooldownTicks;
    assessmentOptions.priorityValue = options.priorityValue;
    assessmentOptions.enqueueAction = options.enqueueAction;

    std::string worldInstanceUuid = options.worldInstanceUuid;
    std::vector<Mmo::NpcPerception::NpcActor> npcs;
    std::vector<Mmo::NpcPerception::PlayerActor> players;

    if(options.synthetic) {
      if(worldInstanceUuid.empty() && !options.dryRun) {
        worldInstanceUuid = Mmo::NpcPerceptionRuntime::resolveRuntimeWorldInstance(
                                target,
                                options.worldInstanceKey,
                                options.worldName)
                                .worldInstanceUuid;
      }
      if(worldInstanceUuid.empty()) {
        worldInstanceUuid = "00000000-0000-0000-0000-000000000000";
      }
      if(assessmentOptions.serverTick == 0) {
        assessmentOptions.serverTick = 1;
      }
      npcs.push_back({
          options.npcEntityKey,
          firstNpcInstanceOrThrow(cache, options.npcInstance),
          {0.0, 0.0, 0.0},
          true,
      });
      players.push_back({
          options.targetKey,
          options.characterKey,
          {options.targetDistance, 0.0, 0.0},
          true,
      });
    } else {
      Mmo::NpcPerceptionRuntime::RuntimeActorQueryOptions actorOptions;
      actorOptions.worldInstanceKey = options.worldInstanceKey;
      actorOptions.worldName = options.worldName;
      actorOptions.maxNpcs = options.maxNpcs;
      actorOptions.maxPlayers = options.maxPlayers;
      const auto snapshot = Mmo::NpcPerceptionRuntime::loadRuntimeActors(target, actorOptions);
      worldInstanceUuid = snapshot.world.worldInstanceUuid;
      if(assessmentOptions.serverTick == 0) {
        assessmentOptions.serverTick = snapshot.world.serverTick;
      }
      npcs = snapshot.npcs;
      players = snapshot.players;
    }

    const auto result = Mmo::NpcPerception::assessNpcPerception(cache, assessmentOptions, npcs, players);

    std::vector<RecordResult> recorded;
    if(!options.dryRun) {
      const std::size_t count = std::min(options.maxRecords, result.decisions.size());
      recorded.reserve(count);
      for(std::size_t i = 0; i < count; ++i) {
        Mmo::AiRuntime::RecordNpcPerceptionDecisionOptions recordOptions;
        recordOptions.aiDatabaseName = options.aiDatabaseName;
        recordOptions.worldInstanceUuid = worldInstanceUuid;
        recordOptions.sessionUuid = result.decisions[i].sessionUuid;
        recordOptions.characterUuid = result.decisions[i].characterUuid;
        recorded.push_back({
            result.decisions[i],
            Mmo::AiRuntime::recordNpcPerceptionDecision(target, recordOptions, result.decisions[i]),
        });
      }
    }

    const bool ready = !result.decisions.empty();
    std::cout << "{\n";
    jsonField(std::cout, "status", ready ? (options.dryRun ? "ready_dry_run" : "ready") : "no_decision");
    jsonRawField(std::cout, "dry_run", options.dryRun ? "true" : "false");
    jsonRawField(std::cout, "synthetic", options.synthetic ? "true" : "false");
    jsonField(std::cout, "ai_db_name", options.aiDatabaseName);
    jsonField(std::cout, "world_instance_uuid", worldInstanceUuid);
    jsonField(std::cout, "content_revision_key", cache.contentRevisionKey());
    jsonField(std::cout, "world_instance_key", cache.worldInstanceKey());
    jsonField(std::cout, "world_name", cache.worldName());
    std::cout << "\"stats\": ";
    jsonStats(std::cout, result.stats);
    jsonCountField(std::cout, "recorded_count", recorded.size());
    if(!result.decisions.empty()) {
      std::cout << "\"first_decision\": ";
      jsonDecision(std::cout, result.decisions.front(), !recorded.empty());
    } else {
      jsonRawField(std::cout, "first_decision", "null", !recorded.empty());
    }
    if(!recorded.empty()) {
      std::cout << "\"recorded\": [\n";
      for(std::size_t i = 0; i < recorded.size(); ++i) {
        jsonRecordResult(std::cout, recorded[i], i + 1 < recorded.size());
      }
      std::cout << "]\n";
    }
    std::cout << "}\n";
    return ready ? 0 : 2;
  } catch(const std::exception& e) {
    std::cout << "{\n";
    jsonField(std::cout, "status", "error");
    jsonField(std::cout, "message", e.what(), false);
    std::cout << "}\n";
    return 1;
  }
}
