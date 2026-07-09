#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <optional>
#include <unordered_map>
#include <vector>

namespace Mmo::Server {

enum class OutboundGameplayDeliveryStatus : std::uint8_t {
  Pending = 0,
  Acked,
  Nacked,
  TimedOut,
};

[[nodiscard]] constexpr const char* outboundGameplayDeliveryStatusName(OutboundGameplayDeliveryStatus status) noexcept {
  switch(status) {
    case OutboundGameplayDeliveryStatus::Pending:  return "pending";
    case OutboundGameplayDeliveryStatus::Acked:    return "acked";
    case OutboundGameplayDeliveryStatus::Nacked:   return "nacked";
    case OutboundGameplayDeliveryStatus::TimedOut: return "timed_out";
  }
  return "unknown";
}

enum class OutboundGameplayReceiptKind : std::uint8_t {
  Acked = 0,
  Nacked,
  Duplicate,
  UnknownAckKey,
  ConflictingTerminalStatus,
  InvalidReceipt,
  LateAfterTimeout,
};

[[nodiscard]] constexpr const char* outboundGameplayReceiptKindName(OutboundGameplayReceiptKind kind) noexcept {
  switch(kind) {
    case OutboundGameplayReceiptKind::Acked:                     return "acked";
    case OutboundGameplayReceiptKind::Nacked:                    return "nacked";
    case OutboundGameplayReceiptKind::Duplicate:                 return "duplicate";
    case OutboundGameplayReceiptKind::UnknownAckKey:             return "unknown_ack_key";
    case OutboundGameplayReceiptKind::ConflictingTerminalStatus: return "conflicting_terminal_status";
    case OutboundGameplayReceiptKind::InvalidReceipt:            return "invalid_receipt";
    case OutboundGameplayReceiptKind::LateAfterTimeout:          return "late_after_timeout";
  }
  return "unknown";
}

struct OutboundGameplayAttempt final {
  std::string actionId;
  std::string ackKey;
  std::string sessionUuid;
  std::string characterKey;
  std::string gameplayKind = "npc_dialog_intent";
  std::uint64_t packetSequence = 0;
  std::uint64_t localSequence = 0;
  std::uint64_t serverTick = 0;
  std::uint64_t sentAtMs = 0;
  std::uint64_t ackDeadlineMs = 0;
  std::uint32_t sendAttempts = 1;
  OutboundGameplayDeliveryStatus status = OutboundGameplayDeliveryStatus::Pending;
  std::string terminalReason;
  std::string terminalMessage;
};

struct OutboundGameplayRegisterResult final {
  bool accepted = false;
  const char* state = "invalid";
};

struct OutboundGameplayReceipt final {
  OutboundGameplayReceiptKind kind = OutboundGameplayReceiptKind::InvalidReceipt;
  OutboundGameplayDeliveryStatus previousStatus = OutboundGameplayDeliveryStatus::Pending;
  OutboundGameplayDeliveryStatus currentStatus = OutboundGameplayDeliveryStatus::Pending;
  std::string actionId;
  std::string ackKey;
  std::string reason;
};

struct OutboundGameplayExpiredAttempt final {
  std::string actionId;
  std::string ackKey;
  std::string sessionUuid;
  std::string characterKey;
  std::uint64_t packetSequence = 0;
  std::uint64_t deadlineMs = 0;
};

struct OutboundGameplayDeliveryStats final {
  std::uint64_t pending = 0;
  std::uint64_t acked = 0;
  std::uint64_t nacked = 0;
  std::uint64_t timedOut = 0;
  std::uint64_t unknownAcks = 0;
  std::uint64_t duplicateReceipts = 0;
  std::uint64_t conflictingReceipts = 0;
  std::uint64_t invalidReceipts = 0;
  std::uint64_t lateAfterTimeoutReceipts = 0;
};

class OutboundGameplayDeliveryState final {
  public:
    [[nodiscard]] OutboundGameplayRegisterResult recordSent(OutboundGameplayAttempt attempt) {
      if(attempt.actionId.empty() || attempt.ackKey.empty())
        return {false, "invalid_identity"};
      if(attempt.ackDeadlineMs < attempt.sentAtMs)
        attempt.ackDeadlineMs = attempt.sentAtMs;

      const auto key = attempt.ackKey;
      const auto found = attempts.find(key);
      if(found == attempts.end()) {
        attempts.emplace(key, std::move(attempt));
        return {true, "sent"};
      }

      auto& existing = found->second;
      if(existing.status != OutboundGameplayDeliveryStatus::Pending)
        return {false, "already_terminal"};

      existing.sentAtMs = std::max(existing.sentAtMs, attempt.sentAtMs);
      existing.ackDeadlineMs = std::max(existing.ackDeadlineMs, attempt.ackDeadlineMs);
      existing.packetSequence = attempt.packetSequence;
      existing.localSequence = attempt.localSequence;
      existing.serverTick = attempt.serverTick;
      ++existing.sendAttempts;
      return {true, "resent_pending"};
    }

