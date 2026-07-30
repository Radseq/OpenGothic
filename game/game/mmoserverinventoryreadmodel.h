#pragma once

#include "mmoclientadapter.h"
#include "../../../shared/net/mmo/mmo_protocol_v2.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace Mmo::ClientPresentation {

enum class ServerInventoryApplyStatus : std::uint8_t {
  Applied,
  Duplicate,
  Stale,
  Invalid,
  CapacityExceeded,
};

struct ServerInventoryStack final {
  ClientItemStackHandle handle{};
  std::uint64_t archetypeId = 0U;
  std::uint64_t presentationId = 0U;
  std::uint64_t presentationRevision = 0U;
  std::uint32_t quantity = 0U;
  std::uint32_t flags = 0U;
  std::uint64_t itemRevision = 0U;

  [[nodiscard]] constexpr bool valid() const noexcept {
    return handle.valid() && archetypeId != 0U && presentationId != 0U &&
           presentationRevision != 0U && quantity != 0U && itemRevision != 0U;
  }

  [[nodiscard]] friend constexpr bool operator==(
      const ServerInventoryStack&,
      const ServerInventoryStack&) noexcept = default;
};

struct ServerEquipmentBinding final {
  ClientEquipmentSlot slot = ClientEquipmentSlot::MeleeWeapon;
  ClientItemStackHandle item{};
  std::uint64_t itemRevision = 0U;
  std::uint32_t flags = 0U;

  [[nodiscard]] constexpr bool valid() const noexcept {
    return item.valid() && itemRevision != 0U;
  }

  [[nodiscard]] friend constexpr bool operator==(
      const ServerEquipmentBinding&,
      const ServerEquipmentBinding&) noexcept = default;
};

struct ServerInventorySnapshot final {
  std::uint64_t revision = 0U;
  std::vector<ServerInventoryStack> stacks;
};

struct ServerEquipmentSnapshot final {
  std::uint64_t revision = 0U;
  std::vector<ServerEquipmentBinding> equipped;
};

struct ServerInventoryDelta final {
  std::uint64_t previousRevision = 0U;
  std::uint64_t revision = 0U;
  std::vector<ServerInventoryStack> upserted;
  std::vector<ClientItemStackHandle> removed;
};

struct ServerEquipmentSlotChanged final {
  std::uint64_t previousRevision = 0U;
  std::uint64_t revision = 0U;
  ClientEquipmentSlot slot = ClientEquipmentSlot::MeleeWeapon;
  std::optional<ServerEquipmentBinding> equipped;
};

class ServerInventoryReadModel final {
 public:
  explicit ServerInventoryReadModel(std::size_t capacity = 4096U)
      : capacity_(std::max<std::size_t>(1U, capacity)) {
    stacks_.reserve(capacity_);
  }

  [[nodiscard]] ServerInventoryApplyStatus install(
      const ServerInventorySnapshot& snapshot) {
    if(snapshot.revision == 0U || snapshot.stacks.size() > capacity_)
      return snapshot.stacks.size() > capacity_
                 ? ServerInventoryApplyStatus::CapacityExceeded
                 : ServerInventoryApplyStatus::Invalid;
    if(revision_ != 0U) {
      if(snapshot.revision < revision_)
        return ServerInventoryApplyStatus::Stale;
      if(snapshot.revision == revision_)
        return ServerInventoryApplyStatus::Duplicate;
    }

    auto next = snapshot.stacks;
    if(!normalizeAndValidate(next))
      return ServerInventoryApplyStatus::Invalid;
    stacks_ = std::move(next);
    revision_ = snapshot.revision;
    return ServerInventoryApplyStatus::Applied;
  }

