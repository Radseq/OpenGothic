#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Mmo::Server {

enum class ConversationObserverStatus : std::uint8_t {
  Pending = 0,
  Acked,
  Nacked,
  TimedOut,
};

[[nodiscard]] constexpr const char* conversationObserverStatusName(ConversationObserverStatus status) noexcept {
  switch(status) {
    case ConversationObserverStatus::Pending:  return "pending";
    case ConversationObserverStatus::Acked:    return "acked";
    case ConversationObserverStatus::Nacked:   return "nacked";
    case ConversationObserverStatus::TimedOut: return "timed_out";
  }
  return "unknown";
}

enum class ConversationSessionStatus : std::uint8_t {
  Open = 0,
  PartiallyAcked,
  Acked,
  Nacked,
  TimedOut,
  MixedTerminal,
};

[[nodiscard]] constexpr const char* conversationSessionStatusName(ConversationSessionStatus status) noexcept {
  switch(status) {
    case ConversationSessionStatus::Open:           return "open";
    case ConversationSessionStatus::PartiallyAcked: return "partially_acked";
    case ConversationSessionStatus::Acked:          return "acked";
    case ConversationSessionStatus::Nacked:         return "nacked";
    case ConversationSessionStatus::TimedOut:       return "timed_out";
    case ConversationSessionStatus::MixedTerminal:  return "mixed_terminal";
  }
  return "unknown";
}



enum class ConversationLateObserverResumeStatus : std::uint8_t {
  Planned = 0,
  MissingObserverSession,
  WorldMismatch,
  AlreadyObserver,
  AlreadyPlanned,
  ConversationTerminal,
  LineExpired,
  NoRemainingDuration,
};

[[nodiscard]] constexpr const char* conversationLateObserverResumeStatusName(ConversationLateObserverResumeStatus status) noexcept {
  switch(status) {
    case ConversationLateObserverResumeStatus::Planned:                return "planned";
    case ConversationLateObserverResumeStatus::MissingObserverSession: return "missing_observer_session";
    case ConversationLateObserverResumeStatus::WorldMismatch:          return "world_mismatch";
    case ConversationLateObserverResumeStatus::AlreadyObserver:        return "already_observer";
    case ConversationLateObserverResumeStatus::AlreadyPlanned:         return "already_planned";
    case ConversationLateObserverResumeStatus::ConversationTerminal:   return "conversation_terminal";
    case ConversationLateObserverResumeStatus::LineExpired:            return "line_expired";
    case ConversationLateObserverResumeStatus::NoRemainingDuration:    return "no_remaining_duration";
  }
  return "unknown";
}

enum class ConversationReceiptKind : std::uint8_t {
  Acked = 0,
  Nacked,
  Duplicate,
  UnknownAckKey,
  ConflictingActionId,
  ConflictingTerminalStatus,
  InvalidReceipt,
  LateAfterTimeout,
};

[[nodiscard]] constexpr const char* conversationReceiptKindName(ConversationReceiptKind kind) noexcept {
  switch(kind) {
    case ConversationReceiptKind::Acked:                     return "acked";
    case ConversationReceiptKind::Nacked:                    return "nacked";
    case ConversationReceiptKind::Duplicate:                 return "duplicate";
    case ConversationReceiptKind::UnknownAckKey:             return "unknown_ack_key";
    case ConversationReceiptKind::ConflictingActionId:       return "conflicting_action_id";
    case ConversationReceiptKind::ConflictingTerminalStatus: return "conflicting_terminal_status";
    case ConversationReceiptKind::InvalidReceipt:            return "invalid_receipt";
    case ConversationReceiptKind::LateAfterTimeout:          return "late_after_timeout";
  }
  return "unknown";
}


enum class ConversationObservationStatus : std::uint8_t {
  None = 0,
  Observed,
  Skipped,
};

[[nodiscard]] constexpr const char* conversationObservationStatusName(ConversationObservationStatus status) noexcept {
  switch(status) {
    case ConversationObservationStatus::None:     return "none";
    case ConversationObservationStatus::Observed: return "observed";
    case ConversationObservationStatus::Skipped:  return "skipped";
  }
  return "unknown";
}

