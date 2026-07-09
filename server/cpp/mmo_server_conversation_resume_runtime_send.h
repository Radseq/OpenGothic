#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "mmo_server_conversation_resume_packet_boundary.h"
#include "mmo_server_conversation_resume_send_gate.h"

namespace Mmo::Server {

enum class ConversationResumeRuntimeSendStatus : std::uint8_t {
  Ready = 0,
  Disabled,
  PacketNotBuildable,
  SendGateNotEligible,
  MissingDurableStorage,
  MissingMysqlTarget,
  MissingEndpoint,
};

[[nodiscard]] constexpr const char* conversationResumeRuntimeSendStatusName(
    ConversationResumeRuntimeSendStatus status) noexcept {
  switch(status) {
    case ConversationResumeRuntimeSendStatus::Ready:               return "ready";
    case ConversationResumeRuntimeSendStatus::Disabled:            return "disabled";
    case ConversationResumeRuntimeSendStatus::PacketNotBuildable:  return "packet_not_buildable";
    case ConversationResumeRuntimeSendStatus::SendGateNotEligible: return "send_gate_not_eligible";
    case ConversationResumeRuntimeSendStatus::MissingDurableStorage: return "missing_durable_storage";
    case ConversationResumeRuntimeSendStatus::MissingMysqlTarget:   return "missing_mysql_target";
    case ConversationResumeRuntimeSendStatus::MissingEndpoint:      return "missing_endpoint";
  }
  return "unknown";
}

struct ConversationResumeRuntimeSendRequest final {
  const ConversationResumePacketBuildResult* packetBoundary = nullptr;
  const ConversationResumeSendGateResult* sendGate = nullptr;
  bool enabled = false;
  bool durableStorageEnabled = false;
  bool mysqlTargetAvailable = false;
  bool endpointAvailable = false;
};

struct ConversationResumeRuntimeSendResult final {
  ConversationResumeRuntimeSendStatus status = ConversationResumeRuntimeSendStatus::Disabled;
  bool ready = false;
  bool willSendUdp = false;
  bool willRegisterDelivery = false;
  bool willRegisterConversationObserver = false;
  bool willPersistSentStorage = false;
  bool requiresDurableSentStorage = true;
  std::string conversationId;
  std::string sessionUuid;
  std::string actionId;
  std::string ackKey;
  std::uint64_t packetSequence = 0;
  std::size_t encodedBytes = 0;
  std::string reason;
};

[[nodiscard]] inline ConversationResumeRuntimeSendResult buildConversationResumeRuntimeSendPreflight(
    const ConversationResumeRuntimeSendRequest& request) {
  ConversationResumeRuntimeSendResult out;

  if(!request.enabled) {
    out.status = ConversationResumeRuntimeSendStatus::Disabled;
    out.reason = "late_observer_resume_runtime_send_disabled";
    return out;
  }
  if(request.packetBoundary == nullptr || !request.packetBoundary->buildable) {
    out.status = ConversationResumeRuntimeSendStatus::PacketNotBuildable;
    out.reason = request.packetBoundary == nullptr ? "missing_resume_packet_boundary"
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

  if(request.sendGate == nullptr || !request.sendGate->eligible) {
    out.status = ConversationResumeRuntimeSendStatus::SendGateNotEligible;
    out.reason = request.sendGate == nullptr ? "missing_resume_send_gate" : request.sendGate->reason;
    return out;
  }
  if(!request.endpointAvailable) {
    out.status = ConversationResumeRuntimeSendStatus::MissingEndpoint;
    out.reason = "late_observer_resume_runtime_send_requires_udp_endpoint";
    return out;
  }
  if(!request.durableStorageEnabled) {
    out.status = ConversationResumeRuntimeSendStatus::MissingDurableStorage;
    out.reason = "late_observer_resume_runtime_send_requires_step281_storage_flag";
    return out;
  }
  if(!request.mysqlTargetAvailable) {
    out.status = ConversationResumeRuntimeSendStatus::MissingMysqlTarget;
    out.reason = "late_observer_resume_runtime_send_requires_mysql_target";
    return out;
  }

  out.status = ConversationResumeRuntimeSendStatus::Ready;
  out.ready = true;
  out.willSendUdp = true;
  out.willRegisterDelivery = true;
  out.willRegisterConversationObserver = true;
  out.willPersistSentStorage = true;
  out.reason = "late_observer_resume_runtime_send_ready";
  return out;
}

} // namespace Mmo::Server