  [[nodiscard]] ServerInventoryApplyStatus apply(
      const ServerInventoryDelta& delta) {
    if(revision_ == 0U || delta.previousRevision != revision_ ||
       delta.revision <= delta.previousRevision) {
      if(delta.revision <= revision_)
        return delta.revision == revision_
                   ? ServerInventoryApplyStatus::Duplicate
                   : ServerInventoryApplyStatus::Stale;
      return ServerInventoryApplyStatus::Invalid;
    }
    if(delta.upserted.size() > capacity_ || delta.removed.size() > capacity_)
      return ServerInventoryApplyStatus::CapacityExceeded;

    auto removed = delta.removed;
    std::sort(removed.begin(), removed.end(), lessHandle);
    for(std::size_t index = 0U; index < removed.size(); ++index) {
      if(!removed[index].valid() ||
         (index != 0U &&
          removed[index - 1U].instanceId == removed[index].instanceId)) {
        return ServerInventoryApplyStatus::Invalid;
      }
    }

    auto upserted = delta.upserted;
    if(!normalizeAndValidate(upserted))
      return ServerInventoryApplyStatus::Invalid;

    auto next = stacks_;
    for(const auto handle : removed) {
      const auto found = lowerBoundInstance(next, handle.instanceId);
      if(found == next.end() || found->handle != handle)
        return ServerInventoryApplyStatus::Invalid;
      next.erase(found);
    }
    for(const auto& stack : upserted) {
      const auto found = lowerBoundInstance(next, stack.handle.instanceId);
      if(found != next.end() &&
         found->handle.instanceId == stack.handle.instanceId) {
        if(found->handle.generation > stack.handle.generation ||
           (found->handle.generation == stack.handle.generation &&
            stack.itemRevision < found->itemRevision)) {
          return ServerInventoryApplyStatus::Invalid;
        }
        *found = stack;
      } else {
        next.insert(found, stack);
      }
    }
    if(next.size() > capacity_ || !normalizeAndValidate(next))
      return next.size() > capacity_
                 ? ServerInventoryApplyStatus::CapacityExceeded
                 : ServerInventoryApplyStatus::Invalid;
    stacks_ = std::move(next);
    revision_ = delta.revision;
    return ServerInventoryApplyStatus::Applied;
  }

  [[nodiscard]] ServerInventoryApplyStatus applyAuthoritative(
      ServerInventoryDelta delta) {
    if(delta.revision == 0U || delta.revision <= revision_)
      return delta.revision == revision_
                 ? ServerInventoryApplyStatus::Duplicate
                 : ServerInventoryApplyStatus::Stale;
    delta.previousRevision = revision_;
    return apply(delta);
  }

  void reset() noexcept {
    stacks_.clear();
    revision_ = 0U;
  }

  [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
  [[nodiscard]] bool ready() const noexcept { return revision_ != 0U; }
  [[nodiscard]] std::span<const ServerInventoryStack> stacks() const noexcept {
    return stacks_;
  }
  [[nodiscard]] const ServerInventoryStack* find(
      const ClientItemStackHandle handle) const noexcept {
    const auto found = lowerBound(stacks_, handle);
    return found != stacks_.end() && found->handle == handle ? &*found : nullptr;
  }

 private:
  [[nodiscard]] static constexpr bool lessHandle(
      const ClientItemStackHandle lhs,
      const ClientItemStackHandle rhs) noexcept {
    return lhs.instanceId < rhs.instanceId ||
           (lhs.instanceId == rhs.instanceId && lhs.generation < rhs.generation);
  }

  [[nodiscard]] static std::vector<ServerInventoryStack>::const_iterator lowerBound(
      const std::vector<ServerInventoryStack>& values,
      const ClientItemStackHandle handle) {
    return std::lower_bound(values.begin(), values.end(), handle,
                            [](const ServerInventoryStack& lhs,
                               const ClientItemStackHandle rhs) {
                              return lessHandle(lhs.handle, rhs);
                            });
  }

  [[nodiscard]] static std::vector<ServerInventoryStack>::iterator
  lowerBoundInstance(std::vector<ServerInventoryStack>& values,
                     const std::uint64_t instanceId) {
    return std::lower_bound(values.begin(), values.end(), instanceId,
                            [](const ServerInventoryStack& lhs,
                               const std::uint64_t rhs) {
                              return lhs.handle.instanceId < rhs;
                            });
  }

  [[nodiscard]] static bool normalizeAndValidate(
      std::vector<ServerInventoryStack>& values) {
    std::sort(values.begin(), values.end(), [](const auto& lhs, const auto& rhs) {
      return lessHandle(lhs.handle, rhs.handle);
    });
    for(std::size_t index = 0U; index < values.size(); ++index) {
      if(!values[index].valid())
        return false;
      if(index != 0U &&
         values[index - 1U].handle.instanceId == values[index].handle.instanceId) {
        return false;
      }
    }
    return true;
  }

  std::size_t capacity_ = 4096U;
  std::uint64_t revision_ = 0U;
  std::vector<ServerInventoryStack> stacks_;
};

class ServerEquipmentReadModel final {
 public:
  static constexpr std::size_t SlotCount = 8U;