enum class ConversationObservationReceiptKind : std::uint8_t {
  Observed = 0,
  Skipped,
  Duplicate,
  UnknownAckKey,
  ConflictingActionId,
  ConflictingObservationStatus,
  InvalidReceipt,
  LateAfterTimeout,
};

[[nodiscard]] constexpr const char* conversationObservationReceiptKindName(ConversationObservationReceiptKind kind) noexcept {
  switch(kind) {
    case ConversationObservationReceiptKind::Observed:                     return "observed";
    case ConversationObservationReceiptKind::Skipped:                      return "skipped";
    case ConversationObservationReceiptKind::Duplicate:                    return "duplicate";
    case ConversationObservationReceiptKind::UnknownAckKey:                return "unknown_ack_key";
    case ConversationObservationReceiptKind::ConflictingActionId:          return "conflicting_action_id";
    case ConversationObservationReceiptKind::ConflictingObservationStatus: return "conflicting_observation_status";
    case ConversationObservationReceiptKind::InvalidReceipt:               return "invalid_receipt";
    case ConversationObservationReceiptKind::LateAfterTimeout:             return "late_after_timeout";
  }
  return "unknown";
}

struct ConversationLineStart final {
  std::string conversationId;
  std::string worldName;
  std::string worldInstanceUuid;
  std::string speakerEntityKey;
  std::string speakerNpcInstanceUuid;
  std::string lineId;
  std::string audioRef;
  std::uint64_t serverTick = 0;
  std::uint64_t startTick = 0;
  std::uint32_t durationMs = 0;
  std::size_t plannedRecipients = 0;
};

struct ConversationObserverSent final {
  std::string sessionUuid;
  std::string characterKey;
  std::string actionId;
  std::string ackKey;
  std::uint64_t packetSequence = 0;
  std::uint64_t localSequence = 0;
  std::uint64_t sentAtMs = 0;
  std::uint64_t ackDeadlineMs = 0;
  double distanceSquared = 0.0;
  bool target = false;
  bool hasPosition = false;
};

struct ConversationObserverState final {
  ConversationObserverSent sent;
  ConversationObserverStatus status = ConversationObserverStatus::Pending;
  ConversationObservationStatus observationStatus = ConversationObservationStatus::None;
  std::string terminalReason;
  std::string terminalMessage;
  std::string observationReason;
  std::string observationMessage;
  std::uint64_t observedAtMs = 0;
  bool observationUiApplied = false;
  bool observationAudioApplied = false;
};

struct ConversationSessionState final {
  ConversationLineStart line;
  std::vector<ConversationObserverState> observers;
  ConversationSessionStatus status = ConversationSessionStatus::Open;
  std::uint64_t openedAtMs = 0;
  std::uint64_t terminalAtMs = 0;
};

struct ConversationStartResult final {
  bool accepted = false;
  const char* state = "invalid";
  std::size_t conversationCount = 0;
};

struct ConversationObserverRegisterResult final {
  bool accepted = false;
  const char* state = "invalid";
  ConversationSessionStatus sessionStatus = ConversationSessionStatus::Open;
  std::size_t observerCount = 0;
};

struct ConversationReceiptResult final {
  ConversationReceiptKind kind = ConversationReceiptKind::InvalidReceipt;
  ConversationObserverStatus previousObserverStatus = ConversationObserverStatus::Pending;
  ConversationObserverStatus currentObserverStatus = ConversationObserverStatus::Pending;
  ConversationSessionStatus previousSessionStatus = ConversationSessionStatus::Open;
  ConversationSessionStatus currentSessionStatus = ConversationSessionStatus::Open;
  std::string conversationId;
  std::string sessionUuid;
  std::string actionId;
  std::string ackKey;
  std::string reason;
};

struct ConversationObservationReceiptResult final {
  ConversationObservationReceiptKind kind = ConversationObservationReceiptKind::InvalidReceipt;
  ConversationObservationStatus previousObservationStatus = ConversationObservationStatus::None;
  ConversationObservationStatus currentObservationStatus = ConversationObservationStatus::None;
  ConversationObserverStatus transportObserverStatus = ConversationObserverStatus::Pending;
  ConversationSessionStatus sessionStatus = ConversationSessionStatus::Open;
  std::string conversationId;
  std::string sessionUuid;
  std::string actionId;
  std::string ackKey;
  std::string reason;
  bool uiApplied = false;
  bool audioApplied = false;
};

