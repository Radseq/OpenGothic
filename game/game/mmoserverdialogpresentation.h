#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

#include "mmonetprotocol.h"

namespace Mmo {

struct ServerDialogPresentationConfig final {
  bool          validateOnly = false;
  bool          requireSpeakerEntityKey = true;
  bool          requireTextOrAudio = true;
  bool          requireLineIdentity = false;
  std::uint32_t minDurationMs = 1;
  std::uint32_t maxDurationMs = 30000;
};

enum class ServerDialogPresentationStatus : std::uint8_t {
  Disabled = 0,
  AcceptedValidateOnly,
  RejectedMissingActionIdentity,
  RejectedMissingSpeakerEntityKey,
  RejectedMissingContent,
  RejectedInvalidDuration,
  RejectedMissingLineIdentity,
};

[[nodiscard]] constexpr std::string_view serverDialogPresentationStatusName(ServerDialogPresentationStatus status) noexcept {
  switch(status) {
    case ServerDialogPresentationStatus::Disabled:                        return "disabled";
    case ServerDialogPresentationStatus::AcceptedValidateOnly:            return "accepted_validate_only";
    case ServerDialogPresentationStatus::RejectedMissingActionIdentity:    return "rejected_missing_action_identity";
    case ServerDialogPresentationStatus::RejectedMissingSpeakerEntityKey:  return "rejected_missing_speaker_entity_key";
    case ServerDialogPresentationStatus::RejectedMissingContent:           return "rejected_missing_content";
    case ServerDialogPresentationStatus::RejectedInvalidDuration:          return "rejected_invalid_duration";
    case ServerDialogPresentationStatus::RejectedMissingLineIdentity:      return "rejected_missing_line_identity";
  }
  return "unknown";
}

struct ServerDialogPresentationDecision final {
  Net::ClientGameplayAckStatus status = Net::ClientGameplayAckStatus::Ack;
  std::uint32_t                flags = Net::ClientGameplayAckAccepted;
  ServerDialogPresentationStatus presentationStatus = ServerDialogPresentationStatus::Disabled;
  std::string                  reason = "client_received_no_ui_apply";
  std::string                  message = "ServerNpcDialogIntent decoded and logged; UI/audio apply remains disabled.";
  bool                         validationEnabled = false;
  bool                         uiApplied = false;
  bool                         audioApplied = false;