  [[nodiscard]] ServerInventoryApplyStatus install(
      const ServerEquipmentSnapshot& snapshot) noexcept {
    if(snapshot.revision == 0U || snapshot.equipped.size() > SlotCount)
      return ServerInventoryApplyStatus::Invalid;
    if(revision_ != 0U) {
      if(snapshot.revision < revision_)
        return ServerInventoryApplyStatus::Stale;
      if(snapshot.revision == revision_)
        return ServerInventoryApplyStatus::Duplicate;
    }

    std::array<std::optional<ServerEquipmentBinding>, SlotCount> next{};
    for(const auto& binding : snapshot.equipped) {
      if(!binding.valid() || !knownSlot(binding.slot))
        return ServerInventoryApplyStatus::Invalid;
      auto& target = next[indexOf(binding.slot)];
      if(target.has_value())
        return ServerInventoryApplyStatus::Invalid;
      target = binding;
    }
    equipped_ = std::move(next);
    revision_ = snapshot.revision;
    return ServerInventoryApplyStatus::Applied;
  }

  [[nodiscard]] ServerInventoryApplyStatus apply(
      const ServerEquipmentSlotChanged& change) noexcept {
    if(!knownSlot(change.slot) || revision_ == 0U ||
       change.previousRevision != revision_ ||
       change.revision <= change.previousRevision) {
      if(change.revision <= revision_)
        return change.revision == revision_
                   ? ServerInventoryApplyStatus::Duplicate
                   : ServerInventoryApplyStatus::Stale;
      return ServerInventoryApplyStatus::Invalid;
    }
    if(change.equipped.has_value() &&
       (!change.equipped->valid() || change.equipped->slot != change.slot)) {
      return ServerInventoryApplyStatus::Invalid;
    }
    equipped_[indexOf(change.slot)] = change.equipped;
    revision_ = change.revision;
    return ServerInventoryApplyStatus::Applied;
  }

  [[nodiscard]] ServerInventoryApplyStatus applyAuthoritative(
      ServerEquipmentSlotChanged change) noexcept {
    if(change.revision == 0U || change.revision <= revision_)
      return change.revision == revision_
                 ? ServerInventoryApplyStatus::Duplicate
                 : ServerInventoryApplyStatus::Stale;
    change.previousRevision = revision_;
    return apply(change);
  }

  void reset() noexcept {
    equipped_ = {};
    revision_ = 0U;
  }