struct ConversationExpiredObserver final {
  std::string conversationId;
  std::string sessionUuid;
  std::string characterKey;
  std::string actionId;
  std::string ackKey;
  std::uint64_t packetSequence = 0;
  std::uint64_t deadlineMs = 0;
  ConversationSessionStatus sessionStatus = ConversationSessionStatus::Open;
};


struct ConversationLateObserverResumeRequest final {
  std::string sessionUuid;
  std::string characterKey;
  std::string worldName;
  std::uint64_t nowMs = 0;
  std::uint64_t serverTick = 0;
};

struct ConversationLateObserverResumePlan final {
  ConversationLateObserverResumeStatus status = ConversationLateObserverResumeStatus::MissingObserverSession;
  bool canResume = false;
  std::string conversationId;
  std::string sessionUuid;
  std::string characterKey;
  std::string worldName;
  std::string speakerEntityKey;
  std::string speakerNpcInstanceUuid;
  std::string lineId;
  std::string audioRef;
  std::uint64_t lineStartTick = 0;
  std::uint64_t observerServerTick = 0;
  std::uint64_t elapsedMs = 0;
  std::uint64_t remainingMs = 0;
  std::uint32_t durationMs = 0;
  std::size_t knownObservers = 0;
};

struct ConversationRuntimeStats final {
  std::uint64_t sessions = 0;
  std::uint64_t observers = 0;
  std::uint64_t pendingObservers = 0;
  std::uint64_t ackedObservers = 0;
  std::uint64_t nackedObservers = 0;
  std::uint64_t timedOutObservers = 0;
  std::uint64_t unknownReceipts = 0;
  std::uint64_t duplicateReceipts = 0;
  std::uint64_t conflictingReceipts = 0;
  std::uint64_t invalidReceipts = 0;
  std::uint64_t lateReceipts = 0;
  std::uint64_t lateResumePlans = 0;
  std::uint64_t lateResumeSkippedAlreadyObserver = 0;
  std::uint64_t lateResumeSkippedAlreadyPlanned = 0;
  std::uint64_t lateResumeSkippedWorldMismatch = 0;
  std::uint64_t lateResumeSkippedExpired = 0;
  std::uint64_t observationReceipts = 0;
  std::uint64_t observedReceipts = 0;
  std::uint64_t skippedObservationReceipts = 0;
  std::uint64_t duplicateObservationReceipts = 0;
  std::uint64_t unknownObservationReceipts = 0;
  std::uint64_t conflictingObservationReceipts = 0;
  std::uint64_t invalidObservationReceipts = 0;
  std::uint64_t lateObservationReceipts = 0;
};

class ConversationSessionRuntime final {
  public:
    [[nodiscard]] ConversationStartResult startLine(ConversationLineStart line, std::uint64_t openedAtMs) {
      if(line.conversationId.empty())
        return {false, "missing_conversation_id", sessions.size()};

      auto [it, inserted] = sessions.emplace(line.conversationId, ConversationSessionState{});
      if(!inserted) {
        auto& existing = it->second;
        if(existing.status == ConversationSessionStatus::Open || existing.status == ConversationSessionStatus::PartiallyAcked)
          return {true, "already_open", sessions.size()};
        return {false, "already_terminal", sessions.size()};
      }

      auto& session = it->second;
      session.line = std::move(line);
      session.openedAtMs = openedAtMs;
      session.status = ConversationSessionStatus::Open;
      return {true, "started", sessions.size()};
    }

