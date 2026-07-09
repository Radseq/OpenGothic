#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "../../game/game/mmonetprotocol.h"
#include "mmo_server_conversation_resume_delivery_registration_boundary.h"
#include "mmo_server_conversation_resume_packet_boundary.h"

namespace Mmo::Server {

enum class ConversationResumeDispatchEnvelopeStatus : std::uint8_t {
  EligibleNoDispatch = 0,
  EnvelopeDisabled,
  RegistrationNotEligible,
  PacketNotBuildable,
  RegistrationPacketMismatch,
  MissingObserverSession,
  MissingConversationId,
  MissingActionId,
  MissingAckKey,
  MissingEndpoint,
  MissingEndpointText,
  EncodeFailed,
  DatagramTooLarge,
  DuplicateEnvelopeActionId,
  DuplicateEnvelopeAckKey,
};

[[nodiscard]] constexpr const char* conversationResumeDispatchEnvelopeStatusName(
    ConversationResumeDispatchEnvelopeStatus status) noexcept {
  switch(status) {
    case ConversationResumeDispatchEnvelopeStatus::EligibleNoDispatch:        return "eligible_no_dispatch";
    case ConversationResumeDispatchEnvelopeStatus::EnvelopeDisabled:          return "envelope_disabled";
    case ConversationResumeDispatchEnvelopeStatus::RegistrationNotEligible:   return "registration_not_eligible";
    case ConversationResumeDispatchEnvelopeStatus::PacketNotBuildable:        return "packet_not_buildable";
    case ConversationResumeDispatchEnvelopeStatus::RegistrationPacketMismatch:return "registration_packet_mismatch";
    case ConversationResumeDispatchEnvelopeStatus::MissingObserverSession:    return "missing_observer_session";
    case ConversationResumeDispatchEnvelopeStatus::MissingConversationId:     return "missing_conversation_id";
    case ConversationResumeDispatchEnvelopeStatus::MissingActionId:           return "missing_action_id";
    case ConversationResumeDispatchEnvelopeStatus::MissingAckKey:             return "missing_ack_key";
    case ConversationResumeDispatchEnvelopeStatus::MissingEndpoint:           return "missing_endpoint";
    case ConversationResumeDispatchEnvelopeStatus::MissingEndpointText:       return "missing_endpoint_text";
    case ConversationResumeDispatchEnvelopeStatus::EncodeFailed:              return "encode_failed";
    case ConversationResumeDispatchEnvelopeStatus::DatagramTooLarge:          return "datagram_too_large";
    case ConversationResumeDispatchEnvelopeStatus::DuplicateEnvelopeActionId: return "duplicate_envelope_action_id";
    case ConversationResumeDispatchEnvelopeStatus::DuplicateEnvelopeAckKey:   return "duplicate_envelope_ack_key";
  }
  return "unknown";
}

struct ConversationResumeDispatchEnvelopeRequest final {
  const ConversationResumeDeliveryRegistrationResult* registrationBoundary = nullptr;
  const ConversationResumePacketBuildResult* packetBoundary = nullptr;
  bool envelopeEnabled = false;
  bool endpointAvailable = false;
  std::string endpointText;
  std::uint64_t nowMs = 0;
};

struct ConversationResumeDispatchEnvelopeResult final {
  ConversationResumeDispatchEnvelopeStatus status = ConversationResumeDispatchEnvelopeStatus::EnvelopeDisabled;
  bool eligible = false;
  bool duplicate = false;
  bool wouldDispatchUdp = false;
  bool wouldRegisterDelivery = false;
  bool wouldRegisterConversationObserver = false;
  bool wouldMutateRuntime = false;
  std::string conversationId;
  std::string sessionUuid;
  std::string characterKey;
  std::string actionId;
  std::string ackKey;
  std::string endpointText;
  std::uint64_t packetSequence = 0;
  std::uint64_t localSequence = 0;
  std::uint64_t serverTick = 0;
  std::uint64_t sentAtMs = 0;
  std::uint64_t ackDeadlineMs = 0;
  std::size_t encodedBytes = 0;
  std::vector<std::uint8_t> datagram;
  std::string reason;
};

struct ConversationResumeDispatchEnvelopeStats final {
  std::uint64_t requests = 0;
  std::uint64_t eligible = 0;
  std::uint64_t disabled = 0;
  std::uint64_t registrationNotEligible = 0;
  std::uint64_t packetNotBuildable = 0;
  std::uint64_t identityMismatch = 0;
  std::uint64_t missingIdentity = 0;
  std::uint64_t missingEndpoint = 0;
  std::uint64_t encodeFailed = 0;
  std::uint64_t datagramTooLarge = 0;
  std::uint64_t duplicateEnvelopeActionId = 0;
  std::uint64_t duplicateEnvelopeAckKey = 0;
};

class ConversationResumeDispatchEnvelope final {
  public:
    [[nodiscard]] ConversationResumeDispatchEnvelopeResult classifyNoDispatch(
        const ConversationResumeDispatchEnvelopeRequest& request) {
      ++counters.requests;

      ConversationResumeDispatchEnvelopeResult out;
      out.sentAtMs = request.nowMs;
      out.endpointText = request.endpointText;

      if(!request.envelopeEnabled) {
        ++counters.disabled;
        out.status = ConversationResumeDispatchEnvelopeStatus::EnvelopeDisabled;
        out.reason = "resume_dispatch_envelope_disabled";
        return out;
      }

      if(request.registrationBoundary == nullptr || !request.registrationBoundary->eligible) {
        ++counters.registrationNotEligible;
        out.status = ConversationResumeDispatchEnvelopeStatus::RegistrationNotEligible;
        out.reason = request.registrationBoundary == nullptr ? "missing_resume_delivery_registration_result"
                                                            : request.registrationBoundary->reason;
        return out;
      }

      if(request.packetBoundary == nullptr || !request.packetBoundary->buildable) {
        ++counters.packetNotBuildable;
        out.status = ConversationResumeDispatchEnvelopeStatus::PacketNotBuildable;
        out.reason = request.packetBoundary == nullptr ? "missing_resume_packet_boundary_result"
                                                       : request.packetBoundary->reason;
        return out;
      }

      const auto& registration = *request.registrationBoundary;
      const auto& packet = request.packetBoundary->packet;
      out.conversationId = packet.conversationId;
      out.sessionUuid = packet.sessionUuid;
      out.characterKey = packet.targetCharacterKey;
      out.actionId = packet.actionId;
      out.ackKey = packet.ackKey;
      out.packetSequence = packet.packetSequence;
      out.localSequence = packet.localSequence;
      out.serverTick = packet.serverTick;
      out.ackDeadlineMs = registration.ackDeadlineMs;
      out.wouldRegisterDelivery = registration.wouldRegisterDelivery;
      out.wouldRegisterConversationObserver = registration.wouldRegisterConversationObserver;

      if(packet.sessionUuid.empty()) {
        ++counters.missingIdentity;
        out.status = ConversationResumeDispatchEnvelopeStatus::MissingObserverSession;
        out.reason = "resume_dispatch_packet_has_no_observer_session_uuid";
        return out;
      }
      if(packet.conversationId.empty()) {
        ++counters.missingIdentity;
        out.status = ConversationResumeDispatchEnvelopeStatus::MissingConversationId;
        out.reason = "resume_dispatch_packet_has_no_conversation_id";
        return out;
      }
      if(packet.actionId.empty()) {
        ++counters.missingIdentity;
        out.status = ConversationResumeDispatchEnvelopeStatus::MissingActionId;
        out.reason = "resume_dispatch_packet_has_no_action_id";
        return out;
      }
      if(packet.ackKey.empty()) {
        ++counters.missingIdentity;
        out.status = ConversationResumeDispatchEnvelopeStatus::MissingAckKey;
        out.reason = "resume_dispatch_packet_has_no_ack_key";
        return out;
      }

      if(std::string_view(registration.conversationId) != std::string_view(packet.conversationId) ||
         std::string_view(registration.sessionUuid) != std::string_view(packet.sessionUuid) ||
         std::string_view(registration.actionId) != std::string_view(packet.actionId) ||
         std::string_view(registration.ackKey) != std::string_view(packet.ackKey)) {
        ++counters.identityMismatch;
        out.status = ConversationResumeDispatchEnvelopeStatus::RegistrationPacketMismatch;
        out.reason = "resume_delivery_registration_and_packet_identity_mismatch";
        return out;
      }

      if(!request.endpointAvailable) {
        ++counters.missingEndpoint;
        out.status = ConversationResumeDispatchEnvelopeStatus::MissingEndpoint;
        out.reason = "resume_dispatch_requires_active_udp_endpoint";
        return out;
      }
      if(request.endpointText.empty()) {
        ++counters.missingEndpoint;
        out.status = ConversationResumeDispatchEnvelopeStatus::MissingEndpointText;
        out.reason = "resume_dispatch_requires_endpoint_text_for_audit";
        return out;
      }

      auto datagram = Mmo::Net::encodeServerNpcDialogIntentPacket(packet);
      if(datagram.empty()) {
        ++counters.encodeFailed;
        out.status = ConversationResumeDispatchEnvelopeStatus::EncodeFailed;
        out.reason = "resume_dispatch_packet_encode_failed";
        return out;
      }
      if(datagram.size() > Mmo::Net::MaxDatagramBytes) {
        ++counters.datagramTooLarge;
        out.status = ConversationResumeDispatchEnvelopeStatus::DatagramTooLarge;
        out.encodedBytes = datagram.size();
        out.reason = "resume_dispatch_datagram_too_large";
        return out;
      }
      if(actionIds.contains(packet.actionId)) {
        ++counters.duplicateEnvelopeActionId;
        out.status = ConversationResumeDispatchEnvelopeStatus::DuplicateEnvelopeActionId;
        out.duplicate = true;
        out.encodedBytes = datagram.size();
        out.reason = "resume_action_id_already_reserved_by_dispatch_envelope";
        return out;
      }
      if(ackKeys.contains(packet.ackKey)) {
        ++counters.duplicateEnvelopeAckKey;
        out.status = ConversationResumeDispatchEnvelopeStatus::DuplicateEnvelopeAckKey;
        out.duplicate = true;
        out.encodedBytes = datagram.size();
        out.reason = "resume_ack_key_already_reserved_by_dispatch_envelope";
        return out;
      }

      actionIds.insert(packet.actionId);
      ackKeys.insert(packet.ackKey);
      ++counters.eligible;

      out.status = ConversationResumeDispatchEnvelopeStatus::EligibleNoDispatch;
      out.eligible = true;
      out.wouldDispatchUdp = true;
      out.wouldMutateRuntime = false;
      out.encodedBytes = datagram.size();
      out.datagram = std::move(datagram);
      out.reason = "resume_dispatch_envelope_eligible_no_dispatch";
      return out;
    }

    [[nodiscard]] ConversationResumeDispatchEnvelopeStats stats() const noexcept {
      return counters;
    }

  private:
    std::unordered_set<std::string> actionIds;
    std::unordered_set<std::string> ackKeys;
    ConversationResumeDispatchEnvelopeStats counters;
};

} // namespace Mmo::Server