  [[nodiscard]] constexpr bool accepted() const noexcept {
    return status == Net::ClientGameplayAckStatus::Ack;
  }
};

[[nodiscard]] inline ServerDialogPresentationDecision serverDialogAck(ServerDialogPresentationStatus presentationStatus,
                                                                      std::string reason,
                                                                      std::string message,
                                                                      bool validationEnabled) {
  ServerDialogPresentationDecision out;
  out.status = Net::ClientGameplayAckStatus::Ack;
  out.flags = Net::ClientGameplayAckAccepted;
  out.presentationStatus = presentationStatus;
  out.reason = std::move(reason);
  out.message = std::move(message);
  out.validationEnabled = validationEnabled;
  return out;
}

[[nodiscard]] inline ServerDialogPresentationDecision serverDialogNack(ServerDialogPresentationStatus presentationStatus,
                                                                       std::string reason,
                                                                       std::string message) {
  ServerDialogPresentationDecision out;
  out.status = Net::ClientGameplayAckStatus::Nack;
  out.flags = Net::ClientGameplayAckRejected;
  out.presentationStatus = presentationStatus;
  out.reason = std::move(reason);
  out.message = std::move(message);
  out.validationEnabled = true;
  return out;
}

[[nodiscard]] inline ServerDialogPresentationDecision evaluateServerDialogPresentation(
    const Net::ServerNpcDialogIntentPacket& intent,
    const ServerDialogPresentationConfig& cfg) {
  if(!cfg.validateOnly) {
    return serverDialogAck(ServerDialogPresentationStatus::Disabled,
                           "client_received_no_ui_apply",
                           "ServerNpcDialogIntent decoded and logged; UI/audio apply remains disabled.",
                           false);
  }

  if(intent.actionId.empty() || intent.ackKey.empty()) {
    return serverDialogNack(ServerDialogPresentationStatus::RejectedMissingActionIdentity,
                            "client_dialog_presentation_missing_action_identity",
                            "ServerNpcDialogIntent cannot be acknowledged safely without action_id and ack_key.");
  }

  if(cfg.requireSpeakerEntityKey && intent.speakerEntityKey.empty() && intent.speakerNpcInstanceUuid.empty()) {
    return serverDialogNack(ServerDialogPresentationStatus::RejectedMissingSpeakerEntityKey,
                            "client_dialog_presentation_missing_speaker",
                            "ServerNpcDialogIntent has no speaker entity key or npc_instance_uuid for future local NPC mapping.");
  }

  if(cfg.requireTextOrAudio && intent.text.empty() && intent.audioRef.empty()) {
    return serverDialogNack(ServerDialogPresentationStatus::RejectedMissingContent,
                            "client_dialog_presentation_missing_text_and_audio",
                            "ServerNpcDialogIntent has neither subtitle text nor audio reference.");
  }

  if(intent.durationMs < cfg.minDurationMs || intent.durationMs > cfg.maxDurationMs) {
    return serverDialogNack(ServerDialogPresentationStatus::RejectedInvalidDuration,
                            "client_dialog_presentation_invalid_duration",
                            "ServerNpcDialogIntent duration is outside the client presentation validation bounds.");
  }

  if(cfg.requireLineIdentity && intent.lineId.empty()) {
    return serverDialogNack(ServerDialogPresentationStatus::RejectedMissingLineIdentity,
                            "client_dialog_presentation_missing_line_id",
                            "ServerNpcDialogIntent has no line_id for deterministic dialog presentation.");
  }

  return serverDialogAck(ServerDialogPresentationStatus::AcceptedValidateOnly,
                         "client_dialog_presentation_validate_only_ack",
                         "ServerNpcDialogIntent passed client presentation validation; UI/audio side effects remain disabled.",
                         true);
}

enum class ServerDialogMainThreadPresentationStatus : std::uint8_t {
  Disabled = 0,
  ObservedNoApply,
  ObservedWorkerNack,
  ObservedSpeakerResolvedNoApply,
  ObservedPresenterPreflightReadyNoApply,
  SkippedMissingWorld,
  SkippedMissingPlayer,
  SkippedInvalidActionIdentity,
  SkippedSpeakerUnresolved,
  SkippedPresenterPreflightRejected,
  SkippedPresenterPreflightBlocked,
};

[[nodiscard]] constexpr std::string_view serverDialogMainThreadPresentationStatusName(
    ServerDialogMainThreadPresentationStatus status) noexcept {
  switch(status) {
    case ServerDialogMainThreadPresentationStatus::Disabled:                     return "disabled";
    case ServerDialogMainThreadPresentationStatus::ObservedNoApply:              return "observed_no_apply";
    case ServerDialogMainThreadPresentationStatus::ObservedWorkerNack:           return "observed_worker_nack";
    case ServerDialogMainThreadPresentationStatus::ObservedSpeakerResolvedNoApply: return "observed_speaker_resolved_no_apply";
    case ServerDialogMainThreadPresentationStatus::ObservedPresenterPreflightReadyNoApply: return "observed_presenter_preflight_ready_no_apply";
    case ServerDialogMainThreadPresentationStatus::SkippedMissingWorld:          return "skipped_missing_world";
    case ServerDialogMainThreadPresentationStatus::SkippedMissingPlayer:         return "skipped_missing_player";
    case ServerDialogMainThreadPresentationStatus::SkippedInvalidActionIdentity: return "skipped_invalid_action_identity";
    case ServerDialogMainThreadPresentationStatus::SkippedSpeakerUnresolved:     return "skipped_speaker_unresolved";
    case ServerDialogMainThreadPresentationStatus::SkippedPresenterPreflightRejected: return "skipped_presenter_preflight_rejected";
    case ServerDialogMainThreadPresentationStatus::SkippedPresenterPreflightBlocked:  return "skipped_presenter_preflight_blocked";
  }
  return "unknown";
}

struct ServerDialogPresentationEvent final {
  Net::ServerNpcDialogIntentPacket intent;
  ServerDialogPresentationDecision decision;
  std::uint64_t receivedOrder = 0;
  bool duplicateDelivery = false;
};

enum class ServerDialogSpeakerResolutionStatus : std::uint8_t {
  NotRequested = 0,
  MissingSpeakerIdentity,
  UnsupportedNpcInstanceUuidOnly,
  UnsupportedSpeakerIdentity,
  WorldMismatch,
  ResolvedNpcEntityKey,
  ResolvedCompactNpcKey,
  ResolvedCharacterPlayer,
  MissingLocalNpc,
  MissingLocalPlayer,
};

[[nodiscard]] constexpr std::string_view serverDialogSpeakerResolutionStatusName(
    ServerDialogSpeakerResolutionStatus status) noexcept {
  switch(status) {
    case ServerDialogSpeakerResolutionStatus::NotRequested:                   return "not_requested";
    case ServerDialogSpeakerResolutionStatus::MissingSpeakerIdentity:         return "missing_speaker_identity";
    case ServerDialogSpeakerResolutionStatus::UnsupportedNpcInstanceUuidOnly: return "unsupported_npc_instance_uuid_only";
    case ServerDialogSpeakerResolutionStatus::UnsupportedSpeakerIdentity:     return "unsupported_speaker_identity";
    case ServerDialogSpeakerResolutionStatus::WorldMismatch:                  return "world_mismatch";
    case ServerDialogSpeakerResolutionStatus::ResolvedNpcEntityKey:           return "resolved_npc_entity_key";
    case ServerDialogSpeakerResolutionStatus::ResolvedCompactNpcKey:          return "resolved_compact_npc_key";
    case ServerDialogSpeakerResolutionStatus::ResolvedCharacterPlayer:        return "resolved_character_player";
    case ServerDialogSpeakerResolutionStatus::MissingLocalNpc:                return "missing_local_npc";
    case ServerDialogSpeakerResolutionStatus::MissingLocalPlayer:             return "missing_local_player";
  }
  return "unknown";
}

struct ServerDialogSpeakerResolution final {
  ServerDialogSpeakerResolutionStatus status = ServerDialogSpeakerResolutionStatus::NotRequested;
  std::string reason = "client_dialog_speaker_resolution_not_requested";
  std::string message = "Local speaker resolution was not requested for this main-thread probe.";
  std::string requestedEntityKey;
  std::string requestedNpcInstanceUuid;
  std::string requestedWorld;
  std::string localWorld;
  std::string displayName;
  std::uint32_t localNpcId = std::numeric_limits<std::uint32_t>::max();
  std::uint32_t persistentId = std::numeric_limits<std::uint32_t>::max();
  std::uint32_t symbol = std::numeric_limits<std::uint32_t>::max();
  float positionX = 0.f;
  float positionY = 0.f;
  float positionZ = 0.f;
  float distanceToPlayerSquared = -1.f;
  bool requested = false;
  bool resolved = false;
  bool playerSpeaker = false;