  [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
  [[nodiscard]] bool ready() const noexcept { return revision_ != 0U; }
  [[nodiscard]] const ServerEquipmentBinding* at(
      const ClientEquipmentSlot slot) const noexcept {
    if(!knownSlot(slot))
      return nullptr;
    const auto& value = equipped_[indexOf(slot)];
    return value.has_value() ? &*value : nullptr;
  }
  [[nodiscard]] std::optional<ClientEquipmentSlot> slotOf(
      const ClientItemStackHandle handle) const noexcept {
    for(std::size_t index = 0U; index < equipped_.size(); ++index) {
      if(equipped_[index].has_value() && equipped_[index]->item == handle)
        return slotAt(index);
    }
    return std::nullopt;
  }

  [[nodiscard]] std::span<const std::optional<ServerEquipmentBinding>>
  bindings() const noexcept {
    return equipped_;
  }

  [[nodiscard]] static constexpr bool knownSlot(
      const ClientEquipmentSlot slot) noexcept {
    switch(slot) {
      case ClientEquipmentSlot::MeleeWeapon:
      case ClientEquipmentSlot::RangedWeapon:
      case ClientEquipmentSlot::Armor:
      case ClientEquipmentSlot::Amulet:
      case ClientEquipmentSlot::RingLeft:
      case ClientEquipmentSlot::RingRight:
      case ClientEquipmentSlot::Belt:
      case ClientEquipmentSlot::Spell:
        return true;
    }
    return false;
  }

 private:
  [[nodiscard]] static constexpr std::size_t indexOf(
      const ClientEquipmentSlot slot) noexcept {
    switch(slot) {
      case ClientEquipmentSlot::MeleeWeapon: return 0U;
      case ClientEquipmentSlot::RangedWeapon: return 1U;
      case ClientEquipmentSlot::Armor: return 2U;
      case ClientEquipmentSlot::Amulet: return 3U;
      case ClientEquipmentSlot::RingLeft: return 4U;
      case ClientEquipmentSlot::RingRight: return 5U;
      case ClientEquipmentSlot::Belt: return 6U;
      case ClientEquipmentSlot::Spell: return 7U;
    }
    return 0U;
  }

  [[nodiscard]] static constexpr ClientEquipmentSlot slotAt(
      const std::size_t index) noexcept {
    constexpr std::array slots{
        ClientEquipmentSlot::MeleeWeapon,
        ClientEquipmentSlot::RangedWeapon,
        ClientEquipmentSlot::Armor,
        ClientEquipmentSlot::Amulet,
        ClientEquipmentSlot::RingLeft,
        ClientEquipmentSlot::RingRight,
        ClientEquipmentSlot::Belt,
        ClientEquipmentSlot::Spell,
    };
    return slots[index];
  }

  std::uint64_t revision_ = 0U;
  std::array<std::optional<ServerEquipmentBinding>, SlotCount> equipped_{};
};

enum class ServerInventoryPendingPhase : std::uint8_t {
  Submitted,
  AwaitingAuthoritativeDelta,
};

enum class ServerInventoryFeedbackStatus : std::uint8_t {
  Ready,
  Pending,
  Accepted,
  Rejected,
  ResyncRequired,
};

struct ServerInventoryPendingCommand final {
  ClientMmoCommandToken command{};
  ClientItemStackHandle primary{};
  ClientItemStackHandle secondary{};
  std::optional<ClientEquipmentSlot> slot;
  std::uint64_t expectedInventoryRevision = 0U;
  std::uint64_t expectedEquipmentRevision = 0U;
  ServerInventoryPendingPhase phase = ServerInventoryPendingPhase::Submitted;
};

class ServerInventoryPendingState final {
 public:
  explicit ServerInventoryPendingState(std::size_t capacity = 64U)
      : capacity_(std::max<std::size_t>(1U, capacity)) {
    pending_.reserve(capacity_);
  }

  [[nodiscard]] bool add(ServerInventoryPendingCommand command) {
    if(resyncRequired_ || !valid(command) || pending_.size() >= capacity_ ||
       find(command.command) != pending_.end()) {
      return false;
    }
    lastRejection_.clear();
    pending_.push_back(std::move(command));
    bumpChangeRevision();
    return true;
  }

  void rejectSubmission(const ClientMmoSubmitStatus status) {
    if(status == ClientMmoSubmitStatus::Accepted)
      return;
    lastRejection_ = submissionRejectionMessage(status);
    bumpChangeRevision();
  }

  void complete(const ClientMmoCommandCompletion& completion) {
    const auto found = find(completion.command);
    if(found == pending_.end())
      return;
    if(completion.status == ClientMmoCommandCompletionStatus::Applied) {
      if(found->phase != ServerInventoryPendingPhase::AwaitingAuthoritativeDelta) {
        found->phase = ServerInventoryPendingPhase::AwaitingAuthoritativeDelta;
        bumpChangeRevision();
      }
      return;
    }

    lastRejection_ = rejectionMessage(completion);
    if(isResyncRequired(completion)) {
      resyncRequired_ = true;
      resyncExpectedInventoryRevision_ = found->expectedInventoryRevision;
      resyncExpectedEquipmentRevision_ = found->expectedEquipmentRevision;
      resyncInventoryReplacementObserved_ = false;
      resyncEquipmentReplacementObserved_ = false;
    }
    pending_.erase(found);
    bumpChangeRevision();
  }

  void reconcile(const std::uint64_t inventoryRevision,
                 const std::uint64_t equipmentRevision) {
    bool changed = std::erase_if(
        pending_, [=](const ServerInventoryPendingCommand& value) {
          if(value.phase != ServerInventoryPendingPhase::AwaitingAuthoritativeDelta)
            return false;
          if(requiresEquipmentRevision(value.command.kind))
            return equipmentRevision > value.expectedEquipmentRevision;
          return inventoryRevision > value.expectedInventoryRevision;
        }) != 0U;

    if(resyncRequired_ &&
       (inventoryRevision > resyncExpectedInventoryRevision_ ||
        (resyncExpectedEquipmentRevision_ != 0U &&
         equipmentRevision > resyncExpectedEquipmentRevision_))) {
      clearResyncRequirement();
      changed = true;
    }
    if(changed)
      bumpChangeRevision();
  }

