#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_set>

#include "../../game/game/mmonetprotocol.h"
#include "mmo_server_conversation_resume_packet_boundary.h"

namespace Mmo::Server {

enum class ConversationResumeSendGateStatus : std::uint8_t {
  EligibleNoSend = 0,
  GateDisabled,
  PacketNotBuildable,
  MissingObserverSession,
  MissingConversationId,
  MissingActionId,
  MissingAckKey,
  MissingRoute,
  RouteSessionMismatch,
  EncodedPacketTooLarge,
  DuplicateActionId,
  DuplicateAckKey,
};

[[nodiscard]] constexpr const char* conversationResumeSendGateStatusName(ConversationResumeSendGateStatus status) noexcept {
  switch(status) {
    case ConversationResumeSendGateStatus::EligibleNoSend:       return "eligible_no_send";
    case ConversationResumeSendGateStatus::GateDisabled:         return "gate_disabled";
    case ConversationResumeSendGateStatus::PacketNotBuildable:   return "packet_not_buildable";
    case ConversationResumeSendGateStatus::MissingObserverSession:return "missing_observer_session";
    case ConversationResumeSendGateStatus::MissingConversationId: return "missing_conversation_id";
    case ConversationResumeSendGateStatus::MissingActionId:       return "missing_action_id";
    case ConversationResumeSendGateStatus::MissingAckKey:         return "missing_ack_key";
    case ConversationResumeSendGateStatus::MissingRoute:          return "missing_route";
    case ConversationResumeSendGateStatus::RouteSessionMismatch:  return "route_session_mismatch";
    case ConversationResumeSendGateStatus::EncodedPacketTooLarge: return "encoded_packet_too_large";
    case ConversationResumeSendGateStatus::DuplicateActionId:     return "duplicate_action_id";
    case ConversationResumeSendGateStatus::DuplicateAckKey:       return "duplicate_ack_key";
  }
  return "unknown";
}

struct ConversationResumeSendGateRequest final {
  const ConversationResumePacketBuildResult* packetBoundary = nullptr;
  bool gateEnabled = false;
  bool endpointAvailable = false;
  std::string routeSessionUuid;
  std::uint64_t nowMs = 0;
};

struct ConversationResumeSendGateResult final {
  ConversationResumeSendGateStatus status = ConversationResumeSendGateStatus::GateDisabled;
  bool eligible = false;
  bool duplicate = false;
  bool wouldSendUdp = false;
  bool wouldRegisterDelivery = false;
  bool wouldRegisterConversationObserver = false;
  std::string conversationId;
  std::string sessionUuid;
  std::string actionId;
  std::string ackKey;
  std::uint64_t packetSequence = 0;
  std::uint64_t nowMs = 0;
  std::size_t encodedBytes = 0;
  std::string reason;
};

struct ConversationResumeSendGateStats final {
  std::uint64_t requests = 0;
  std::uint64_t eligible = 0;
  std::uint64_t disabled = 0;
  std::uint64_t packetNotBuildable = 0;
  std::uint64_t missingIdentity = 0;
  std::uint64_t missingRoute = 0;
  std::uint64_t routeMismatch = 0;
  std::uint64_t packetTooLarge = 0;
  std::uint64_t duplicateActionId = 0;
  std::uint64_t duplicateAckKey = 0;
};

class ConversationResumeSendGate final {
  public:
    [[nodiscard]] ConversationResumeSendGateResult classifyNoSend(const ConversationResumeSendGateRequest& request) {
      ++counters.requests;

      ConversationResumeSendGateResult out;
      out.nowMs = request.nowMs;

      if(!request.gateEnabled) {
        ++counters.disabled;
        out.status = ConversationResumeSendGateStatus::GateDisabled;
        out.reason = "resume_send_gate_disabled";
        return out;
      }

      if(request.packetBoundary == nullptr || !request.packetBoundary->buildable) {
        ++counters.packetNotBuildable;
        out.status = ConversationResumeSendGateStatus::PacketNotBuildable;
        out.reason = request.packetBoundary == nullptr ? "missing_resume_packet_boundary_result"
                                                       : request.packetBoundary->reason;
        return out;
      }

      const auto& packet = request.packetBoundary->packet;
      out.conversationId = packet.conversationId;
      out.sessionUuid = packet.sessionUuid;
      out.actionId = packet.actionId;
      out.ackKey = packet.ackKey;
      out.packetSequence = packet.packetSequence;
      out.encodedBytes = request.packetBoundary->encodedBytes;

      if(packet.sessionUuid.empty()) {
        ++counters.missingIdentity;
        out.status = ConversationResumeSendGateStatus::MissingObserverSession;
        out.reason = "resume_packet_has_no_observer_session_uuid";
        return out;
      }
      if(packet.conversationId.empty()) {
        ++counters.missingIdentity;
        out.status = ConversationResumeSendGateStatus::MissingConversationId;
        out.reason = "resume_packet_has_no_conversation_id";
        return out;
      }
      if(packet.actionId.empty()) {
        ++counters.missingIdentity;
        out.status = ConversationResumeSendGateStatus::MissingActionId;
        out.reason = "resume_packet_has_no_action_id";
        return out;
      }
      if(packet.ackKey.empty()) {
        ++counters.missingIdentity;
        out.status = ConversationResumeSendGateStatus::MissingAckKey;
        out.reason = "resume_packet_has_no_ack_key";
        return out;
      }
      if(!request.endpointAvailable) {
        ++counters.missingRoute;
        out.status = ConversationResumeSendGateStatus::MissingRoute;
        out.reason = "resume_observer_has_no_active_udp_route";
        return out;
      }
      if(!request.routeSessionUuid.empty() && std::string_view(request.routeSessionUuid) != std::string_view(packet.sessionUuid)) {
        ++counters.routeMismatch;
        out.status = ConversationResumeSendGateStatus::RouteSessionMismatch;
        out.reason = "resume_udp_route_session_mismatch";
        return out;
      }
      if(request.packetBoundary->encodedBytes == 0 || request.packetBoundary->encodedBytes > Mmo::Net::MaxDatagramBytes) {
        ++counters.packetTooLarge;
        out.status = ConversationResumeSendGateStatus::EncodedPacketTooLarge;
        out.reason = "resume_packet_encoded_size_not_sendable";
        return out;
      }
      if(actionIds.contains(packet.actionId)) {
        ++counters.duplicateActionId;
        out.status = ConversationResumeSendGateStatus::DuplicateActionId;
        out.duplicate = true;
        out.reason = "resume_action_id_already_reserved_by_send_gate";
        return out;
      }
      if(ackKeys.contains(packet.ackKey)) {
        ++counters.duplicateAckKey;
        out.status = ConversationResumeSendGateStatus::DuplicateAckKey;
        out.duplicate = true;
        out.reason = "resume_ack_key_already_reserved_by_send_gate";
        return out;
      }

      actionIds.insert(packet.actionId);
      ackKeys.insert(packet.ackKey);
      ++counters.eligible;

      out.status = ConversationResumeSendGateStatus::EligibleNoSend;
      out.eligible = true;
      out.wouldSendUdp = true;
      out.wouldRegisterDelivery = true;
      out.wouldRegisterConversationObserver = true;
      out.reason = "resume_send_gate_eligible_no_send";
      return out;
    }

    [[nodiscard]] ConversationResumeSendGateStats stats() const noexcept {
      return counters;
    }

  private:
    std::unordered_set<std::string> actionIds;
    std::unordered_set<std::string> ackKeys;
    ConversationResumeSendGateStats counters;
};

} // namespace Mmo::Server