  [[nodiscard]] constexpr bool ok() const noexcept {
    return resolved;
  }
};


enum class ServerDialogPresenterSubtitleCapability : std::uint8_t {
  NotRequested = 0,
  DirectText,
  ScriptLineText,
  ScriptAudioRefText,
  Missing,
};

[[nodiscard]] constexpr std::string_view serverDialogPresenterSubtitleCapabilityName(
    ServerDialogPresenterSubtitleCapability status) noexcept {
  switch(status) {
    case ServerDialogPresenterSubtitleCapability::NotRequested:      return "not_requested";
    case ServerDialogPresenterSubtitleCapability::DirectText:        return "direct_text";
    case ServerDialogPresenterSubtitleCapability::ScriptLineText:    return "script_line_text";
    case ServerDialogPresenterSubtitleCapability::ScriptAudioRefText:return "script_audio_ref_text";
    case ServerDialogPresenterSubtitleCapability::Missing:           return "missing";
  }
  return "unknown";
}

enum class ServerDialogPresenterAudioCapability : std::uint8_t {
  NotRequested = 0,
  SafeReferenceNoLoad,
  RejectedEmptyAfterNormalize,
  RejectedPathLikeReference,
  RejectedTooLong,
  RejectedControlCharacter,
};

[[nodiscard]] constexpr std::string_view serverDialogPresenterAudioCapabilityName(
    ServerDialogPresenterAudioCapability status) noexcept {
  switch(status) {
    case ServerDialogPresenterAudioCapability::NotRequested:               return "not_requested";
    case ServerDialogPresenterAudioCapability::SafeReferenceNoLoad:        return "safe_reference_no_load";
    case ServerDialogPresenterAudioCapability::RejectedEmptyAfterNormalize:return "rejected_empty_after_normalize";
    case ServerDialogPresenterAudioCapability::RejectedPathLikeReference:  return "rejected_path_like_reference";
    case ServerDialogPresenterAudioCapability::RejectedTooLong:            return "rejected_too_long";
    case ServerDialogPresenterAudioCapability::RejectedControlCharacter:   return "rejected_control_character";
  }
  return "unknown";
}

enum class ServerDialogPresenterTimingCapability : std::uint8_t {
  NotRequested = 0,
  Valid,
  RejectedInvalidDuration,
};

[[nodiscard]] constexpr std::string_view serverDialogPresenterTimingCapabilityName(
    ServerDialogPresenterTimingCapability status) noexcept {
  switch(status) {
    case ServerDialogPresenterTimingCapability::NotRequested:             return "not_requested";
    case ServerDialogPresenterTimingCapability::Valid:                    return "valid";
    case ServerDialogPresenterTimingCapability::RejectedInvalidDuration:  return "rejected_invalid_duration";
  }
  return "unknown";
}

enum class ServerDialogPresenterPreflightStatus : std::uint8_t {
  NotRequested = 0,
  ReadyNoApply,
  BlockedByWorkerNack,
  BlockedBySpeakerUnresolved,
  BlockedByLocalDialogBusy,
  RejectedMissingPresentableContent,
  RejectedUnsafeAudioReference,
  RejectedInvalidDuration,
};

[[nodiscard]] constexpr std::string_view serverDialogPresenterPreflightStatusName(
    ServerDialogPresenterPreflightStatus status) noexcept {
  switch(status) {
    case ServerDialogPresenterPreflightStatus::NotRequested:                    return "not_requested";
    case ServerDialogPresenterPreflightStatus::ReadyNoApply:                    return "ready_no_apply";
    case ServerDialogPresenterPreflightStatus::BlockedByWorkerNack:             return "blocked_by_worker_nack";
    case ServerDialogPresenterPreflightStatus::BlockedBySpeakerUnresolved:      return "blocked_by_speaker_unresolved";
    case ServerDialogPresenterPreflightStatus::BlockedByLocalDialogBusy:        return "blocked_by_local_dialog_busy";
    case ServerDialogPresenterPreflightStatus::RejectedMissingPresentableContent:return "rejected_missing_presentable_content";
    case ServerDialogPresenterPreflightStatus::RejectedUnsafeAudioReference:    return "rejected_unsafe_audio_reference";
    case ServerDialogPresenterPreflightStatus::RejectedInvalidDuration:         return "rejected_invalid_duration";
  }
  return "unknown";
}

struct ServerDialogPresenterPreflightInput final {
  bool enabled = false;
  bool workerAccepted = true;
  bool speakerResolved = false;
  bool localDialogBusy = false;
  std::string scriptTextByLineId;
  std::string scriptTextByAudioRef;
  std::uint32_t minDurationMs = 1;
  std::uint32_t maxDurationMs = 30000;
};

struct ServerDialogPresenterPreflightResult final {
  ServerDialogPresenterPreflightStatus status = ServerDialogPresenterPreflightStatus::NotRequested;
  ServerDialogPresenterSubtitleCapability subtitleCapability = ServerDialogPresenterSubtitleCapability::NotRequested;
  ServerDialogPresenterAudioCapability audioCapability = ServerDialogPresenterAudioCapability::NotRequested;
  ServerDialogPresenterTimingCapability timingCapability = ServerDialogPresenterTimingCapability::NotRequested;
  std::string reason = "client_dialog_presenter_preflight_not_requested";
  std::string message = "No-apply subtitle/audio presenter preflight was not requested.";
  std::string normalizedAudioRef;
  bool requested = false;
  bool presentableSubtitle = false;
  bool presentableAudioRef = false;
  bool wouldQueueBehindLocalDialog = false;
  bool uiApplied = false;
  bool audioApplied = false;