  void observeAuthoritativeInventoryReplacement() noexcept {
    if(!resyncRequired_)
      return;
    resyncInventoryReplacementObserved_ = true;
    clearResyncAfterReplacementIfComplete();
  }

  void observeAuthoritativeEquipmentReplacement() noexcept {
    if(!resyncRequired_)
      return;
    resyncEquipmentReplacementObserved_ = true;
    clearResyncAfterReplacementIfComplete();
  }

  void reset() noexcept {
    pending_.clear();
    lastRejection_.clear();
    resyncRequired_ = false;
    resyncExpectedInventoryRevision_ = 0U;
    resyncExpectedEquipmentRevision_ = 0U;
    resyncInventoryReplacementObserved_ = false;
    resyncEquipmentReplacementObserved_ = false;
    bumpChangeRevision();
  }

  [[nodiscard]] bool pending(const ClientMmoCommandToken& command) const noexcept {
    return find(command) != pending_.end();
  }
  [[nodiscard]] bool pending(const ClientItemStackHandle handle) const noexcept {
    return std::any_of(pending_.begin(), pending_.end(), [handle](const auto& value) {
      return value.primary == handle || value.secondary == handle;
    });
  }
  [[nodiscard]] bool pending(const ClientEquipmentSlot slot) const noexcept {
    return std::any_of(pending_.begin(), pending_.end(), [slot](const auto& value) {
      return value.slot.has_value() && *value.slot == slot;
    });
  }
  [[nodiscard]] std::optional<ServerInventoryPendingPhase> phase(
      const ClientItemStackHandle handle) const noexcept {
    const auto found = std::find_if(
        pending_.begin(), pending_.end(), [handle](const auto& value) {
          return value.primary == handle || value.secondary == handle;
        });
    return found != pending_.end() ? std::optional{found->phase} : std::nullopt;
  }
  [[nodiscard]] std::span<const ServerInventoryPendingCommand> commands()
      const noexcept {
    return pending_;
  }
  [[nodiscard]] ServerInventoryFeedbackStatus feedbackStatus() const noexcept {
    if(resyncRequired_)
      return ServerInventoryFeedbackStatus::ResyncRequired;
    if(!lastRejection_.empty())
      return ServerInventoryFeedbackStatus::Rejected;
    if(std::any_of(pending_.begin(), pending_.end(), [](const auto& value) {
         return value.phase == ServerInventoryPendingPhase::Submitted;
       })) {
      return ServerInventoryFeedbackStatus::Pending;
    }
    if(!pending_.empty())
      return ServerInventoryFeedbackStatus::Accepted;
    return ServerInventoryFeedbackStatus::Ready;
  }
  [[nodiscard]] bool canSubmit() const noexcept { return !resyncRequired_; }
  [[nodiscard]] bool resyncRequired() const noexcept { return resyncRequired_; }
  [[nodiscard]] std::uint64_t changeRevision() const noexcept {
    return changeRevision_;
  }
  [[nodiscard]] const std::string& lastRejection() const noexcept {
    return lastRejection_;
  }
  void clearRejection() noexcept {
    if(lastRejection_.empty() || resyncRequired_)
      return;
    lastRejection_.clear();
    bumpChangeRevision();
  }

 private:
  [[nodiscard]] static constexpr bool requiresEquipmentRevision(
      const ClientMmoCommandKind kind) noexcept {
    return kind == ClientMmoCommandKind::EquipItem ||
           kind == ClientMmoCommandKind::UnequipItem;
  }

