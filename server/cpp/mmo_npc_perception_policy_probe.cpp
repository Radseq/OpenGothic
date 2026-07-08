#include "mmo_npc_perception_policy.h"

#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string_view>
#include <vector>

namespace {

struct ProbeOptions final {
  std::filesystem::path runtimeReadModelPath;
  std::string contentRevisionKey;
  std::string worldInstanceKey;
  std::string worldName;
  std::string npcInstance;
  std::string npcEntityKey = "npc:probe";
  std::string targetKey = "PC_HERO";
  std::string characterKey = "PC_HERO";
  std::string perceptionKind = "PERC_ASSESSPLAYER";
  double targetDistance = 100.0;
  double maxDistance = 1500.0;
  std::uint64_t serverTick = 1;
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

[[nodiscard]] std::string need(int& i, int argc, char** argv, std::string_view flag) {
  if(i + 1 >= argc) {
    throw std::runtime_error(std::string(flag) + " requires value");
  }
  return argv[++i];
}

[[nodiscard]] ProbeOptions parseArgs(int argc, char** argv) {
  if(argc < 5) {
    throw std::runtime_error(
        "usage: " + std::string(argv[0]) +
        " <runtime_read_model.json> <content_revision_key> <world_instance_key> <world_name> [options]");
  }

  ProbeOptions options;
  options.runtimeReadModelPath = argv[1];
  options.contentRevisionKey = argv[2];
  options.worldInstanceKey = argv[3];
  options.worldName = argv[4];

  for(int i = 5; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if(arg == "--npc-instance") {
      options.npcInstance = need(i, argc, argv, arg);
    } else if(arg == "--npc-entity-key") {
      options.npcEntityKey = need(i, argc, argv, arg);
    } else if(arg == "--target-key") {
      options.targetKey = need(i, argc, argv, arg);
    } else if(arg == "--character-key") {
      options.characterKey = need(i, argc, argv, arg);
    } else if(arg == "--perception-kind") {
      options.perceptionKind = need(i, argc, argv, arg);
    } else if(arg == "--target-distance") {
      options.targetDistance = parseDouble(need(i, argc, argv, arg)).value_or(options.targetDistance);
    } else if(arg == "--max-distance") {
      options.maxDistance = parseDouble(need(i, argc, argv, arg)).value_or(options.maxDistance);
    } else if(arg == "--server-tick") {
      options.serverTick = parseU64(need(i, argc, argv, arg)).value_or(options.serverTick);
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

} // namespace

int main(int argc, char** argv) {
  try {
    const ProbeOptions options = parseArgs(argc, argv);

    Mmo::WorldInstanceContent::WorldInstanceContentCacheOptions cacheOptions;
    cacheOptions.runtimeReadModelPath = options.runtimeReadModelPath;
    cacheOptions.contentRevisionKey = options.contentRevisionKey;
    cacheOptions.worldInstanceKey = options.worldInstanceKey;
    cacheOptions.worldName = options.worldName;
    const auto cache = Mmo::WorldInstanceContent::WorldInstanceContentCache::load(cacheOptions);

    std::string npcInstance = options.npcInstance;
    if(npcInstance.empty() && !cache.readModel().npcTemplates.empty()) {
      npcInstance = cache.readModel().npcTemplates.front().npcInstance;
    }
    if(npcInstance.empty()) {
      throw std::runtime_error("no npc instance available for perception probe");
    }

    Mmo::NpcPerception::AssessmentOptions assessmentOptions;
    assessmentOptions.perceptionKind = options.perceptionKind;
    assessmentOptions.maxDistance = options.maxDistance;
    assessmentOptions.serverTick = options.serverTick;

    std::vector<Mmo::NpcPerception::NpcActor> npcs;
    npcs.push_back({
        options.npcEntityKey,
        npcInstance,
        {0.0, 0.0, 0.0},
        true,
    });

    std::vector<Mmo::NpcPerception::PlayerActor> players;
    players.push_back({
        options.targetKey,
        options.characterKey,
        {options.targetDistance, 0.0, 0.0},
        true,
    });

    const auto result = Mmo::NpcPerception::assessNpcPerception(cache, assessmentOptions, npcs, players);
    const bool ready = !result.decisions.empty();

    std::cout << "{\n";
    jsonField(std::cout, "status", ready ? "ready" : "no_decision");
    jsonField(std::cout, "content_revision_key", cache.contentRevisionKey());
    jsonField(std::cout, "world_instance_key", cache.worldInstanceKey());
    jsonField(std::cout, "world_name", cache.worldName());
    jsonField(std::cout, "perception_kind", assessmentOptions.perceptionKind);
    std::cout << "\"stats\": ";
    jsonStats(std::cout, result.stats);
    if(!result.decisions.empty()) {
      const auto& decision = result.decisions.front();
      std::cout << "\"first_decision\": {\n";
      jsonField(std::cout, "npc_entity_key", decision.npcEntityKey);
      jsonField(std::cout, "npc_instance", decision.npcInstance);
      jsonField(std::cout, "target_key", decision.targetKey);
      jsonField(std::cout, "rule_key", decision.ruleKey);
      jsonField(std::cout, "function_symbol", decision.functionSymbol);
      jsonField(std::cout, "decision_kind", decision.decisionKind);
      jsonField(std::cout, "idempotency_key", decision.idempotencyKey);
      jsonRawField(std::cout, "distance", std::to_string(decision.distance));
      jsonRawField(std::cout, "enqueue_action", decision.enqueueAction ? "true" : "false", false);
      std::cout << "},\n";
      jsonRawField(std::cout, "first_decision_payload", Mmo::NpcPerception::decisionPayloadJson(decision), false);
    } else {
      jsonRawField(std::cout, "first_decision", "null", false);
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