    [[nodiscard]] ConversationObserverRegisterResult recordObserverSent(std::string_view conversationId,
                                                                        ConversationObserverSent observer) {
      if(conversationId.empty() || observer.actionId.empty() || observer.ackKey.empty())
        return {false, "missing_identity", ConversationSessionStatus::Open, 0};

      const auto sessionIt = sessions.find(std::string(conversationId));
      if(sessionIt == sessions.end())
        return {false, "unknown_conversation", ConversationSessionStatus::Open, 0};

      auto& session = sessionIt->second;
      const auto foundAck = ackKeyIndex.find(observer.ackKey);
      if(foundAck != ackKeyIndex.end())
        return {false, "duplicate_ack_key", session.status, session.observers.size()};

      const std::size_t index = session.observers.size();
      const std::string ackKey = observer.ackKey;
      session.observers.push_back({std::move(observer), ConversationObserverStatus::Pending, {}, {}});
      ackKeyIndex.emplace(ackKey, AckIndex{std::string(conversationId), index});
      session.status = recomputeSessionStatus(session);
      return {true, "observer_sent", session.status, session.observers.size()};
    }

    [[nodiscard]] ConversationReceiptResult recordReceipt(std::string_view actionId,
                                                          std::string_view ackKey,
                                                          bool acked,
                                                          std::string_view reason,
                                                          std::string_view message,
                                                          std::uint64_t terminalAtMs) {
      if(actionId.empty() || ackKey.empty()) {
        ++statsCounters.invalidReceipts;
        ConversationReceiptResult out;
        out.kind = ConversationReceiptKind::InvalidReceipt;
        out.reason = "missing_action_or_ack_key";
        return out;
      }

      const auto ackIt = ackKeyIndex.find(std::string(ackKey));
      if(ackIt == ackKeyIndex.end()) {
        ++statsCounters.unknownReceipts;
        ConversationReceiptResult out;
        out.kind = ConversationReceiptKind::UnknownAckKey;
        out.actionId = std::string(actionId);
        out.ackKey = std::string(ackKey);
        out.reason = "ack_key_not_known_to_conversation_runtime";
        return out;
      }

      auto sessionIt = sessions.find(ackIt->second.conversationId);
      if(sessionIt == sessions.end()) {
        ++statsCounters.invalidReceipts;
        ConversationReceiptResult out;
        out.kind = ConversationReceiptKind::InvalidReceipt;
        out.actionId = std::string(actionId);
        out.ackKey = std::string(ackKey);
        out.reason = "conversation_index_corrupt";
        return out;
      }

      auto& session = sessionIt->second;
      if(ackIt->second.observerIndex >= session.observers.size()) {
        ++statsCounters.invalidReceipts;
        ConversationReceiptResult out;
        out.kind = ConversationReceiptKind::InvalidReceipt;
        out.conversationId = session.line.conversationId;
        out.actionId = std::string(actionId);
        out.ackKey = std::string(ackKey);
        out.reason = "observer_index_corrupt";
        return out;
      }

      auto& observer = session.observers[ackIt->second.observerIndex];
      ConversationReceiptResult out;
      out.previousObserverStatus = observer.status;
      out.currentObserverStatus = observer.status;
      out.previousSessionStatus = session.status;
      out.currentSessionStatus = session.status;
      out.conversationId = session.line.conversationId;
      out.sessionUuid = observer.sent.sessionUuid;
      out.actionId = observer.sent.actionId;
      out.ackKey = observer.sent.ackKey;

      if(std::string_view(observer.sent.actionId) != actionId) {
        ++statsCounters.conflictingReceipts;
        out.kind = ConversationReceiptKind::ConflictingActionId;
        out.reason = "action_id_mismatch";
        return out;
      }

      const auto requested = acked ? ConversationObserverStatus::Acked : ConversationObserverStatus::Nacked;
      if(observer.status == ConversationObserverStatus::TimedOut) {
        ++statsCounters.lateReceipts;
        observer.terminalReason = reason.empty() ? std::string("late_after_timeout") : std::string(reason);
        observer.terminalMessage = std::string(message);
        out.kind = ConversationReceiptKind::LateAfterTimeout;
        out.reason = "observer_already_timed_out";
        return out;
      }

      if(observer.status == requested) {
        ++statsCounters.duplicateReceipts;
        observer.terminalReason = reason.empty() ? observer.terminalReason : std::string(reason);
        observer.terminalMessage = message.empty() ? observer.terminalMessage : std::string(message);
        out.kind = ConversationReceiptKind::Duplicate;
        out.reason = "observer_terminal_status_replayed";
        return out;
      }

      if(observer.status != ConversationObserverStatus::Pending) {
        ++statsCounters.conflictingReceipts;
        out.kind = ConversationReceiptKind::ConflictingTerminalStatus;
        out.reason = "observer_terminal_status_conflict";
        return out;
      }

      observer.status = requested;
      observer.terminalReason = std::string(reason);
      observer.terminalMessage = std::string(message);
      out.currentObserverStatus = observer.status;
      session.status = recomputeSessionStatus(session);
      out.currentSessionStatus = session.status;
      if(isConversationSessionTerminal(session.status) && session.terminalAtMs == 0)
        session.terminalAtMs = terminalAtMs;
      out.kind = acked ? ConversationReceiptKind::Acked : ConversationReceiptKind::Nacked;
      out.reason = acked ? "observer_ack_recorded" : "observer_nack_recorded";
      return out;
    }