  [[nodiscard]] static constexpr bool valid(
      const ServerInventoryPendingCommand& command) noexcept {
    if(!command.command.valid() || !command.primary.valid())
      return false;
    switch(command.command.kind) {
      case ClientMmoCommandKind::PickupItem:
        return false;
      case ClientMmoCommandKind::EquipItem:
        return command.slot.has_value() &&
               ServerEquipmentReadModel::knownSlot(*command.slot) &&
               (!command.secondary.valid() ||
                command.secondary != command.primary) &&
               command.expectedInventoryRevision != 0U &&
               command.expectedEquipmentRevision != 0U;
      case ClientMmoCommandKind::UnequipItem:
        return command.slot.has_value() &&
               ServerEquipmentReadModel::knownSlot(*command.slot) &&
               !command.secondary.valid() &&
               command.expectedInventoryRevision != 0U &&
               command.expectedEquipmentRevision != 0U;
      case ClientMmoCommandKind::MergeStack:
        return !command.slot.has_value() && command.secondary.valid() &&
               command.secondary != command.primary &&
               command.expectedInventoryRevision != 0U;
      case ClientMmoCommandKind::DropItem:
      case ClientMmoCommandKind::SplitStack:
      case ClientMmoCommandKind::UseItem:
        return !command.slot.has_value() && !command.secondary.valid() &&
               command.expectedInventoryRevision != 0U;
    }
    return false;
  }

  [[nodiscard]] std::vector<ServerInventoryPendingCommand>::iterator find(
      const ClientMmoCommandToken& command) noexcept {
    return std::find_if(pending_.begin(), pending_.end(),
                        [&command](const auto& value) {
                          return value.command == command;
                        });
  }
  [[nodiscard]] std::vector<ServerInventoryPendingCommand>::const_iterator find(
      const ClientMmoCommandToken& command) const noexcept {
    return std::find_if(pending_.begin(), pending_.end(),
                        [&command](const auto& value) {
                          return value.command == command;
                        });
  }
  [[nodiscard]] static std::string submissionRejectionMessage(
      const ClientMmoSubmitStatus status) {
    switch(status) {
      case ClientMmoSubmitStatus::Disabled:
        return "Inventory command was not submitted because the MMO bridge is disabled";
      case ClientMmoSubmitStatus::InvalidIntent:
        return "Inventory command was rejected by client-side validation";
      case ClientMmoSubmitStatus::UnsupportedIntent:
        return "Inventory command is not supported by the active server capability set";
      case ClientMmoSubmitStatus::QueueFull:
        return "Inventory command was not submitted because the client queue is full";
      case ClientMmoSubmitStatus::TransportError:
        return "Inventory command was not submitted because the transport is unavailable";
      case ClientMmoSubmitStatus::Accepted:
        break;
    }
    return "Inventory command submission failed";
  }

  [[nodiscard]] static constexpr bool isResyncRequired(
      const ClientMmoCommandCompletion& completion) noexcept {
    using Code = Mmo::ProtocolV2::CommandRejectionCode;
    return completion.status == ClientMmoCommandCompletionStatus::Rejected &&
           static_cast<Code>(completion.rejectionCode) ==
               Code::AggregateRevisionMismatch;
  }

  void clearResyncRequirement() noexcept {
    resyncRequired_ = false;
    resyncExpectedInventoryRevision_ = 0U;
    resyncExpectedEquipmentRevision_ = 0U;
    resyncInventoryReplacementObserved_ = false;
    resyncEquipmentReplacementObserved_ = false;
    lastRejection_.clear();
  }

  void clearResyncAfterReplacementIfComplete() noexcept {
    if(!resyncInventoryReplacementObserved_ ||
       (resyncExpectedEquipmentRevision_ != 0U &&
        !resyncEquipmentReplacementObserved_)) {
      return;
    }
    clearResyncRequirement();
    bumpChangeRevision();
  }

  void bumpChangeRevision() noexcept {
    ++changeRevision_;
    if(changeRevision_ == 0U)
      changeRevision_ = 1U;
  }

