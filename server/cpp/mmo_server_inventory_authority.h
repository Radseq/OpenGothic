#pragma once

#include <cstdint>
#include <string_view>

namespace Mmo::Server::InventoryAuthority {

inline constexpr std::int64_t MinAmount = 1;
inline constexpr std::int64_t MaxStackAmount = 1000000;
inline constexpr std::int64_t MinBagIndex = -1;
inline constexpr std::int64_t MaxBagIndex = 1000000;
inline constexpr std::int64_t MinSymbol = 0;
inline constexpr std::int64_t MaxSymbol = 10000000;
inline constexpr std::size_t MaxOwnerKeyBytes = 512;
inline constexpr std::size_t MaxCharacterKeyBytes = 191;
inline constexpr std::size_t MaxEquipmentSlotBytes = 64;
inline constexpr std::size_t MaxWorldItemKeyBytes = 512;

struct ValidationResult final {
  bool accepted = true;
  const char* reason = "ok";
};

struct StackAmountInput final {
  std::int64_t amount = 1;
  std::int64_t bagIndex = -1;
};

struct TransferInput final {
  std::string_view sourceCharacterKey;
  std::string_view targetCharacterKey;
  std::int64_t amount = 1;
};

struct WorldInventoryInput final {
  std::string_view ownerKey;
  std::int64_t amount = 1;
  std::int64_t bagIndex = -1;
};

struct WorldItemInput final {
  std::string_view entityKey;
  std::int64_t itemSymbol = -1;
  std::int64_t amount = 1;
  std::int64_t bagIndex = -1;
};

struct EquipmentInput final {
  std::string_view slot;
};

[[nodiscard]] constexpr bool validTextLen(std::string_view text, std::size_t maxLen) noexcept {
  return !text.empty() && text.size() <= maxLen;
}

[[nodiscard]] constexpr bool validAmount(std::int64_t amount) noexcept {
  return amount >= MinAmount && amount <= MaxStackAmount;
}

[[nodiscard]] constexpr bool validBagIndex(std::int64_t bagIndex) noexcept {
  return bagIndex >= MinBagIndex && bagIndex <= MaxBagIndex;
}

[[nodiscard]] constexpr bool validSymbol(std::int64_t symbol) noexcept {
  return symbol >= MinSymbol && symbol <= MaxSymbol;
}

[[nodiscard]] constexpr ValidationResult validateStackAmount(const StackAmountInput& input) noexcept {
  if(!validAmount(input.amount))
    return {false, "item_amount_invalid"};
  if(!validBagIndex(input.bagIndex))
    return {false, "item_bag_index_invalid"};
  return {};
}

[[nodiscard]] constexpr ValidationResult validateTransfer(const TransferInput& input) noexcept {
  if(!validTextLen(input.sourceCharacterKey, MaxCharacterKeyBytes) ||
     !validTextLen(input.targetCharacterKey, MaxCharacterKeyBytes))
    return {false, "item_transfer_owner_missing"};
  if(input.sourceCharacterKey == input.targetCharacterKey)
    return {false, "item_transfer_same_owner"};
  return validateStackAmount({input.amount, -1});
}

[[nodiscard]] constexpr ValidationResult validateWorldInventoryTake(const WorldInventoryInput& input) noexcept {
  if(!validTextLen(input.ownerKey, MaxOwnerKeyBytes))
    return {false, "world_inventory_owner_missing"};
  return validateStackAmount({input.amount, input.bagIndex});
}

[[nodiscard]] constexpr ValidationResult validateWorldItemPickup(const WorldItemInput& input) noexcept {
  if(!input.entityKey.empty() && input.entityKey.size() > MaxWorldItemKeyBytes)
    return {false, "world_item_key_too_large"};
  if(input.entityKey.empty() && !validSymbol(input.itemSymbol))
    return {false, "world_item_identity_missing"};
  return validateStackAmount({input.amount, input.bagIndex});
}

[[nodiscard]] constexpr ValidationResult validateDrop(const WorldItemInput& input) noexcept {
  if(!validTextLen(input.entityKey, MaxWorldItemKeyBytes))
    return {false, "drop_world_item_key_missing"};
  return validateStackAmount({input.amount, -1});
}

[[nodiscard]] constexpr ValidationResult validateEquipment(const EquipmentInput& input) noexcept {
  if(!validTextLen(input.slot, MaxEquipmentSlotBytes))
    return {false, "equipment_slot_missing"};
  return {};
}

} // namespace Mmo::Server::InventoryAuthority
