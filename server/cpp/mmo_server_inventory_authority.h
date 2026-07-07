#pragma once

#include <array>
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
inline constexpr std::size_t MaxCurrencyKeyBytes = 128;
inline constexpr std::int64_t MaxTradePriceAbs = 1000000000000LL;
inline constexpr std::size_t MaxItemUseConditions = 3;

inline constexpr std::uint32_t ItmCatNone = 1u << 0u;
inline constexpr std::uint32_t ItmCatMeleeWeapon = 1u << 1u;
inline constexpr std::uint32_t ItmCatRangedWeapon = 1u << 2u;
inline constexpr std::uint32_t ItmCatMunition = 1u << 3u;
inline constexpr std::uint32_t ItmCatArmor = 1u << 4u;
inline constexpr std::uint32_t ItmCatFood = 1u << 5u;
inline constexpr std::uint32_t ItmCatDocs = 1u << 6u;
inline constexpr std::uint32_t ItmCatPotion = 1u << 7u;
inline constexpr std::uint32_t ItmCatLight = 1u << 8u;
inline constexpr std::uint32_t ItmCatRune = 1u << 9u;
inline constexpr std::uint32_t ItmRing = 1u << 11u;
inline constexpr std::uint32_t ItmMission = 1u << 12u;
inline constexpr std::uint32_t ItmShield = 1u << 18u;
inline constexpr std::uint32_t ItmAmulet = 1u << 22u;
inline constexpr std::uint32_t ItmBelt = 1u << 24u;
inline constexpr std::uint32_t ItmTorch = 1u << 28u;
inline constexpr std::uint32_t ItmCatMagic = 1u << 31u;

inline constexpr std::string_view SlotWeaponMelee = "weapon_melee";
inline constexpr std::string_view SlotWeaponRanged = "weapon_ranged";
inline constexpr std::string_view SlotShield = "shield";
inline constexpr std::string_view SlotArmor = "armor";
inline constexpr std::string_view SlotBelt = "belt";
inline constexpr std::string_view SlotAmulet = "amulet";
inline constexpr std::string_view SlotRingLeft = "ring_left";
inline constexpr std::string_view SlotRingRight = "ring_right";
inline constexpr std::string_view SlotRune = "rune";
inline constexpr std::string_view SlotTorch = "torch";
inline constexpr std::string_view SlotUnknown = "unknown";

enum class GothicAttribute : std::int64_t {
  Hitpoints = 0,
  HitpointsMax = 1,
  Mana = 2,
  ManaMax = 3,
  Strength = 4,
  Dexterity = 5,
  RegenerateHp = 6,
  RegenerateMana = 7,
};

inline constexpr std::int64_t TalentMage = 7;

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

struct ConsumeInput final {
  std::int64_t amount = 1;
};

struct TradeInput final {
  std::string_view npcKey;
  std::string_view currencyKey;
  std::int64_t amount = 1;
  std::int64_t priceTotal = 0;
  std::int64_t bagIndex = -1;
};

struct ItemEquipInput final {
  std::string_view slot;
  std::string_view classification;
  std::uint32_t mainFlag = 0;
  std::uint32_t itemFlags = 0;
  bool hasGothicFlags = false;
};

struct CharacterUseStats final {
  std::int64_t healthCurrent = 0;
  std::int64_t healthMax = 0;
  std::int64_t manaCurrent = 0;
  std::int64_t manaMax = 0;
  std::int64_t strength = 0;
  std::int64_t dexterity = 0;
  std::int64_t regenerateHp = 0;
  std::int64_t regenerateMana = 0;
  std::int64_t mageCircle = 0;
};

struct ItemUseRequirements final {
  std::array<std::int64_t, MaxItemUseConditions> conditionAttributes = {};
  std::array<std::int64_t, MaxItemUseConditions> conditionValues = {};
  std::int64_t magicCircle = 0;
};