  [[nodiscard]] constexpr bool ready() const noexcept {
    return status == ServerDialogPresenterPreflightStatus::ReadyNoApply;
  }

  [[nodiscard]] constexpr bool blocked() const noexcept {
    return status == ServerDialogPresenterPreflightStatus::BlockedByWorkerNack ||
           status == ServerDialogPresenterPreflightStatus::BlockedBySpeakerUnresolved ||
           status == ServerDialogPresenterPreflightStatus::BlockedByLocalDialogBusy;
  }
};

[[nodiscard]] inline bool serverDialogEndsWithCaseInsensitive(std::string_view value, std::string_view suffix) noexcept {
  if(value.size() < suffix.size())
    return false;
  value.remove_prefix(value.size() - suffix.size());
  for(std::size_t i = 0; i < suffix.size(); ++i) {
    const auto a = static_cast<unsigned char>(value[i]);
    const auto b = static_cast<unsigned char>(suffix[i]);
    const char la = (a >= 'A' && a <= 'Z') ? static_cast<char>(a - 'A' + 'a') : static_cast<char>(a);
    const char lb = (b >= 'A' && b <= 'Z') ? static_cast<char>(b - 'A' + 'a') : static_cast<char>(b);
    if(la != lb)
      return false;
  }
  return true;
}

[[nodiscard]] inline std::string normalizeServerDialogAudioRef(std::string_view audioRef) {
  while(!audioRef.empty() && (audioRef.front() == ' ' || audioRef.front() == '\t'))
    audioRef.remove_prefix(1);
  while(!audioRef.empty() && (audioRef.back() == ' ' || audioRef.back() == '\t'))
    audioRef.remove_suffix(1);
  if(serverDialogEndsWithCaseInsensitive(audioRef, ".wav"))
    audioRef.remove_suffix(4);
  return std::string(audioRef);
}

[[nodiscard]] inline ServerDialogPresenterAudioCapability classifyServerDialogAudioRef(std::string_view normalizedAudioRef) noexcept {
  constexpr std::size_t MaxAudioRefLength = 128;
  if(normalizedAudioRef.empty())
    return ServerDialogPresenterAudioCapability::RejectedEmptyAfterNormalize;
  if(normalizedAudioRef.size() > MaxAudioRefLength)
    return ServerDialogPresenterAudioCapability::RejectedTooLong;
  for(char c : normalizedAudioRef) {
    const auto uc = static_cast<unsigned char>(c);
    if(uc < 0x20u || uc == 0x7fu)
      return ServerDialogPresenterAudioCapability::RejectedControlCharacter;
    if(c == '/' || c == '\\' || c == ':' || c == '*')
      return ServerDialogPresenterAudioCapability::RejectedPathLikeReference;
  }
  if(normalizedAudioRef.find("..") != std::string_view::npos)
    return ServerDialogPresenterAudioCapability::RejectedPathLikeReference;
  return ServerDialogPresenterAudioCapability::SafeReferenceNoLoad;
}

[[nodiscard]] constexpr bool isServerDialogPresenterAudioSafe(ServerDialogPresenterAudioCapability status) noexcept {
  return status == ServerDialogPresenterAudioCapability::SafeReferenceNoLoad;
}

[[nodiscard]] inline ServerDialogPresenterPreflightResult evaluateServerDialogPresenterPreflight(
    const Net::ServerNpcDialogIntentPacket& intent,
    const ServerDialogPresenterPreflightInput& input) {
  ServerDialogPresenterPreflightResult out;
  if(!input.enabled)
    return out;

  out.requested = true;
  out.reason = "client_dialog_presenter_preflight_started";
  out.message = "ServerNpcDialogIntent reached the no-apply subtitle/audio presenter preflight.";

  if(!intent.text.empty()) {
    out.subtitleCapability = ServerDialogPresenterSubtitleCapability::DirectText;
    out.presentableSubtitle = true;
  } else if(!input.scriptTextByLineId.empty()) {
    out.subtitleCapability = ServerDialogPresenterSubtitleCapability::ScriptLineText;
    out.presentableSubtitle = true;
  } else if(!input.scriptTextByAudioRef.empty()) {
    out.subtitleCapability = ServerDialogPresenterSubtitleCapability::ScriptAudioRefText;
    out.presentableSubtitle = true;
  } else {
    out.subtitleCapability = ServerDialogPresenterSubtitleCapability::Missing;
  }

  if(intent.audioRef.empty()) {
    out.audioCapability = ServerDialogPresenterAudioCapability::NotRequested;
  } else {
    out.normalizedAudioRef = normalizeServerDialogAudioRef(intent.audioRef);
    out.audioCapability = classifyServerDialogAudioRef(out.normalizedAudioRef);
    out.presentableAudioRef = isServerDialogPresenterAudioSafe(out.audioCapability);
  }

  if(intent.durationMs < input.minDurationMs || intent.durationMs > input.maxDurationMs)
    out.timingCapability = ServerDialogPresenterTimingCapability::RejectedInvalidDuration;
  else
    out.timingCapability = ServerDialogPresenterTimingCapability::Valid;

  if(!input.workerAccepted) {
    out.status = ServerDialogPresenterPreflightStatus::BlockedByWorkerNack;
    out.reason = "client_dialog_presenter_preflight_worker_nack";
    out.message = "The UDP worker already returned NACK, so the future presenter would not apply this dialog intent.";
    return out;
  }

  if(!input.speakerResolved) {
    out.status = ServerDialogPresenterPreflightStatus::BlockedBySpeakerUnresolved;
    out.reason = "client_dialog_presenter_preflight_speaker_unresolved";
    out.message = "The future presenter would need a resolved local speaker before subtitle/audio presentation.";
    return out;
  }

  if(out.timingCapability != ServerDialogPresenterTimingCapability::Valid) {
    out.status = ServerDialogPresenterPreflightStatus::RejectedInvalidDuration;
    out.reason = "client_dialog_presenter_preflight_invalid_duration";
    out.message = "The future presenter rejected this intent because duration is outside guarded presentation bounds.";
    return out;
  }

  if(!intent.audioRef.empty() && !out.presentableAudioRef) {
    out.status = ServerDialogPresenterPreflightStatus::RejectedUnsafeAudioReference;
    out.reason = "client_dialog_presenter_preflight_unsafe_audio_ref";
    out.message = "The future presenter rejected this intent because audio_ref is not a safe local sound reference.";
    return out;
  }

  if(!out.presentableSubtitle && !out.presentableAudioRef) {
    out.status = ServerDialogPresenterPreflightStatus::RejectedMissingPresentableContent;
    out.reason = "client_dialog_presenter_preflight_missing_content";
    out.message = "The future presenter found neither subtitle text nor a safe audio reference.";
    return out;
  }

  if(input.localDialogBusy) {
    out.status = ServerDialogPresenterPreflightStatus::BlockedByLocalDialogBusy;
    out.reason = "client_dialog_presenter_preflight_local_dialog_busy";
    out.message = "A local Gothic dialog is already active; a future presenter would queue or reject this server-owned line instead of applying immediately.";
    out.wouldQueueBehindLocalDialog = true;
    return out;
  }

  out.status = ServerDialogPresenterPreflightStatus::ReadyNoApply;
  out.reason = "client_dialog_presenter_preflight_ready_no_apply";
  out.message = "The future presenter has enough local capability to show/play this server-owned dialog line, but no UI/audio side effects were applied.";
  return out;
}

struct ServerDialogMainThreadPresentationProbeInput final {
  bool enabled = false;
  bool serverBoundClient = false;
  bool worldAvailable = false;
  bool playerAvailable = false;
  bool requireLocalSpeakerResolution = false;
  bool requirePresenterPreflight = false;
  ServerDialogSpeakerResolution speakerResolution;
  ServerDialogPresenterPreflightResult presenterPreflight;
};

struct ServerDialogMainThreadPresentationProbeResult final {
  ServerDialogMainThreadPresentationStatus status = ServerDialogMainThreadPresentationStatus::Disabled;
  std::string reason = "client_dialog_main_thread_probe_disabled";
  std::string message = "Main-thread server dialog presentation probe is disabled.";
  bool uiApplied = false;
  bool audioApplied = false;
  bool terminalAckAlreadySent = true;