  [[nodiscard]] static std::string rejectionMessage(
      const ClientMmoCommandCompletion& completion) {
    switch(completion.status) {
      case ClientMmoCommandCompletionStatus::Rejected: {
        using Code = Mmo::ProtocolV2::CommandRejectionCode;
        const auto code = static_cast<Code>(completion.rejectionCode);
        const char* reason = "Unknown rejection";
        switch(code) {
          case Code::None: reason = "No rejection reason supplied"; break;
          case Code::UnsupportedProtocol: reason = "Client/server protocol mismatch"; break;
          case Code::InvalidHeader: reason = "Invalid command header"; break;
          case Code::ConnectionNotFound: reason = "Connection is no longer active"; break;
          case Code::StaleRoute: reason = "Command belongs to an obsolete world route"; break;
          case Code::RouteStageRejected: reason = "Character is not ready for this action"; break;
          case Code::InvalidTargetHandle: reason = "Target no longer exists"; break;
          case Code::TargetOutsideBoundWorld: reason = "Target belongs to another world instance"; break;
          case Code::InvalidPayloadFingerprint: reason = "Command payload fingerprint is invalid"; break;
          case Code::SequenceAlreadySeen: reason = "Command sequence was already processed"; break;
          case Code::SequenceTooOld: reason = "Command sequence is too old"; break;
          case Code::IdempotencyConflict: reason = "Command key was reused for different data"; break;
          case Code::AggregateRevisionMismatch: reason = "Character state changed; inventory was refreshed"; break;
          case Code::AdmissionCapacityExceeded: reason = "Server command queue is full"; break;
          case Code::CapabilityNotNegotiated: reason = "Server does not expose this gameplay capability"; break;
          case Code::DomainRejected: reason = "Gameplay rules rejected this action"; break;
          case Code::InvalidPayload: reason = "Inventory command contains invalid data"; break;
          case Code::TargetRevisionMismatch: reason = "Target changed before the action was applied"; break;
          case Code::ActionNotAllowed: reason = "This action is not allowed in the current state"; break;
          case Code::OutOfRange: reason = "Target is out of range"; break;
          case Code::ResourceNotFound: reason = "Item or inventory resource was not found"; break;
          case Code::Busy: reason = "Character or target is busy"; break;
          case Code::CooldownActive: reason = "Action is still on cooldown"; break;
        }
        return std::string("Server rejected inventory command: ") + reason +
               " (code " + std::to_string(completion.rejectionCode) + ")";
      }
      case ClientMmoCommandCompletionStatus::CancelledByRouteChange:
        return "Inventory command cancelled by route change";
      case ClientMmoCommandCompletionStatus::TimedOut:
        return "Inventory command timed out";
      case ClientMmoCommandCompletionStatus::ConnectionLost:
        return "Inventory command cancelled because the connection was lost";
      case ClientMmoCommandCompletionStatus::TransportClosed:
        return "Inventory command cancelled because the transport was closed";
      case ClientMmoCommandCompletionStatus::Applied:
        break;
    }
    return {};
  }

  std::size_t capacity_ = 64U;
  std::vector<ServerInventoryPendingCommand> pending_;
  std::string lastRejection_;
  std::uint64_t changeRevision_ = 1U;
  std::uint64_t resyncExpectedInventoryRevision_ = 0U;
  std::uint64_t resyncExpectedEquipmentRevision_ = 0U;
  bool resyncInventoryReplacementObserved_ = false;
  bool resyncEquipmentReplacementObserved_ = false;
  bool resyncRequired_ = false;
};

class ServerInventoryPresentationState final {
 public:
  [[nodiscard]] ServerInventoryApplyStatus install(
      const ServerInventorySnapshot& inventory,
      const ServerEquipmentSnapshot& equipment) {
    ServerInventoryReadModel nextInventory = inventory_;
    ServerEquipmentReadModel nextEquipment = equipment_;
    const auto inventoryStatus = nextInventory.install(inventory);
    if(inventoryStatus != ServerInventoryApplyStatus::Applied &&
       inventoryStatus != ServerInventoryApplyStatus::Duplicate) {
      return inventoryStatus;
    }
    const auto equipmentStatus = nextEquipment.install(equipment);
    if(equipmentStatus != ServerInventoryApplyStatus::Applied &&
       equipmentStatus != ServerInventoryApplyStatus::Duplicate) {
      return equipmentStatus;
    }
    for(const auto& binding : nextEquipment.bindings()) {
      if(!binding.has_value())
        continue;
      const auto* stack = nextInventory.find(binding->item);
      if(stack == nullptr || stack->itemRevision != binding->itemRevision)
        return ServerInventoryApplyStatus::Invalid;
    }
    inventory_ = std::move(nextInventory);
    equipment_ = std::move(nextEquipment);
    if(inventoryStatus == ServerInventoryApplyStatus::Applied)
      pending_.observeAuthoritativeInventoryReplacement();
    if(equipmentStatus == ServerInventoryApplyStatus::Applied)
      pending_.observeAuthoritativeEquipmentReplacement();
    pending_.reconcile(inventory_.revision(), equipment_.revision());
    return inventoryStatus == ServerInventoryApplyStatus::Duplicate &&
                   equipmentStatus == ServerInventoryApplyStatus::Duplicate
               ? ServerInventoryApplyStatus::Duplicate
               : ServerInventoryApplyStatus::Applied;
  }

