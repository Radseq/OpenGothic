#include "mmo_ai_runtime_persistence.h"
#include "mmo_server_persistence.h"

#include <charconv>
#include <cstddef>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

struct ProbeOptions final {
  std::string mysqlUrl;
  std::string aiDatabaseName = "mmo_ai_runtime";
  std::string worldInstanceUuid;
  std::string workerId = "mmo_manual_action_queue_probe";
  std::string skipReason = "step235_manual_probe_claim_cleanup";
  std::size_t maxPendingRows = 16;
  bool claimOne = false;
  bool skipAfterClaim = false;
  bool allowMutation = false;
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

[[nodiscard]] std::optional<std::size_t> parseSize(std::string_view text) noexcept {
  std::size_t value = 0;
  const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
  if(result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

[[nodiscard]] ProbeOptions parseArgs(int argc, char** argv) {
  if(argc < 2) {
    throw std::runtime_error(
        "usage: " + std::string(argv[0]) +
        " <mysql_url> [--ai-db-name NAME] [--world-instance-uuid UUID] [--max-pending N] "
        "[--claim-one --worker-id ID --i-understand-this-mutates-db] "
        "[--skip-after-claim --skip-reason TEXT]");
  }

  ProbeOptions options;
  options.mysqlUrl = argv[1];
  for(int i = 2; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if(arg == "--ai-db-name") {
      options.aiDatabaseName = need(i, argc, argv, arg);
    } else if(arg == "--world-instance-uuid") {
      options.worldInstanceUuid = need(i, argc, argv, arg);
    } else if(arg == "--worker-id") {
      options.workerId = need(i, argc, argv, arg);
    } else if(arg == "--skip-reason") {
      options.skipReason = need(i, argc, argv, arg);
    } else if(arg == "--max-pending" || arg == "--max-pending-rows") {
      options.maxPendingRows = parseSize(need(i, argc, argv, arg)).value_or(options.maxPendingRows);
    } else if(arg == "--claim-one") {
      options.claimOne = true;
    } else if(arg == "--skip-after-claim") {
      options.skipAfterClaim = true;
    } else if(arg == "--i-understand-this-mutates-db") {
      options.allowMutation = true;
    } else {
      throw std::runtime_error("unknown argument: " + std::string(arg));
    }
  }

  if(options.skipAfterClaim && !options.claimOne) {
    throw std::runtime_error("--skip-after-claim requires --claim-one");
  }
  if(options.claimOne && !options.allowMutation) {
    throw std::runtime_error("--claim-one mutates DB; pass --i-understand-this-mutates-db explicitly");
  }
  if(options.workerId.empty()) {
    throw std::runtime_error("worker id must not be empty");
  }
  return options;
}

void jsonInspection(
    std::ostream& out,
    const Mmo::AiRuntime::NpcPerceptionActionQueueInspection& inspection,
    bool comma = true) {
  out << "{\n";
  jsonCountField(out, "total_count", inspection.totalCount);
  jsonCountField(out, "pending_count", inspection.pendingCount);
  jsonCountField(out, "due_pending_count", inspection.duePendingCount);
  jsonCountField(out, "delayed_retry_count", inspection.delayedRetryCount);
  jsonCountField(out, "claimed_count", inspection.claimedCount);
  jsonCountField(out, "applied_count", inspection.appliedCount);
  jsonCountField(out, "failed_count", inspection.failedCount);
  jsonCountField(out, "skipped_count", inspection.skippedCount);
  jsonCountField(out, "dispatch_log_count", inspection.dispatchLogCount);
  jsonRawField(out, "pending_actions", inspection.pendingActionsJson, false);
  out << '}';
  if(comma) {
    out << ',';
  }
  out << '\n';
}

void jsonClaimedAction(
    std::ostream& out,
    const Mmo::AiRuntime::ClaimedNpcPerceptionAction& action,
    bool comma = true) {
  out << "{\n";
  jsonRawField(out, "claimed", action.claimed ? "true" : "false");
  jsonField(out, "action_queue_uuid", action.actionQueueUuid);
  jsonField(out, "decision_uuid", action.decisionUuid);
  jsonField(out, "action_kind", action.actionKind);
  jsonField(out, "world_instance_uuid", action.worldInstanceUuid);
  jsonField(out, "session_uuid", action.sessionUuid);
  jsonField(out, "character_uuid", action.characterUuid);
  jsonField(out, "target_key", action.targetKey);
  jsonField(out, "idempotency_key", action.idempotencyKey);
  jsonRawField(out, "request_payload", action.requestPayloadJson.empty() ? "{}" : action.requestPayloadJson, false);
  out << '}';
  if(comma) {
    out << ',';
  }
  out << '\n';
}

[[nodiscard]] std::string statusFor(
    const ProbeOptions& options,
    const Mmo::AiRuntime::NpcPerceptionActionQueueInspection& before,
    const Mmo::AiRuntime::ClaimedNpcPerceptionAction* claimed,
    const Mmo::AiRuntime::SkippedNpcPerceptionAction* skipped) {
  if(!options.claimOne) {
    return before.duePendingCount > 0 ? "ready" : "empty";
  }
  if(claimed == nullptr || !claimed->claimed) {
    return "claim_empty";
  }
  if(skipped != nullptr) {
    return skipped->actionStatus == "skipped" ? "claimed_and_skipped" : "claimed_skip_unconfirmed";
  }
  return "claimed";
}

} // namespace

int main(int argc, char** argv) {
  try {
    const ProbeOptions options = parseArgs(argc, argv);
    const auto target = Mmo::Server::parseMysqlUrl(options.mysqlUrl);

    Mmo::AiRuntime::NpcPerceptionActionQueueInspectOptions inspectOptions;
    inspectOptions.aiDatabaseName = options.aiDatabaseName;
    inspectOptions.worldInstanceUuid = options.worldInstanceUuid;
    inspectOptions.maxPendingRows = options.maxPendingRows;

    const auto before = Mmo::AiRuntime::inspectNpcPerceptionActionQueue(target, inspectOptions);

    Mmo::AiRuntime::ClaimedNpcPerceptionAction claimed;
    bool hasClaimResult = false;
    Mmo::AiRuntime::SkippedNpcPerceptionAction skipped;
    bool hasSkipResult = false;

    if(options.claimOne) {
      Mmo::AiRuntime::ClaimNpcPerceptionActionOptions claimOptions;
      claimOptions.aiDatabaseName = options.aiDatabaseName;
      claimOptions.workerId = options.workerId;
      claimed = Mmo::AiRuntime::claimNextNpcPerceptionAction(target, claimOptions);
      hasClaimResult = true;

      if(options.skipAfterClaim && claimed.claimed) {
        Mmo::AiRuntime::SkipNpcPerceptionActionOptions skipOptions;
        skipOptions.aiDatabaseName = options.aiDatabaseName;
        skipOptions.actionQueueUuid = claimed.actionQueueUuid;
        skipOptions.workerId = options.workerId;
        skipOptions.reason = options.skipReason;
        skipped = Mmo::AiRuntime::skipNpcPerceptionAction(target, skipOptions);
        hasSkipResult = true;
      }
    }

    const auto after = options.claimOne || options.skipAfterClaim
        ? Mmo::AiRuntime::inspectNpcPerceptionActionQueue(target, inspectOptions)
        : before;

    std::cout << "{\n";
    jsonField(
        std::cout,
        "status",
        statusFor(options, before, hasClaimResult ? &claimed : nullptr, hasSkipResult ? &skipped : nullptr));
    jsonRawField(std::cout, "mutated_db", options.claimOne ? "true" : "false");
    jsonField(std::cout, "ai_db_name", options.aiDatabaseName);
    jsonField(std::cout, "world_instance_uuid", options.worldInstanceUuid);
    jsonField(std::cout, "worker_id", options.workerId);
    jsonCountField(std::cout, "max_pending_rows", options.maxPendingRows);
    std::cout << "\"before\": ";
    jsonInspection(std::cout, before, true);
    if(hasClaimResult) {
      std::cout << "\"claim\": ";
      jsonClaimedAction(std::cout, claimed, true);
    }
    if(hasSkipResult) {
      std::cout << "\"skip\": {\n";
      jsonField(std::cout, "action_status", skipped.actionStatus, false);
      std::cout << "},\n";
    }
    std::cout << "\"after\": ";
    jsonInspection(std::cout, after, false);
    std::cout << "}\n";

    if(options.claimOne && !claimed.claimed) {
      return 2;
    }
    if(hasSkipResult && skipped.actionStatus != "skipped") {
      return 3;
    }
    return 0;
  } catch(const std::exception& e) {
    std::cout << "{\n";
    jsonField(std::cout, "status", "error");
    jsonField(std::cout, "message", e.what(), false);
    std::cout << "}\n";
    return 1;
  }
}
