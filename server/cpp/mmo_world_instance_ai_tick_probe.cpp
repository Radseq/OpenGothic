#include "mmo_server_persistence.h"
#include "mmo_world_instance_ai_tick.h"
#include "mmo_world_instance_ai_tick_evidence.h"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

struct ProbeOptions final {
  std::string mysqlUrl;
  Mmo::WorldInstanceAiTick::WorldInstanceAiTickOptions tick;
  Mmo::WorldInstanceAiTick::WorldInstanceAiTickEvidenceOptions evidence;
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
    out << ',';
  }
  out << '\n';
}

void jsonRawField(std::ostream& out, std::string_view name, std::string_view value, bool comma = true) {
  jsonEscape(out, name);
  out << ": " << value;
  if(comma) {
    out << ',';
  }
  out << '\n';
}

void jsonCountField(std::ostream& out, std::string_view name, std::size_t value, bool comma = true) {
  jsonEscape(out, name);
  out << ": " << value;
  if(comma) {
    out << ',';
  }
  out << '\n';
}

[[nodiscard]] std::string need(int& i, int argc, char** argv, std::string_view flag) {
  if(i + 1 >= argc) {
    throw std::runtime_error(std::string(flag) + " requires value");
  }
  return argv[++i];
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

[[nodiscard]] ProbeOptions parseArgs(int argc, char** argv) {
  if(argc < 6) {
    throw std::runtime_error(
        "usage: " + std::string(argv[0]) +
        " <mysql_url> <runtime_read_model.json> <content_revision_key> <world_instance_key> <world_name> [options]");
  }

  ProbeOptions options;
  options.mysqlUrl = argv[1];
  options.tick.runtimeReadModelPath = std::filesystem::path(argv[2]);
  options.tick.contentRevisionKey = argv[3];
  options.tick.worldInstanceKey = argv[4];
  options.tick.worldName = argv[5];

  for(int i = 6; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if(arg == "--ai-db-name") {
      options.tick.aiDatabaseName = need(i, argc, argv, arg);
    } else if(arg == "--perception-kind") {
      options.tick.perceptionKind = need(i, argc, argv, arg);
    } else if(arg == "--max-distance") {
      options.tick.maxDistance = parseDouble(need(i, argc, argv, arg)).value_or(options.tick.maxDistance);
    } else if(arg == "--server-tick") {
      options.tick.serverTick = parseU64(need(i, argc, argv, arg)).value_or(options.tick.serverTick);
    } else if(arg == "--cooldown-ticks") {
      options.tick.cooldownTicks = parseU64(need(i, argc, argv, arg)).value_or(options.tick.cooldownTicks);
    } else if(arg == "--priority") {
      options.tick.priorityValue = static_cast<int>(parseU64(need(i, argc, argv, arg)).value_or(options.tick.priorityValue));
    } else if(arg == "--max-npcs") {
      options.tick.maxNpcs = parseSize(need(i, argc, argv, arg)).value_or(options.tick.maxNpcs);
    } else if(arg == "--max-players") {
      options.tick.maxPlayers = parseSize(need(i, argc, argv, arg)).value_or(options.tick.maxPlayers);
    } else if(arg == "--max-records") {
      options.tick.maxRecords = parseSize(need(i, argc, argv, arg)).value_or(options.tick.maxRecords);
    } else if(arg == "--no-repair-weak-npc-entity-keys") {
      options.tick.repairWeakNpcEntityKeys = false;
    } else if(arg == "--include-weak-npc-identity") {
      options.tick.includeWeakNpcIdentity = true;
    } else if(arg == "--enqueue-action") {
      options.tick.enqueueAction = parseBoolish(need(i, argc, argv, arg), options.tick.enqueueAction);
    } else if(arg == "--strict-evidence") {
      options.evidence.requireActorPair = true;
      options.evidence.requireDecision = true;
      options.evidence.requireCleanNpcIdentity = true;
      options.evidence.minAcceptedNpcs = std::max<std::size_t>(options.evidence.minAcceptedNpcs, 1);
      options.evidence.minPlayerActors = std::max<std::size_t>(options.evidence.minPlayerActors, 1);
      options.evidence.minDecisions = std::max<std::size_t>(options.evidence.minDecisions, 1);
    } else if(arg == "--require-actor-pair") {
      options.evidence.requireActorPair = true;
      options.evidence.minAcceptedNpcs = std::max<std::size_t>(options.evidence.minAcceptedNpcs, 1);
      options.evidence.minPlayerActors = std::max<std::size_t>(options.evidence.minPlayerActors, 1);
    } else if(arg == "--require-decision") {
      options.evidence.requireDecision = true;
      options.evidence.minDecisions = std::max<std::size_t>(options.evidence.minDecisions, 1);
    } else if(arg == "--require-clean-npc-identity") {
      options.evidence.requireCleanNpcIdentity = true;
    } else if(arg == "--require-no-record-limit-skip") {
      options.evidence.requireNoRecordLimitSkip = true;
    } else if(arg == "--min-accepted-npcs") {
      options.evidence.minAcceptedNpcs = parseSize(need(i, argc, argv, arg)).value_or(options.evidence.minAcceptedNpcs);
    } else if(arg == "--min-player-actors") {
      options.evidence.minPlayerActors = parseSize(need(i, argc, argv, arg)).value_or(options.evidence.minPlayerActors);
    } else if(arg == "--min-decisions") {
      options.evidence.minDecisions = parseSize(need(i, argc, argv, arg)).value_or(options.evidence.minDecisions);
    } else if(arg == "--max-skipped-weak-npc-identity") {
      options.evidence.maxSkippedWeakEntityKeys = parseSize(need(i, argc, argv, arg)).value_or(options.evidence.maxSkippedWeakEntityKeys);
    } else if(arg == "--max-skipped-missing-npc-instance") {
      options.evidence.maxSkippedMissingNpcInstance = parseSize(need(i, argc, argv, arg)).value_or(options.evidence.maxSkippedMissingNpcInstance);
    } else if(arg == "--dry-run") {
      options.tick.dryRun = true;
    } else if(arg == "--write") {
      options.tick.dryRun = false;
    } else {
      throw std::runtime_error("unknown argument: " + std::string(arg));
    }
  }

  return options;
}

void jsonNpcIdentityStats(
    std::ostream& out,
    const Mmo::NpcPerceptionRuntime::RuntimeNpcIdentityStats& stats,
    bool comma = true) {
  out << "{\n";
  jsonCountField(out, "npc_rows_read", stats.npcRowsRead);
  jsonCountField(out, "accepted_npcs", stats.acceptedNpcs);
  jsonCountField(out, "repaired_entity_keys", stats.repairedEntityKeys);
  jsonCountField(out, "skipped_weak_entity_keys", stats.skippedWeakEntityKeys);
  jsonCountField(out, "skipped_missing_npc_instance", stats.skippedMissingNpcInstance, false);
  out << '}';
  if(comma) {
    out << ',';
  }
  out << '\n';
}

void jsonAssessmentStats(std::ostream& out, const Mmo::NpcPerception::AssessmentStats& stats, bool comma = true) {
  out << "{\n";
  jsonCountField(out, "npc_actors", stats.npcActors);
  jsonCountField(out, "player_actors", stats.playerActors);
  jsonCountField(out, "evaluated_pairs", stats.evaluatedPairs);
  jsonCountField(out, "inactive_pairs_skipped", stats.inactivePairsSkipped);
  jsonCountField(out, "distance_pairs_skipped", stats.distancePairsSkipped);
  jsonCountField(out, "missing_perception_binding_pairs", stats.missingPerceptionBindingPairs);
  jsonCountField(out, "decisions", stats.decisions, false);
  out << '}';
  if(comma) {
    out << ',';
  }
  out << '\n';
}

void jsonEvidenceReport(
    std::ostream& out,
    const Mmo::WorldInstanceAiTick::WorldInstanceAiTickEvidenceReport& report,
    bool comma = true) {
  out << "{\n";
  jsonRawField(out, "accepted", report.accepted ? "true" : "false");
  jsonField(out, "status", report.status);
  jsonField(out, "reason", report.reason);
  jsonCountField(out, "accepted_npcs", report.acceptedNpcs);
  jsonCountField(out, "player_actors", report.playerActors);
  jsonCountField(out, "decisions", report.decisions);
  jsonCountField(out, "weak_identity_skips", report.weakIdentitySkips);
  jsonCountField(out, "missing_npc_instance_skips", report.missingNpcInstanceSkips);
  jsonCountField(out, "record_limit_skips", report.recordLimitSkips);
  jsonRawField(out, "accepted_npc_ratio", std::to_string(report.acceptedNpcRatio), false);
  out << '}';
  if(comma) {
    out << ',';
  }
  out << '\n';
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
  jsonRawField(out, "distance", std::to_string(decision.distance));
  jsonRawField(out, "server_tick", std::to_string(decision.serverTick));
  jsonRawField(out, "enqueue_action", decision.enqueueAction ? "true" : "false", false);
  out << '}';
  if(comma) {
    out << ',';
  }
  out << '\n';
}

void jsonRecordedDecision(
    std::ostream& out,
    const Mmo::WorldInstanceAiTick::RecordedDecision& decision,
    bool comma = true) {
  out << "{\n";
  jsonField(out, "npc_entity_key", decision.decision.npcEntityKey);
  jsonField(out, "target_key", decision.decision.targetKey);
  jsonField(out, "idempotency_key", decision.decision.idempotencyKey);
  jsonField(out, "decision_uuid", decision.recorded.decisionUuid);
  jsonField(out, "decision_status", decision.recorded.decisionStatus);
  jsonField(out, "action_queue_uuid", decision.recorded.actionQueueUuid, false);
  out << '}';
  if(comma) {
    out << ',';
  }
  out << '\n';
}

[[nodiscard]] std::string statusFor(
    const Mmo::WorldInstanceAiTick::WorldInstanceAiTickResult& result,
    const Mmo::WorldInstanceAiTick::WorldInstanceAiTickEvidenceReport& evidence) {
  if(!evidence.accepted) {
    return "evidence_failed";
  }
  if(result.assessment.decisions > 0) {
    return result.dryRun ? "ready_dry_run" : "ready";
  }
  if(result.assessment.npcActors == 0 || result.assessment.playerActors == 0) {
    return "no_actor_pair";
  }
  return "no_decision";
}

} // namespace