    [[nodiscard]] ConversationObservationReceiptResult recordObservation(std::string_view actionId,
                                                                          std::string_view ackKey,
                                                                          bool observed,
                                                                          bool uiApplied,
                                                                          bool audioApplied,
                                                                          std::string_view reason,
                                                                          std::string_view message,
                                                                          std::uint64_t observedAtMs) {
      ++statsCounters.observationReceipts;
      if(actionId.empty() || ackKey.empty()) {
        ++statsCounters.invalidObservationReceipts;
        ConversationObservationReceiptResult out;
        out.kind = ConversationObservationReceiptKind::InvalidReceipt;
        out.reason = "missing_action_or_ack_key";
        return out;
      }

      const auto ackIt = ackKeyIndex.find(std::string(ackKey));
      if(ackIt == ackKeyIndex.end()) {
        ++statsCounters.unknownObservationReceipts;
        ConversationObservationReceiptResult out;
        out.kind = ConversationObservationReceiptKind::UnknownAckKey;
        out.actionId = std::string(actionId);
        out.ackKey = std::string(ackKey);
        out.reason = "ack_key_not_known_to_conversation_runtime";
        return out;
      }

      auto sessionIt = sessions.find(ackIt->second.conversationId);
      if(sessionIt == sessions.end()) {
        ++statsCounters.invalidObservationReceipts;
        ConversationObservationReceiptResult out;
        out.kind = ConversationObservationReceiptKind::InvalidReceipt;
        out.actionId = std::string(actionId);
        out.ackKey = std::string(ackKey);
        out.reason = "conversation_index_corrupt";
        return out;
      }

      auto& session = sessionIt->second;
      if(ackIt->second.observerIndex >= session.observers.size()) {
        ++statsCounters.invalidObservationReceipts;
        ConversationObservationReceiptResult out;
        out.kind = ConversationObservationReceiptKind::InvalidReceipt;
        out.conversationId = session.line.conversationId;
        out.actionId = std::string(actionId);
        out.ackKey = std::string(ackKey);
        out.reason = "observer_index_corrupt";
        return out;
      }

      auto& observer = session.observers[ackIt->second.observerIndex];
      ConversationObservationReceiptResult out;
      out.previousObservationStatus = observer.observationStatus;
      out.currentObservationStatus = observer.observationStatus;
      out.transportObserverStatus = observer.status;
      out.sessionStatus = session.status;
      out.conversationId = session.line.conversationId;
      out.sessionUuid = observer.sent.sessionUuid;
      out.actionId = observer.sent.actionId;
      out.ackKey = observer.sent.ackKey;
      out.uiApplied = uiApplied;
      out.audioApplied = audioApplied;

      if(std::string_view(observer.sent.actionId) != actionId) {
        ++statsCounters.conflictingObservationReceipts;
        out.kind = ConversationObservationReceiptKind::ConflictingActionId;
        out.reason = "action_id_mismatch";
        return out;
      }

      const auto requested = observed ? ConversationObservationStatus::Observed : ConversationObservationStatus::Skipped;
      if(observer.status == ConversationObserverStatus::TimedOut) {
        ++statsCounters.lateObservationReceipts;
        observer.observationStatus = requested;
        observer.observationReason = reason.empty() ? std::string("late_observation_after_transport_timeout") : std::string(reason);
        observer.observationMessage = std::string(message);
        observer.observedAtMs = observedAtMs;
        observer.observationUiApplied = uiApplied;
        observer.observationAudioApplied = audioApplied;
        out.currentObservationStatus = observer.observationStatus;
        out.kind = ConversationObservationReceiptKind::LateAfterTimeout;
        out.reason = "observer_transport_already_timed_out";
        return out;
      }

      if(observer.observationStatus == requested) {
        ++statsCounters.duplicateObservationReceipts;
        observer.observationReason = reason.empty() ? observer.observationReason : std::string(reason);
        observer.observationMessage = message.empty() ? observer.observationMessage : std::string(message);
        observer.observedAtMs = observedAtMs == 0 ? observer.observedAtMs : observedAtMs;
        observer.observationUiApplied = observer.observationUiApplied || uiApplied;
        observer.observationAudioApplied = observer.observationAudioApplied || audioApplied;
        out.currentObservationStatus = observer.observationStatus;
        out.uiApplied = observer.observationUiApplied;
        out.audioApplied = observer.observationAudioApplied;
        out.kind = ConversationObservationReceiptKind::Duplicate;
        out.reason = "observer_main_thread_observation_replayed";
        return out;
      }

      if(observer.observationStatus != ConversationObservationStatus::None) {
        ++statsCounters.conflictingObservationReceipts;
        out.kind = ConversationObservationReceiptKind::ConflictingObservationStatus;
        out.reason = "observer_main_thread_observation_conflict";
        return out;
      }

      observer.observationStatus = requested;
      observer.observationReason = std::string(reason);
      observer.observationMessage = std::string(message);
      observer.observedAtMs = observedAtMs;
      observer.observationUiApplied = uiApplied;
      observer.observationAudioApplied = audioApplied;
      out.currentObservationStatus = observer.observationStatus;
      out.kind = observed ? ConversationObservationReceiptKind::Observed : ConversationObservationReceiptKind::Skipped;
      out.reason = observed ? "observer_main_thread_observed" : "observer_main_thread_skipped";
      if(observed)
        ++statsCounters.observedReceipts;
      else
        ++statsCounters.skippedObservationReceipts;
      return out;
    }