  [[nodiscard]] constexpr bool observed() const noexcept {
    return status == ServerDialogMainThreadPresentationStatus::ObservedNoApply ||
           status == ServerDialogMainThreadPresentationStatus::ObservedWorkerNack ||
           status == ServerDialogMainThreadPresentationStatus::ObservedSpeakerResolvedNoApply ||
           status == ServerDialogMainThreadPresentationStatus::ObservedPresenterPreflightReadyNoApply;
  }
};

[[nodiscard]] inline ServerDialogMainThreadPresentationProbeResult evaluateServerDialogMainThreadPresentationProbe(
    const ServerDialogPresentationEvent& event,
    const ServerDialogMainThreadPresentationProbeInput& input) {
  if(!input.enabled || !input.serverBoundClient) {
    return {};
  }

  if(!input.worldAvailable) {
    ServerDialogMainThreadPresentationProbeResult out;
    out.status = ServerDialogMainThreadPresentationStatus::SkippedMissingWorld;
    out.reason = "client_dialog_main_thread_missing_world";
    out.message = "ServerNpcDialogIntent reached the main thread before a local world was available.";
    return out;
  }

  if(!input.playerAvailable) {
    ServerDialogMainThreadPresentationProbeResult out;
    out.status = ServerDialogMainThreadPresentationStatus::SkippedMissingPlayer;
    out.reason = "client_dialog_main_thread_missing_player";
    out.message = "ServerNpcDialogIntent reached the main thread before a local player instance was available.";
    return out;
  }

  if(event.intent.actionId.empty() || event.intent.ackKey.empty()) {
    ServerDialogMainThreadPresentationProbeResult out;
    out.status = ServerDialogMainThreadPresentationStatus::SkippedInvalidActionIdentity;
    out.reason = "client_dialog_main_thread_invalid_action_identity";
    out.message = "ServerNpcDialogIntent cannot be bound to a future presenter without action_id and ack_key.";
    return out;
  }

  if(!event.decision.accepted()) {
    ServerDialogMainThreadPresentationProbeResult out;
    out.status = ServerDialogMainThreadPresentationStatus::ObservedWorkerNack;
    out.reason = "client_dialog_main_thread_worker_nack_observed";
    out.message = "ServerNpcDialogIntent was observed on the main thread after the UDP worker already returned NACK.";
    return out;
  }

  if(input.requireLocalSpeakerResolution) {
    if(!input.speakerResolution.ok()) {
      ServerDialogMainThreadPresentationProbeResult out;
      out.status = ServerDialogMainThreadPresentationStatus::SkippedSpeakerUnresolved;
      out.reason = input.speakerResolution.reason.empty()
          ? "client_dialog_main_thread_speaker_unresolved"
          : input.speakerResolution.reason;
      out.message = input.speakerResolution.message.empty()
          ? "ServerNpcDialogIntent reached the main thread, but the local speaker could not be resolved safely."
          : input.speakerResolution.message;
      return out;
    }

    if(input.requirePresenterPreflight) {
      if(input.presenterPreflight.ready()) {
        ServerDialogMainThreadPresentationProbeResult out;
        out.status = ServerDialogMainThreadPresentationStatus::ObservedPresenterPreflightReadyNoApply;
        out.reason = "client_dialog_main_thread_presenter_preflight_ready_no_apply";
        out.message = "ServerNpcDialogIntent passed the no-apply subtitle/audio presenter preflight on the main thread.";
        return out;
      }

      ServerDialogMainThreadPresentationProbeResult out;
      out.status = input.presenterPreflight.blocked()
          ? ServerDialogMainThreadPresentationStatus::SkippedPresenterPreflightBlocked
          : ServerDialogMainThreadPresentationStatus::SkippedPresenterPreflightRejected;
      out.reason = input.presenterPreflight.reason.empty()
          ? "client_dialog_main_thread_presenter_preflight_not_ready"
          : input.presenterPreflight.reason;
      out.message = input.presenterPreflight.message.empty()
          ? "ServerNpcDialogIntent reached the no-apply presenter preflight, but it is not ready for local presentation."
          : input.presenterPreflight.message;
      return out;
    }

    ServerDialogMainThreadPresentationProbeResult out;
    out.status = ServerDialogMainThreadPresentationStatus::ObservedSpeakerResolvedNoApply;
    out.reason = "client_dialog_main_thread_speaker_resolved_no_apply";
    out.message = "ServerNpcDialogIntent resolved to a local speaker on the main thread; subtitle/audio side effects remain disabled.";
    return out;
  }

  ServerDialogMainThreadPresentationProbeResult out;
  out.status = ServerDialogMainThreadPresentationStatus::ObservedNoApply;
  out.reason = "client_dialog_main_thread_observed_no_apply";
  out.message = "ServerNpcDialogIntent reached the main-thread presenter boundary; subtitle/audio side effects remain disabled.";
  return out;
}

[[nodiscard]] inline std::string serverDialogPresentationJsonEscape(std::string_view value) {
  std::string out;
  out.reserve(value.size() + 2);
  out.push_back('"');
  for(char c : value) {
    switch(c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if(static_cast<unsigned char>(c) < 0x20u) {
          constexpr char hex[] = "0123456789abcdef";
          out += "\\u00";
          out.push_back(hex[(static_cast<unsigned char>(c) >> 4u) & 0x0Fu]);
          out.push_back(hex[static_cast<unsigned char>(c) & 0x0Fu]);
        } else {
          out.push_back(c);
        }
        break;
    }
  }
  out.push_back('"');
  return out;
}

[[nodiscard]] inline std::string serverDialogMainThreadProbeJson(
    const ServerDialogPresentationEvent& event,
    const ServerDialogMainThreadPresentationProbeInput& input,
    const ServerDialogMainThreadPresentationProbeResult& result) {
  std::string out;
  out.reserve(event.intent.text.size() + event.intent.audioRef.size() + event.intent.reason.size() + 1024);
  out += "{\"schema\":\"mmo.client_dialog_main_thread_presentation_probe.v1\"";
  out += ",\"received_order\":";
  out += std::to_string(event.receivedOrder);
  out += ",\"packet_sequence\":";
  out += std::to_string(event.intent.packetSequence);
  out += ",\"local_sequence\":";
  out += std::to_string(event.intent.localSequence);
  out += ",\"server_tick\":";
  out += std::to_string(event.intent.serverTick);
  out += ",\"start_tick\":";
  out += std::to_string(event.intent.startTick);
  out += ",\"duration_ms\":";
  out += std::to_string(event.intent.durationMs);
  out += ",\"session_uuid\":";
  out += serverDialogPresentationJsonEscape(event.intent.sessionUuid);
  out += ",\"target_character_key\":";
  out += serverDialogPresentationJsonEscape(event.intent.targetCharacterKey);
  out += ",\"action_id\":";
  out += serverDialogPresentationJsonEscape(event.intent.actionId);
  out += ",\"ack_key\":";
  out += serverDialogPresentationJsonEscape(event.intent.ackKey);
  out += ",\"conversation_id\":";
  out += serverDialogPresentationJsonEscape(event.intent.conversationId);
  out += ",\"world_instance_uuid\":";
  out += serverDialogPresentationJsonEscape(event.intent.worldInstanceUuid);
  out += ",\"speaker_entity_key\":";
  out += serverDialogPresentationJsonEscape(event.intent.speakerEntityKey);
  out += ",\"speaker_npc_instance_uuid\":";
  out += serverDialogPresentationJsonEscape(event.intent.speakerNpcInstanceUuid);
  out += ",\"line_id\":";
  out += serverDialogPresentationJsonEscape(event.intent.lineId);
  out += ",\"text\":";
  out += serverDialogPresentationJsonEscape(event.intent.text);
  out += ",\"audio_ref\":";
  out += serverDialogPresentationJsonEscape(event.intent.audioRef);
  out += ",\"udp_worker_ack_status\":";
  out += serverDialogPresentationJsonEscape(event.decision.accepted() ? "ack" : "nack");
  out += ",\"udp_worker_reason\":";
  out += serverDialogPresentationJsonEscape(event.decision.reason);
  out += ",\"udp_worker_presentation_status\":";
  out += serverDialogPresentationJsonEscape(serverDialogPresentationStatusName(event.decision.presentationStatus));
  out += ",\"duplicate_delivery\":";
  out += event.duplicateDelivery ? "true" : "false";
  out += ",\"probe_enabled\":";
  out += input.enabled ? "true" : "false";
  out += ",\"server_bound_client\":";
  out += input.serverBoundClient ? "true" : "false";
  out += ",\"world_available\":";
  out += input.worldAvailable ? "true" : "false";
  out += ",\"player_available\":";
  out += input.playerAvailable ? "true" : "false";
  out += ",\"require_local_speaker_resolution\":";
  out += input.requireLocalSpeakerResolution ? "true" : "false";
  out += ",\"speaker_resolution_status\":";
  out += serverDialogPresentationJsonEscape(serverDialogSpeakerResolutionStatusName(input.speakerResolution.status));
  out += ",\"speaker_resolution_reason\":";
  out += serverDialogPresentationJsonEscape(input.speakerResolution.reason);
  out += ",\"speaker_resolution_message\":";
  out += serverDialogPresentationJsonEscape(input.speakerResolution.message);
  out += ",\"speaker_resolution_requested\":";
  out += input.speakerResolution.requested ? "true" : "false";
  out += ",\"speaker_resolved\":";
  out += input.speakerResolution.resolved ? "true" : "false";
  out += ",\"speaker_is_player\":";
  out += input.speakerResolution.playerSpeaker ? "true" : "false";
  out += ",\"speaker_requested_entity_key\":";
  out += serverDialogPresentationJsonEscape(input.speakerResolution.requestedEntityKey);
  out += ",\"speaker_requested_npc_instance_uuid\":";
  out += serverDialogPresentationJsonEscape(input.speakerResolution.requestedNpcInstanceUuid);
  out += ",\"speaker_requested_world\":";
  out += serverDialogPresentationJsonEscape(input.speakerResolution.requestedWorld);
  out += ",\"speaker_local_world\":";
  out += serverDialogPresentationJsonEscape(input.speakerResolution.localWorld);
  out += ",\"speaker_display_name\":";
  out += serverDialogPresentationJsonEscape(input.speakerResolution.displayName);
  out += ",\"speaker_local_npc_id\":";
  out += std::to_string(input.speakerResolution.localNpcId);
  out += ",\"speaker_persistent_id\":";
  out += std::to_string(input.speakerResolution.persistentId);
  out += ",\"speaker_symbol\":";
  out += std::to_string(input.speakerResolution.symbol);
  out += ",\"speaker_position\":{";
  out += "\"x\":";
  out += std::to_string(input.speakerResolution.positionX);
  out += ",\"y\":";
  out += std::to_string(input.speakerResolution.positionY);
  out += ",\"z\":";
  out += std::to_string(input.speakerResolution.positionZ);
  out += "}";
  out += ",\"speaker_distance_to_player_squared\":";
  out += std::to_string(input.speakerResolution.distanceToPlayerSquared);
  out += ",\"main_thread_status\":";
  out += serverDialogPresentationJsonEscape(serverDialogMainThreadPresentationStatusName(result.status));
  out += ",\"main_thread_reason\":";
  out += serverDialogPresentationJsonEscape(result.reason);
  out += ",\"main_thread_message\":";
  out += serverDialogPresentationJsonEscape(result.message);
  out += ",\"ui_applied\":";
  out += result.uiApplied ? "true" : "false";
  out += ",\"audio_applied\":";
  out += result.audioApplied ? "true" : "false";
  out += ",\"terminal_ack_already_sent\":";
  out += result.terminalAckAlreadySent ? "true" : "false";
  out += "}";
  return out;
}

} // namespace Mmo





