#pragma once

#include "game/mmoserverinventoryreadmodel.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace Mmo::ClientPresentation {

struct ServerInventoryPageFingerprint final {
  std::uint64_t replacementRevision = 0U;
  std::uint64_t inventoryRevision = 0U;
  std::uint64_t equipmentRevision = 0U;
  std::uint64_t feedbackRevision = 0U;

  [[nodiscard]] friend constexpr bool operator==(
      const ServerInventoryPageFingerprint&,
      const ServerInventoryPageFingerprint&) noexcept = default;
};

class ServerInventoryPageModel final {
 public:
  explicit ServerInventoryPageModel(
      const ServerInventoryPresentationState& state) noexcept
      : state_(state) {}

  [[nodiscard]] bool ready() const noexcept { return state_.ready(); }
  [[nodiscard]] bool actionsEnabled() const noexcept {
    return ready() && state_.pending().canSubmit();
  }
  [[nodiscard]] ServerInventoryFeedbackStatus feedbackStatus() const noexcept {
    return state_.pending().feedbackStatus();
  }
  [[nodiscard]] std::span<const ServerInventoryStack> stacks() const noexcept {
    return state_.inventory().stacks();
  }
  [[nodiscard]] const ServerInventoryStack* stackAt(
      const std::size_t index) const noexcept {
    const auto values = stacks();
    return index < values.size() ? &values[index] : nullptr;
  }
  [[nodiscard]] std::optional<ServerInventoryPendingPhase> pendingPhase(
      const ClientItemStackHandle handle) const noexcept {
    return state_.pending().phase(handle);
  }
  [[nodiscard]] const ServerEquipmentReadModel& equipment() const noexcept {
    return state_.equipment();
  }
  [[nodiscard]] const std::string& rejectionMessage() const noexcept {
    return state_.pending().lastRejection();
  }
  [[nodiscard]] ServerInventoryPageFingerprint fingerprint() const noexcept {
    return {
        .replacementRevision = state_.replacementRevision(),
        .inventoryRevision = state_.inventory().revision(),
        .equipmentRevision = state_.equipment().revision(),
        .feedbackRevision = state_.pending().changeRevision(),
    };
  }

  [[nodiscard]] std::optional<ClientUseItemRequest> useRequest(
      const ClientItemStackHandle item) const noexcept {
    if(!canActOn(item) || state_.equipment().slotOf(item).has_value())
      return std::nullopt;
    return ClientUseItemRequest{
        .item = item,
        .target = std::nullopt,
        .expectedInventoryRevision = state_.inventory().revision(),
        .expectedTargetRevision = 0U,
    };
  }

  [[nodiscard]] std::optional<ClientUnequipItemRequest> unequipRequest(
      const ClientItemStackHandle item) const noexcept {
    if(!canActOn(item))
      return std::nullopt;
    const auto slot = state_.equipment().slotOf(item);
    if(!slot.has_value() || state_.pending().pending(*slot))
      return std::nullopt;
    return ClientUnequipItemRequest{
        .slot = *slot,
        .expectedEquipmentRevision = state_.equipment().revision(),
        .expectedInventoryRevision = state_.inventory().revision(),
    };
  }

  [[nodiscard]] std::optional<ClientEquipItemRequest> equipRequest(
      const ClientItemStackHandle item,
      const ClientEquipmentSlot slot) const noexcept {
    if(!ServerEquipmentReadModel::knownSlot(slot) || !canActOn(item) ||
       state_.pending().pending(slot)) {
      return std::nullopt;
    }
    return ClientEquipItemRequest{
        .item = item,
        .slot = slot,
        .expectedInventoryRevision = state_.inventory().revision(),
        .expectedEquipmentRevision = state_.equipment().revision(),
    };
  }

  [[nodiscard]] std::optional<ClientDropItemRequest> dropRequest(
      const ClientItemStackHandle item,
      const std::uint32_t amount,
      const ClientPosition position) const noexcept {
    const auto* stack = canActOn(item) ? state_.inventory().find(item) : nullptr;
    if(stack == nullptr || amount == 0U || amount > stack->quantity)
      return std::nullopt;
    return ClientDropItemRequest{
        .item = item,
        .amount = amount,
        .proposedPosition = position,
        .expectedInventoryRevision = state_.inventory().revision(),
    };
  }

  [[nodiscard]] std::optional<ClientSplitStackRequest> splitRequest(
      const ClientItemStackHandle item,
      const std::uint32_t amount) const noexcept {
    const auto* stack = canActOn(item) ? state_.inventory().find(item) : nullptr;
    if(stack == nullptr || amount == 0U || amount >= stack->quantity)
      return std::nullopt;
    return ClientSplitStackRequest{
        .item = item,
        .amount = amount,
        .expectedInventoryRevision = state_.inventory().revision(),
    };
  }

  [[nodiscard]] std::optional<ClientMergeStackRequest> mergeRequest(
      const ClientItemStackHandle source,
      const ClientItemStackHandle destination,
      const std::uint32_t amount) const noexcept {
    if(source == destination || !canActOn(source) || !canActOn(destination))
      return std::nullopt;
    const auto* sourceStack = state_.inventory().find(source);
    if(sourceStack == nullptr || amount == 0U || amount > sourceStack->quantity)
      return std::nullopt;
    return ClientMergeStackRequest{
        .source = source,
        .destination = destination,
        .amount = amount,
        .expectedInventoryRevision = state_.inventory().revision(),
    };
  }

 private:
  [[nodiscard]] bool canActOn(
      const ClientItemStackHandle item) const noexcept {
    return actionsEnabled() && item.valid() &&
           state_.inventory().find(item) != nullptr &&
           !state_.pending().pending(item);
  }

  const ServerInventoryPresentationState& state_;
};

} // namespace Mmo::ClientPresentation