  [[nodiscard]] ServerInventoryApplyStatus installInventory(
      const ServerInventorySnapshot& inventory) {
    const auto status = inventory_.install(inventory);
    if(status == ServerInventoryApplyStatus::Applied) {
      pending_.observeAuthoritativeInventoryReplacement();
      pending_.reconcile(inventory_.revision(), equipment_.revision());
    }
    return status;
  }

  [[nodiscard]] ServerInventoryApplyStatus installEquipment(
      const ServerEquipmentSnapshot& equipment) {
    const auto status = equipment_.install(equipment);
    if(status == ServerInventoryApplyStatus::Applied) {
      pending_.observeAuthoritativeEquipmentReplacement();
      pending_.reconcile(inventory_.revision(), equipment_.revision());
    }
    return status;
  }

  [[nodiscard]] ServerInventoryApplyStatus apply(
      const ServerInventoryDelta& delta) {
    const auto status = inventory_.apply(delta);
    if(status == ServerInventoryApplyStatus::Applied)
      pending_.reconcile(inventory_.revision(), equipment_.revision());
    return status;
  }
  [[nodiscard]] ServerInventoryApplyStatus apply(
      const ServerEquipmentSlotChanged& change) noexcept {
    const auto status = equipment_.apply(change);
    if(status == ServerInventoryApplyStatus::Applied)
      pending_.reconcile(inventory_.revision(), equipment_.revision());
    return status;
  }

  [[nodiscard]] ServerInventoryApplyStatus applyAuthoritative(
      ServerInventoryDelta delta) {
    const auto status = inventory_.applyAuthoritative(std::move(delta));
    if(status == ServerInventoryApplyStatus::Applied)
      pending_.reconcile(inventory_.revision(), equipment_.revision());
    return status;
  }

  [[nodiscard]] ServerInventoryApplyStatus applyAuthoritative(
      ServerEquipmentSlotChanged change) noexcept {
    const auto status = equipment_.applyAuthoritative(std::move(change));
    if(status == ServerInventoryApplyStatus::Applied)
      pending_.reconcile(inventory_.revision(), equipment_.revision());
    return status;
  }

  void complete(const ClientMmoCommandCompletion& completion) {
    if((completion.status == ClientMmoCommandCompletionStatus::ConnectionLost ||
        completion.status == ClientMmoCommandCompletionStatus::TransportClosed ||
        completion.status == ClientMmoCommandCompletionStatus::CancelledByRouteChange) &&
       pending_.pending(completion.command)) {
      reset();
      return;
    }
    pending_.complete(completion);
    pending_.reconcile(inventory_.revision(), equipment_.revision());
  }
  [[nodiscard]] bool markPending(ServerInventoryPendingCommand command) {
    return pending_.add(std::move(command));
  }
  void rejectSubmission(const ClientMmoSubmitStatus status) {
    pending_.rejectSubmission(status);
  }
  void reset() noexcept {
    inventory_.reset();
    equipment_.reset();
    pending_.reset();
    ++replacementRevision_;
    if(replacementRevision_ == 0U)
      replacementRevision_ = 1U;
  }

  [[nodiscard]] const ServerInventoryReadModel& inventory() const noexcept {
    return inventory_;
  }
  [[nodiscard]] const ServerEquipmentReadModel& equipment() const noexcept {
    return equipment_;
  }
  [[nodiscard]] ServerInventoryPendingState& pending() noexcept {
    return pending_;
  }
  [[nodiscard]] const ServerInventoryPendingState& pending() const noexcept {
    return pending_;
  }
  [[nodiscard]] bool ready() const noexcept {
    return inventory_.ready() && equipment_.ready();
  }
  [[nodiscard]] std::uint64_t replacementRevision() const noexcept {
    return replacementRevision_;
  }

 private:
  ServerInventoryReadModel inventory_;
  ServerEquipmentReadModel equipment_;
  ServerInventoryPendingState pending_;
  std::uint64_t replacementRevision_ = 0U;
};

} // namespace Mmo::ClientPresentation