    [[nodiscard]] std::vector<ConversationLateObserverResumePlan> planLateObserverResume(ConversationLateObserverResumeRequest request) {
      std::vector<ConversationLateObserverResumePlan> plans;
      if(request.sessionUuid.empty()) {
        ConversationLateObserverResumePlan out;
        out.status = ConversationLateObserverResumeStatus::MissingObserverSession;
        plans.push_back(std::move(out));
        return plans;
      }

      for(auto& [conversationId, session] : sessions) {
        auto plan = makeLateObserverResumePlan(session, request);
        plan.conversationId = conversationId;
        plan.sessionUuid = request.sessionUuid;
        plan.characterKey = request.characterKey;
        if(plan.status != ConversationLateObserverResumeStatus::Planned) {
          countLateResumeSkip(plan.status);
          continue;
        }

        const auto key = lateResumePlanKey(conversationId, request.sessionUuid);
        if(!lateResumePlanIndex.insert(key).second) {
          ++statsCounters.lateResumeSkippedAlreadyPlanned;
          continue;
        }

        ++statsCounters.lateResumePlans;
        plans.push_back(std::move(plan));
      }
      return plans;
    }

    [[nodiscard]] std::vector<ConversationExpiredObserver> expirePending(std::uint64_t nowMs) {
      std::vector<ConversationExpiredObserver> expired;
      for(auto& [conversationId, session] : sessions) {
        bool changed = false;
        for(auto& observer : session.observers) {
          if(observer.status != ConversationObserverStatus::Pending)
            continue;
          if(observer.sent.ackDeadlineMs == 0 || observer.sent.ackDeadlineMs > nowMs)
            continue;
          observer.status = ConversationObserverStatus::TimedOut;
          observer.terminalReason = "conversation_observer_ack_timeout_no_durable_retry_yet";
          expired.push_back({conversationId,
                             observer.sent.sessionUuid,
                             observer.sent.characterKey,
                             observer.sent.actionId,
                             observer.sent.ackKey,
                             observer.sent.packetSequence,
                             observer.sent.ackDeadlineMs,
                             session.status});
          changed = true;
        }
        if(changed) {
          session.status = recomputeSessionStatus(session);
          if(isConversationSessionTerminal(session.status) && session.terminalAtMs == 0)
            session.terminalAtMs = nowMs;
          for(auto& row : expired) {
            if(row.conversationId == conversationId)
              row.sessionStatus = session.status;
          }
        }
      }
      return expired;
    }