    [[nodiscard]] OutboundGameplayReceipt recordReceipt(std::string_view actionId,
                                                        std::string_view ackKey,
                                                        bool acked,
                                                        std::string_view reason,
                                                        std::string_view message) {
      if(actionId.empty() || ackKey.empty()) {
        ++statsCounters.invalidReceipts;
        OutboundGameplayReceipt receipt;
        receipt.kind = OutboundGameplayReceiptKind::InvalidReceipt;
        receipt.reason = "missing_action_or_ack_key";
        return receipt;
      }

      const auto it = attempts.find(std::string(ackKey));
      if(it == attempts.end()) {
        ++statsCounters.unknownAcks;
        OutboundGameplayReceipt receipt;
        receipt.kind = OutboundGameplayReceiptKind::UnknownAckKey;
        receipt.actionId = std::string(actionId);
        receipt.ackKey = std::string(ackKey);
        receipt.reason = "ack_key_not_pending_in_process";
        return receipt;
      }

      auto& attempt = it->second;
      const auto requested = acked ? OutboundGameplayDeliveryStatus::Acked : OutboundGameplayDeliveryStatus::Nacked;
      OutboundGameplayReceipt receipt;
      receipt.previousStatus = attempt.status;
      receipt.currentStatus = attempt.status;
      receipt.actionId = attempt.actionId;
      receipt.ackKey = attempt.ackKey;

      if(std::string_view(attempt.actionId) != actionId) {
        ++statsCounters.conflictingReceipts;
        receipt.kind = OutboundGameplayReceiptKind::ConflictingTerminalStatus;
        receipt.reason = "action_id_mismatch";
        return receipt;
      }

      if(attempt.status == OutboundGameplayDeliveryStatus::TimedOut) {
        ++statsCounters.lateAfterTimeoutReceipts;
        attempt.terminalReason = reason.empty() ? std::string("late_after_timeout") : std::string(reason);
        attempt.terminalMessage = std::string(message);
        receipt.kind = OutboundGameplayReceiptKind::LateAfterTimeout;
        receipt.reason = "terminal_state_already_timed_out";
        return receipt;
      }

      if(attempt.status == requested) {
        ++statsCounters.duplicateReceipts;
        attempt.terminalReason = reason.empty() ? attempt.terminalReason : std::string(reason);
        attempt.terminalMessage = message.empty() ? attempt.terminalMessage : std::string(message);
        receipt.kind = OutboundGameplayReceiptKind::Duplicate;
        receipt.reason = "terminal_status_replayed";
        return receipt;
      }

      if(attempt.status != OutboundGameplayDeliveryStatus::Pending) {
        ++statsCounters.conflictingReceipts;
        receipt.kind = OutboundGameplayReceiptKind::ConflictingTerminalStatus;
        receipt.reason = "terminal_status_conflict";
        return receipt;
      }

      attempt.status = requested;
      attempt.terminalReason = std::string(reason);
      attempt.terminalMessage = std::string(message);
      receipt.currentStatus = attempt.status;
      receipt.kind = acked ? OutboundGameplayReceiptKind::Acked : OutboundGameplayReceiptKind::Nacked;
      receipt.reason = acked ? "ack_recorded" : "nack_recorded";
      return receipt;
    }

    [[nodiscard]] std::vector<OutboundGameplayExpiredAttempt> expirePending(std::uint64_t nowMs) {
      std::vector<OutboundGameplayExpiredAttempt> expired;
      for(auto& [ackKey, attempt] : attempts) {
        if(attempt.status != OutboundGameplayDeliveryStatus::Pending)
          continue;
        if(attempt.ackDeadlineMs == 0 || attempt.ackDeadlineMs > nowMs)
          continue;
        attempt.status = OutboundGameplayDeliveryStatus::TimedOut;
        attempt.terminalReason = "ack_timeout_no_durable_retry_yet";
        expired.push_back({attempt.actionId,
                           ackKey,
                           attempt.sessionUuid,
                           attempt.characterKey,
                           attempt.packetSequence,
                           attempt.ackDeadlineMs});
      }
      return expired;
    }

    [[nodiscard]] bool hasAttempt(std::string_view ackKey) const {
      return !ackKey.empty() && attempts.find(std::string(ackKey)) != attempts.end();
    }

    [[nodiscard]] std::optional<OutboundGameplayDeliveryStatus> attemptStatus(std::string_view ackKey) const {
      if(ackKey.empty())
        return std::nullopt;
      const auto it = attempts.find(std::string(ackKey));
      if(it == attempts.end())
        return std::nullopt;
      return it->second.status;
    }

    [[nodiscard]] OutboundGameplayDeliveryStats stats() const {
      OutboundGameplayDeliveryStats out = statsCounters;
      for(const auto& [_, attempt] : attempts) {
        switch(attempt.status) {
          case OutboundGameplayDeliveryStatus::Pending:  ++out.pending; break;
          case OutboundGameplayDeliveryStatus::Acked:    ++out.acked; break;
          case OutboundGameplayDeliveryStatus::Nacked:   ++out.nacked; break;
          case OutboundGameplayDeliveryStatus::TimedOut: ++out.timedOut; break;
        }
      }
      return out;
    }

  private:
    std::unordered_map<std::string, OutboundGameplayAttempt> attempts;
    OutboundGameplayDeliveryStats statsCounters;
};

} // namespace Mmo::Server