struct ItemUseCheckResult final {
  bool accepted = true;
  const char* reason = "ok";
  std::int64_t requiredAttribute = -1;
  std::int64_t currentValue = 0;
  std::int64_t requiredValue = 0;
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

[[nodiscard]] constexpr bool validTradePrice(std::int64_t price) noexcept {
  return price >= -MaxTradePriceAbs && price <= MaxTradePriceAbs;
}

[[nodiscard]] constexpr bool isKnownGothicAttribute(std::int64_t attribute) noexcept {
  return attribute >= static_cast<std::int64_t>(GothicAttribute::Hitpoints) &&
         attribute <= static_cast<std::int64_t>(GothicAttribute::RegenerateMana);
}

[[nodiscard]] constexpr std::int64_t attributeValue(const CharacterUseStats& stats,
                                                    std::int64_t attribute) noexcept {
  switch(static_cast<GothicAttribute>(attribute)) {
    case GothicAttribute::Hitpoints: return stats.healthCurrent;
    case GothicAttribute::HitpointsMax: return stats.healthMax;
    case GothicAttribute::Mana: return stats.manaCurrent;
    case GothicAttribute::ManaMax: return stats.manaMax;
    case GothicAttribute::Strength: return stats.strength;
    case GothicAttribute::Dexterity: return stats.dexterity;
    case GothicAttribute::RegenerateHp: return stats.regenerateHp;
    case GothicAttribute::RegenerateMana: return stats.regenerateMana;
  }
  return 0;
}

[[nodiscard]] constexpr bool hasFlag(std::uint32_t mask, std::uint32_t flag) noexcept {
  return (mask & flag) != 0u;
}

[[nodiscard]] constexpr bool isRingSlot(std::string_view slot) noexcept {
  return slot == SlotRingLeft || slot == SlotRingRight;
}

[[nodiscard]] constexpr bool isKnownEquipmentSlot(std::string_view slot) noexcept {
  return slot == SlotWeaponMelee ||
         slot == SlotWeaponRanged ||
         slot == SlotShield ||
         slot == SlotArmor ||
         slot == SlotBelt ||
         slot == SlotAmulet ||
         slot == SlotRingLeft ||
         slot == SlotRingRight ||
         slot == SlotRune ||
         slot == SlotTorch ||
         slot == SlotUnknown;
}

[[nodiscard]] constexpr std::string_view normalizedNumericEquipmentSlot(std::int64_t slot) noexcept {
  if(slot == 1)
    return SlotWeaponMelee;
  if(slot == 2)
    return SlotWeaponRanged;
  if(slot >= 3 && slot <= 10)
    return SlotRune;
  return SlotUnknown;
}

[[nodiscard]] constexpr bool isUsableClassification(std::string_view classification) noexcept {
  return classification == "weapon" ||
         classification == "armor" ||
         classification == "jewelry" ||
         classification == "rune" ||
         classification == "scroll" ||
         classification == "unknown";
}

[[nodiscard]] constexpr bool slotMatchesClassification(std::string_view slot,
                                                       std::string_view classification) noexcept {
  if(classification == "weapon")
    return slot == SlotWeaponMelee || slot == SlotWeaponRanged || slot == SlotShield;
  if(classification == "armor")
    return slot == SlotArmor;
  if(classification == "jewelry")
    return slot == SlotBelt || slot == SlotAmulet || isRingSlot(slot);
  if(classification == "rune" || classification == "scroll")
    return slot == SlotRune;
  return true;
}

[[nodiscard]] constexpr bool slotMatchesGothicUse(std::string_view slot,
                                                  std::uint32_t mainFlag,
                                                  std::uint32_t itemFlags) noexcept {
  // This mirrors Inventory::use ordering from the client: special item flags
  // such as shield/belt/amulet/ring are checked around main category flags.
  if(hasFlag(itemFlags, ItmShield))
    return slot == SlotShield;
  if(hasFlag(mainFlag, ItmCatMeleeWeapon))
    return slot == SlotWeaponMelee;
  if(hasFlag(mainFlag, ItmCatRangedWeapon))
    return slot == SlotWeaponRanged;
  if(hasFlag(mainFlag, ItmCatRune))
    return slot == SlotRune;
  if(hasFlag(mainFlag, ItmCatArmor))
    return slot == SlotArmor;
  if(hasFlag(itemFlags, ItmBelt))
    return slot == SlotBelt;
  if(hasFlag(itemFlags, ItmAmulet))
    return slot == SlotAmulet;
  if(hasFlag(itemFlags, ItmRing))
    return isRingSlot(slot);
  if(hasFlag(itemFlags, ItmTorch))
    return slot == SlotTorch;
  return false;
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
  if(!isKnownEquipmentSlot(input.slot))
    return {false, "equipment_slot_unknown"};
  return {};
}

[[nodiscard]] constexpr ValidationResult validateConsume(const ConsumeInput& input) noexcept {
  return validateStackAmount({input.amount, -1});
}

[[nodiscard]] constexpr ValidationResult validateTrade(const TradeInput& input) noexcept {
  if(!validTextLen(input.npcKey, MaxOwnerKeyBytes))
    return {false, "trade_npc_key_missing"};
  if(!validTextLen(input.currencyKey, MaxCurrencyKeyBytes))
    return {false, "trade_currency_key_missing"};
  if(!validTradePrice(input.priceTotal))
    return {false, "trade_price_out_of_range"};
  return validateStackAmount({input.amount, input.bagIndex});
}

[[nodiscard]] constexpr ItemUseCheckResult validateItemUseRequirements(
    const CharacterUseStats& stats,
    const ItemUseRequirements& requirements) noexcept {
  for(std::size_t i = 0; i < MaxItemUseConditions; ++i) {
    const auto required = requirements.conditionValues[i];
    if(required == 0)
      continue;

    const auto attribute = requirements.conditionAttributes[i];
    if(!isKnownGothicAttribute(attribute))
      return {false, "item_use_condition_attribute_unknown", attribute, 0, required};

    const auto current = attributeValue(stats, attribute);
    if(current < required)
      return {false, "item_use_condition_failed", attribute, current, required};
  }

  if(stats.mageCircle < requirements.magicCircle)
    return {false, "item_magic_circle_failed", TalentMage, stats.mageCircle, requirements.magicCircle};

  return {};
}

[[nodiscard]] constexpr ValidationResult validateEquipItem(const ItemEquipInput& input) noexcept {
  if(auto slot = validateEquipment({input.slot}); !slot.accepted)
    return slot;
  if(input.slot == SlotUnknown)
    return {false, "equipment_slot_unknown"};
  if(input.hasGothicFlags) {
    if(!slotMatchesGothicUse(input.slot, input.mainFlag, input.itemFlags))
      return {false, "equipment_slot_rejected_by_item_flags"};
    return {};
  }
  if(!input.classification.empty() && !isUsableClassification(input.classification))
    return {false, "equipment_item_not_equipable"};
  if(!input.classification.empty() && !slotMatchesClassification(input.slot, input.classification))
    return {false, "equipment_slot_rejected_by_classification"};
  return {};
}

} // namespace Mmo::Server::InventoryAuthority