    [[nodiscard]] bool hasConversation(std::string_view conversationId) const {
      return !conversationId.empty() && sessions.find(std::string(conversationId)) != sessions.end();
    }

    [[nodiscard]] bool hasObserverAckKey(std::string_view ackKey) const {
      return !ackKey.empty() && ackKeyIndex.find(std::string(ackKey)) != ackKeyIndex.end();
    }

    [[nodiscard]] bool hasObserverSession(std::string_view conversationId, std::string_view sessionUuid) const {
      if(conversationId.empty() || sessionUuid.empty())
        return false;
      const auto it = sessions.find(std::string(conversationId));
      if(it == sessions.end())
        return false;
      return hasObserverForSession(it->second, sessionUuid);
    }

    [[nodiscard]] ConversationRuntimeStats stats() const {
      ConversationRuntimeStats out = statsCounters;
      out.sessions = sessions.size();
      for(const auto& [_, session] : sessions) {
        for(const auto& observer : session.observers) {
          ++out.observers;
          switch(observer.status) {
            case ConversationObserverStatus::Pending:  ++out.pendingObservers; break;
            case ConversationObserverStatus::Acked:    ++out.ackedObservers; break;
            case ConversationObserverStatus::Nacked:   ++out.nackedObservers; break;
            case ConversationObserverStatus::TimedOut: ++out.timedOutObservers; break;
          }
        }
      }
      return out;
    }

  private:
    struct AckIndex final {
      std::string conversationId;
      std::size_t observerIndex = 0;
    };

    [[nodiscard]] static bool isConversationSessionTerminal(ConversationSessionStatus status) noexcept {
      return status == ConversationSessionStatus::Acked ||
             status == ConversationSessionStatus::Nacked ||
             status == ConversationSessionStatus::TimedOut ||
             status == ConversationSessionStatus::MixedTerminal;
    }


    [[nodiscard]] static bool isConversationLineResumable(ConversationSessionStatus status) noexcept {
      return status == ConversationSessionStatus::Open ||
             status == ConversationSessionStatus::PartiallyAcked ||
             status == ConversationSessionStatus::Acked ||
             status == ConversationSessionStatus::MixedTerminal;
    }

    [[nodiscard]] static ConversationSessionStatus recomputeSessionStatus(const ConversationSessionState& session) noexcept {
      if(session.observers.empty())
        return ConversationSessionStatus::Open;

      std::size_t pending = 0;
      std::size_t acked = 0;
      std::size_t nacked = 0;
      std::size_t timedOut = 0;
      for(const auto& observer : session.observers) {
        switch(observer.status) {
          case ConversationObserverStatus::Pending:  ++pending; break;
          case ConversationObserverStatus::Acked:    ++acked; break;
          case ConversationObserverStatus::Nacked:   ++nacked; break;
          case ConversationObserverStatus::TimedOut: ++timedOut; break;
        }
      }

      if(pending != 0)
        return acked == 0 && nacked == 0 && timedOut == 0 ? ConversationSessionStatus::Open : ConversationSessionStatus::PartiallyAcked;
      if(acked == session.observers.size())
        return ConversationSessionStatus::Acked;
      if(nacked == session.observers.size())
        return ConversationSessionStatus::Nacked;
      if(timedOut == session.observers.size())
        return ConversationSessionStatus::TimedOut;
      return ConversationSessionStatus::MixedTerminal;
    }