int main(int argc, char** argv) {
  try {
    const ProbeOptions options = parseArgs(argc, argv);
    const auto target = Mmo::Server::parseMysqlUrl(options.mysqlUrl);
    const auto result = Mmo::WorldInstanceAiTick::runWorldInstanceAiTick(target, options.tick);
    const auto evidence = Mmo::WorldInstanceAiTick::evaluateWorldInstanceAiTickEvidence(result, options.evidence);

    std::cout << "{\n";
    jsonField(std::cout, "status", statusFor(result, evidence));
    jsonRawField(std::cout, "dry_run", result.dryRun ? "true" : "false");
    jsonField(std::cout, "ai_db_name", options.tick.aiDatabaseName);
    jsonField(std::cout, "content_revision_key", options.tick.contentRevisionKey);
    jsonField(std::cout, "world_instance_uuid", result.world.worldInstanceUuid);
    jsonField(std::cout, "world_instance_key", result.world.worldInstanceKey);
    jsonField(std::cout, "world_name", result.world.worldName);
    jsonRawField(std::cout, "server_tick", std::to_string(result.world.serverTick));
    jsonField(std::cout, "perception_kind", options.tick.perceptionKind);
    jsonRawField(std::cout, "max_distance", std::to_string(options.tick.maxDistance));
    jsonRawField(std::cout, "repair_weak_npc_entity_keys", options.tick.repairWeakNpcEntityKeys ? "true" : "false");
    jsonRawField(std::cout, "include_weak_npc_identity", options.tick.includeWeakNpcIdentity ? "true" : "false");
    std::cout << "\"npc_identity\": ";
    jsonNpcIdentityStats(std::cout, result.npcIdentity);
    std::cout << "\"assessment\": ";
    jsonAssessmentStats(std::cout, result.assessment);
    jsonCountField(std::cout, "record_limit", result.recordLimit);
    jsonCountField(std::cout, "recorded_count", result.recorded.size());
    jsonCountField(std::cout, "skipped_by_record_limit", result.skippedByRecordLimit);
    std::cout << "\"evidence\": ";
    jsonEvidenceReport(std::cout, evidence);
    if(result.decisions.empty()) {
      jsonRawField(std::cout, "first_decision", "null", !result.recorded.empty());
    } else {
      std::cout << "\"first_decision\": ";
      jsonDecision(std::cout, result.decisions.front(), !result.recorded.empty());
    }
    if(!result.recorded.empty()) {
      std::cout << "\"recorded\": [\n";
      for(std::size_t i = 0; i < result.recorded.size(); ++i) {
        jsonRecordedDecision(std::cout, result.recorded[i], i + 1 < result.recorded.size());
      }
      std::cout << "]\n";
    }
    std::cout << "}\n";
    if(!evidence.accepted) {
      return 3;
    }
    return result.assessment.decisions > 0 ? 0 : 2;
  } catch(const std::exception& e) {
    std::cout << "{\n";
    jsonField(std::cout, "status", "error");
    jsonField(std::cout, "message", e.what(), false);
    std::cout << "}\n";
    return 1;
  }
}