    [[nodiscard]] static std::string lateResumePlanKey(std::string_view conversationId, std::string_view sessionUuid) {
      std::string out;
      out.reserve(conversationId.size() + sessionUuid.size() + 1);
      out.append(conversationId);
      out.push_back('\n');
      out.append(sessionUuid);
      return out;
    }

    [[nodiscard]] static bool sameWorldOrUnspecified(std::string_view lineWorld, std::string_view observerWorld) noexcept {
      return lineWorld.empty() || observerWorld.empty() || lineWorld == observerWorld;
    }

    [[nodiscard]] static bool hasObserverForSession(const ConversationSessionState& session, std::string_view sessionUuid) noexcept {
      return std::any_of(session.observers.begin(), session.observers.end(), [sessionUuid](const ConversationObserverState& observer) {
        return std::string_view(observer.sent.sessionUuid) == sessionUuid;
      });
    }

    [[nodiscard]] static ConversationLateObserverResumePlan makeLateObserverResumePlan(const ConversationSessionState& session,
                                                                                       const ConversationLateObserverResumeRequest& request) noexcept {
      ConversationLateObserverResumePlan out;
      out.worldName = session.line.worldName;
      out.speakerEntityKey = session.line.speakerEntityKey;
      out.speakerNpcInstanceUuid = session.line.speakerNpcInstanceUuid;
      out.lineId = session.line.lineId;
      out.audioRef = session.line.audioRef;
      out.lineStartTick = session.line.startTick;
      out.observerServerTick = request.serverTick;
      out.durationMs = session.line.durationMs;
      out.knownObservers = session.observers.size();

      if(!sameWorldOrUnspecified(session.line.worldName, request.worldName)) {
        out.status = ConversationLateObserverResumeStatus::WorldMismatch;
        return out;
      }
      if(hasObserverForSession(session, request.sessionUuid)) {
        out.status = ConversationLateObserverResumeStatus::AlreadyObserver;
        return out;
      }
      if(!isConversationLineResumable(session.status)) {
        out.status = ConversationLateObserverResumeStatus::ConversationTerminal;
        return out;
      }
      if(session.line.durationMs == 0) {
        out.status = ConversationLateObserverResumeStatus::NoRemainingDuration;
        return out;
      }

      const auto openedAtMs = session.openedAtMs == 0 ? request.nowMs : session.openedAtMs;
      out.elapsedMs = request.nowMs > openedAtMs ? request.nowMs - openedAtMs : 0;
      if(out.elapsedMs >= session.line.durationMs) {
        out.status = ConversationLateObserverResumeStatus::LineExpired;
        return out;
      }

      out.remainingMs = static_cast<std::uint64_t>(session.line.durationMs) - out.elapsedMs;
      if(out.remainingMs == 0) {
        out.status = ConversationLateObserverResumeStatus::NoRemainingDuration;
        return out;
      }

      out.status = ConversationLateObserverResumeStatus::Planned;
      out.canResume = true;
      return out;
    }

    void countLateResumeSkip(ConversationLateObserverResumeStatus status) noexcept {
      switch(status) {
        case ConversationLateObserverResumeStatus::AlreadyObserver:
          ++statsCounters.lateResumeSkippedAlreadyObserver;
          break;
        case ConversationLateObserverResumeStatus::AlreadyPlanned:
          ++statsCounters.lateResumeSkippedAlreadyPlanned;
          break;
        case ConversationLateObserverResumeStatus::WorldMismatch:
          ++statsCounters.lateResumeSkippedWorldMismatch;
          break;
        case ConversationLateObserverResumeStatus::LineExpired:
        case ConversationLateObserverResumeStatus::NoRemainingDuration:
        case ConversationLateObserverResumeStatus::ConversationTerminal:
          ++statsCounters.lateResumeSkippedExpired;
          break;
        case ConversationLateObserverResumeStatus::Planned:
        case ConversationLateObserverResumeStatus::MissingObserverSession:
          break;
      }
    }

    std::unordered_map<std::string, ConversationSessionState> sessions;
    std::unordered_map<std::string, AckIndex> ackKeyIndex;
    std::unordered_set<std::string> lateResumePlanIndex;
    ConversationRuntimeStats statsCounters;
};

} // namespace Mmo::Server






