#include "mmosemantichooks_internal.h"

#include <array>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "mmoclientadapter.h"
#include "mmoclientbridge.h"
#include "world/world.h"
#include "world/objects/interactive.h"
#include "world/objects/item.h"
#include "world/objects/npc.h"
#include "world/waypoint.h"
#include "commandline.h"
#include "utils/versioninfo.h"

namespace Mmo::Hooks::Detail {

void onWorldItemPickedUp(Npc& actor,
                         const Item& inventoryItem,
                         std::uint32_t sourceWorldItemPersistentId,
                         std::size_t sourceItemSymbol,
                         std::size_t sourceAmount,
                         const char* sourceLocation) noexcept {
  if((!isClientMmoDiagnosticsEnabled() && !isServerBoundClientModeEnabled()) || !shouldCapturePlayerAction(actor))
    return;
  auto& world = actor.world();
  auto target = worldItemKey(world.name(), sourceWorldItemPersistentId, sourceItemSymbol);

  if(isServerBoundClientModeEnabled()) {
    const auto actorIdentity = actorKey(actor);
    const auto templateKey = itemTemplateKey(sourceItemSymbol);
    const auto actorPos = actor.position();
    ClientInventoryRequest request;
    request.clientTick = world.tickCount();
    request.action = ClientInventoryAction::PickupWorldItem;
    request.itemSymbol = sourceItemSymbol;
    request.inventoryItemSymbol = inventoryItem.clsId();
    request.sourceWorldItemPersistentId = sourceWorldItemPersistentId;
    request.amount = sourceAmount;
    request.actorPosition = {.x = actorPos.x, .y = actorPos.y, .z = actorPos.z};
    request.targetKey = target;
    request.source = sourceLocation != nullptr ? std::string_view(sourceLocation)
                                               : std::string_view("unknown");
    request.actorKey = actorIdentity;
    request.itemTemplateKey = templateKey;
    request.world = world.name();
    (void)submitClientInventory(request);
  }
  if(!isClientMmoDiagnosticsEnabled())
    return;

  std::string payload;
  payload.reserve(512);
  payload.append("{\"source\":"); appendEscaped(payload, sourceLocation);
  payload.append(",\"actor_key\":"); appendEscaped(payload, actorKey(actor));
  payload.append(",\"target_key\":"); appendEscaped(payload, target);
  payload.append(",\"item_template_key\":"); appendEscaped(payload, itemTemplateKey(sourceItemSymbol));
  payload.append(",\"source_world_item_persistent_id\":"); appendUInt(payload, sourceWorldItemPersistentId);
  payload.append(",\"item_symbol\":"); appendUInt(payload, sourceItemSymbol);
  payload.append(",\"inventory_item_symbol\":"); appendUInt(payload, inventoryItem.clsId());
  payload.append(",\"amount\":"); appendUInt(payload, sourceAmount);
  appendWorld(payload, world);
  appendVec3(payload, "actor_position", actor.position());
  payload.push_back('}');

  if(isClientMmoDiagnosticsEnabled())
    submit(SemanticActionKind::PickupWorldItem, std::move(target), std::move(payload), world.tickCount());
}

void onInventoryTransfer(World& world,
                         const Npc* sourceNpc,
                         std::size_t itemSymbol,
                         std::uint32_t sourceItemPersistentId,
                         std::size_t amount,
                         bool movedWholeInstance,
                         const char* sourceLocation) noexcept {
  if(!isClientMmoDiagnosticsEnabled() || amount == 0 || !shouldCaptureTransfer(world, sourceNpc))
    return;
  std::string target = itemTemplateKey(itemSymbol);
  target.append(":transfer:");
  appendUInt(target, sourceItemPersistentId);

  std::string payload;
  payload.reserve(448);
  payload.append("{\"source\":"); appendEscaped(payload, sourceLocation);
  if(sourceNpc != nullptr) {
    payload.append(",\"source_actor_key\":"); appendEscaped(payload, actorKey(*sourceNpc));
    payload.append(",\"source_actor_symbol\":"); appendUInt(payload, sourceNpc->instanceSymbol());
    payload.append(",\"source_actor_persistent_id\":"); appendUInt(payload, sourceNpc->persistentId());
    }
  payload.append(",\"item_template_key\":"); appendEscaped(payload, itemTemplateKey(itemSymbol));
  payload.append(",\"item_symbol\":"); appendUInt(payload, itemSymbol);
  payload.append(",\"source_item_persistent_id\":"); appendUInt(payload, sourceItemPersistentId);
  payload.append(",\"amount\":"); appendUInt(payload, amount);
  payload.append(",\"moved_whole_instance\":"); payload.append(movedWholeInstance ? "true" : "false");
  appendWorld(payload, world);
  payload.push_back('}');

  submit(SemanticActionKind::TransferCharacterItem, std::move(target), std::move(payload), world.tickCount());
}

void onItemEquipped(Npc& actor,
                    const Item& item,
                    std::uint8_t slot,
                    const char* sourceLocation) noexcept {
  if((!isClientMmoDiagnosticsEnabled() && !isServerBoundClientModeEnabled()) || !shouldCapturePlayerAction(actor))
    return;
  auto& world = actor.world();
  auto target = itemTemplateKey(item.clsId());
  target.append(":equip:");
  appendUInt(target, actor.persistentId());
  target.push_back(':');
  appendUInt(target, slot);

  if(isServerBoundClientModeEnabled()) {
    const auto actorIdentity = actorKey(actor);
    const auto templateKey = itemTemplateKey(item.clsId());
    const auto equipmentSlot = std::to_string(slot);
    const auto actorPos = actor.position();
    ClientInventoryRequest request;
    request.clientTick = world.tickCount();
    request.action = ClientInventoryAction::EquipCharacterItem;
    request.itemSymbol = item.clsId();
    request.itemPersistentId = item.persistentId();
    request.amount = item.count();
    request.equipmentSlotId = slot;
    request.actorPosition = {.x = actorPos.x, .y = actorPos.y, .z = actorPos.z};
    request.targetKey = target;
    request.source = sourceLocation != nullptr ? std::string_view(sourceLocation)
                                               : std::string_view("unknown");
    request.actorKey = actorIdentity;
    request.itemTemplateKey = templateKey;
    request.equipmentSlot = equipmentSlot;
    request.world = world.name();
    (void)submitClientInventory(request);
  }
  if(!isClientMmoDiagnosticsEnabled())
    return;

  std::string payload;
  payload.reserve(448);
  payload.append("{\"source\":"); appendEscaped(payload, sourceLocation);
  payload.append(",\"actor_key\":"); appendEscaped(payload, actorKey(actor));
  payload.append(",\"item_template_key\":"); appendEscaped(payload, itemTemplateKey(item.clsId()));
  payload.append(",\"item_symbol\":"); appendUInt(payload, item.clsId());
  payload.append(",\"item_persistent_id\":"); appendUInt(payload, item.persistentId());
  payload.append(",\"slot\":"); appendUInt(payload, slot);
  payload.append(",\"amount\":"); appendUInt(payload, item.count());
  appendWorld(payload, world);
  appendVec3(payload, "actor_position", actor.position());
  payload.push_back('}');

  if(isClientMmoDiagnosticsEnabled())
    submit(SemanticActionKind::EquipCharacterItem, std::move(target), std::move(payload), world.tickCount());
}

void onItemUnequipped(Npc& actor,
                      const Item& item,
                      std::uint8_t slot,
                      const char* sourceLocation) noexcept {
  if((!isClientMmoDiagnosticsEnabled() && !isServerBoundClientModeEnabled()) || !shouldCapturePlayerAction(actor))
    return;
  auto& world = actor.world();
  auto target = itemTemplateKey(item.clsId());
  target.append(":unequip:");
  appendUInt(target, actor.persistentId());
  target.push_back(':');
  appendUInt(target, slot);

  if(isServerBoundClientModeEnabled()) {
    const auto actorIdentity = actorKey(actor);
    const auto templateKey = itemTemplateKey(item.clsId());
    const auto equipmentSlot = std::to_string(slot);
    const auto actorPos = actor.position();
    ClientInventoryRequest request;
    request.clientTick = world.tickCount();
    request.action = ClientInventoryAction::UnequipCharacterItem;
    request.itemSymbol = item.clsId();
    request.itemPersistentId = item.persistentId();
    request.amount = item.count();
    request.equipmentSlotId = slot;
    request.actorPosition = {.x = actorPos.x, .y = actorPos.y, .z = actorPos.z};
    request.targetKey = target;
    request.source = sourceLocation != nullptr ? std::string_view(sourceLocation)
                                               : std::string_view("unknown");
    request.actorKey = actorIdentity;
    request.itemTemplateKey = templateKey;
    request.equipmentSlot = equipmentSlot;
    request.world = world.name();
    (void)submitClientInventory(request);
  }
  if(!isClientMmoDiagnosticsEnabled())
    return;

  std::string payload;
  payload.reserve(448);
  payload.append("{\"source\":"); appendEscaped(payload, sourceLocation);
  payload.append(",\"actor_key\":"); appendEscaped(payload, actorKey(actor));
  payload.append(",\"item_template_key\":"); appendEscaped(payload, itemTemplateKey(item.clsId()));
  payload.append(",\"item_symbol\":"); appendUInt(payload, item.clsId());
  payload.append(",\"item_persistent_id\":"); appendUInt(payload, item.persistentId());
  payload.append(",\"slot\":"); appendUInt(payload, slot);
  payload.append(",\"amount\":"); appendUInt(payload, item.count());
  appendWorld(payload, world);
  appendVec3(payload, "actor_position", actor.position());
  payload.push_back('}');

  if(isClientMmoDiagnosticsEnabled())
    submit(SemanticActionKind::UnequipCharacterItem, std::move(target), std::move(payload), world.tickCount());
}

void onContainerInventoryTaken(Npc& actor,
                               Interactive& container,
                               std::size_t itemSymbol,
                               std::uint32_t sourceItemPersistentId,
                               std::size_t amount,
                               const char* sourceLocation) noexcept {
  if((!isClientMmoDiagnosticsEnabled() && !isServerBoundClientModeEnabled()) || amount == 0 || !shouldCapturePlayerAction(actor))
    return;
  auto& world = actor.world();
  auto sourceKey = interactiveEntityKey(world, container);

  std::string target = sourceKey;
  target.append(":take:");
  appendUInt(target, itemSymbol);
  target.push_back(':');
  appendUInt(target, sourceItemPersistentId);

  if(isServerBoundClientModeEnabled()) {
    const auto actorIdentity = actorKey(actor);
    const auto templateKey = itemTemplateKey(itemSymbol);
    const auto actorPos = actor.position();
    ClientInventoryRequest request;
    request.clientTick = world.tickCount();
    request.action = ClientInventoryAction::TakeContainerItem;
    request.itemSymbol = itemSymbol;
    request.sourceItemPersistentId = sourceItemPersistentId;
    request.amount = amount;
    request.actorPosition = {.x = actorPos.x, .y = actorPos.y, .z = actorPos.z};
    request.targetKey = target;
    request.source = sourceLocation != nullptr ? std::string_view(sourceLocation)
                                               : std::string_view("unknown");
    request.actorKey = actorIdentity;
    request.itemTemplateKey = templateKey;
    request.sourceEntityKey = sourceKey;
    request.sourceContainerKey = sourceKey;
    request.containerKey = sourceKey;
    request.world = world.name();
    (void)submitClientInventory(request);
  }
  if(!isClientMmoDiagnosticsEnabled())
    return;

  std::string payload;
  payload.reserve(768);
  payload.append("{\"source\":"); appendEscaped(payload, sourceLocation);
  payload.append(",\"actor_key\":"); appendEscaped(payload, actorKey(actor));
  payload.append(",\"target_key\":"); appendEscaped(payload, target);
  payload.append(",\"source_entity_key\":"); appendEscaped(payload, sourceKey);
  payload.append(",\"source_container_key\":"); appendEscaped(payload, sourceKey);
  payload.append(",\"container_key\":"); appendEscaped(payload, sourceKey);
  payload.append(",\"slot_id\":"); appendUInt(payload, world.mobsiId(&container));
  payload.append(",\"vob_id\":"); appendUInt(payload, container.getId());
  payload.append(",\"tag\":"); appendEscaped(payload, container.tag());
  payload.append(",\"focus_name\":"); appendEscaped(payload, container.focusName());
  payload.append(",\"display_name\":"); appendEscaped(payload, container.displayName());
  payload.append(",\"scheme\":"); appendEscaped(payload, container.schemeName());
  payload.append(",\"container\":"); appendBool(payload, container.isContainer());
  payload.append(",\"item_template_key\":"); appendEscaped(payload, itemTemplateKey(itemSymbol));
  payload.append(",\"item_symbol\":"); appendUInt(payload, itemSymbol);
  payload.append(",\"source_item_persistent_id\":"); appendUInt(payload, sourceItemPersistentId);
  payload.append(",\"amount\":"); appendUInt(payload, amount);
  payload.append(",\"reason\":\"take_container_item\"");
  appendWorld(payload, world);
  appendVec3(payload, "actor_position", actor.position());
  payload.push_back('}');

  if(isClientMmoDiagnosticsEnabled())
    submit(SemanticActionKind::TakeContainerItem, std::move(target), std::move(payload), world.tickCount());
}

void onNpcInventoryLooted(Npc& looter,
                          Npc& sourceNpc,
                          std::size_t itemSymbol,
                          std::uint32_t sourceItemPersistentId,
                          std::size_t amount,
                          const char* sourceLocation) noexcept {
  if((!isClientMmoDiagnosticsEnabled() && !isServerBoundClientModeEnabled()) || amount == 0 || !shouldCapturePlayerAction(looter))
    return;
  if(!sourceNpc.isDead() && !sourceNpc.isUnconscious())
    return;
  auto& world = looter.world();
  auto sourceKey = npcEntityKey(world.name(), sourceNpc.persistentId(), sourceNpc.instanceSymbol());
  std::string target = sourceKey;
  target.append(":loot:");
  appendUInt(target, itemSymbol);
  target.push_back(':');
  appendUInt(target, sourceItemPersistentId);

  if(isServerBoundClientModeEnabled()) {
    const auto actorIdentity = actorKey(looter);
    const auto templateKey = itemTemplateKey(itemSymbol);
    const auto actorPos = looter.position();
    ClientInventoryRequest request;
    request.clientTick = world.tickCount();
    request.action = ClientInventoryAction::LootNpcInventory;
    request.itemSymbol = itemSymbol;
    request.sourceItemPersistentId = sourceItemPersistentId;
    request.amount = amount;
    request.actorPosition = {.x = actorPos.x, .y = actorPos.y, .z = actorPos.z};
    request.targetKey = target;
    request.source = sourceLocation != nullptr ? std::string_view(sourceLocation)
                                               : std::string_view("unknown");
    request.actorKey = actorIdentity;
    request.itemTemplateKey = templateKey;
    request.sourceNpcKey = sourceKey;
    request.sourceEntityKey = sourceKey;
    request.world = world.name();
    (void)submitClientInventory(request);
  }
  if(!isClientMmoDiagnosticsEnabled())
    return;

  std::string payload;
  payload.reserve(768);
  payload.append("{\"source\":"); appendEscaped(payload, sourceLocation);
  appendNpcIdentity(payload, "looter", looter);
  appendNpcIdentity(payload, "source_npc", sourceNpc);
  payload.append(",\"target_key\":"); appendEscaped(payload, target);
  payload.append(",\"source_npc_key\":"); appendEscaped(payload, sourceKey);
  payload.append(",\"item_template_key\":"); appendEscaped(payload, itemTemplateKey(itemSymbol));
  payload.append(",\"item_symbol\":"); appendUInt(payload, itemSymbol);
  payload.append(",\"source_item_persistent_id\":"); appendUInt(payload, sourceItemPersistentId);
  payload.append(",\"amount\":"); appendUInt(payload, amount);
  payload.append(",\"source_dead\":"); payload.append(sourceNpc.isDead() ? "true" : "false");
  payload.append(",\"source_unconscious\":"); payload.append(sourceNpc.isUnconscious() ? "true" : "false");
  payload.append(",\"reason\":\"loot_dead_or_unconscious_npc\"");
  appendWorld(payload, world);
  appendVec3(payload, "looter_position", looter.position());
  appendVec3(payload, "source_npc_position", sourceNpc.position());
  payload.push_back('}');

  if(isClientMmoDiagnosticsEnabled())
    submit(SemanticActionKind::LootNpcInventory, std::move(target), std::move(payload), world.tickCount());
}

void onCharacterItemDropped(Npc& actor,
                            const Item& worldItem,
                            std::size_t itemSymbol,
                            std::uint32_t sourceItemPersistentId,
                            std::size_t amount,
                            const char* sourceLocation) noexcept {
  if((!isClientMmoDiagnosticsEnabled() && !isServerBoundClientModeEnabled()) || amount == 0 || !shouldCapturePlayerAction(actor))
    return;
  auto& world = actor.world();
  auto target = worldItemKey(world.name(), worldItem.persistentId(), itemSymbol);

  if(isServerBoundClientModeEnabled()) {
    const auto actorIdentity = actorKey(actor);
    const auto templateKey = itemTemplateKey(itemSymbol);
    const auto actorPos = actor.position();
    ClientInventoryRequest request;
    request.clientTick = world.tickCount();
    request.action = ClientInventoryAction::DropCharacterItem;
    request.itemSymbol = itemSymbol;
    request.itemPersistentId = sourceItemPersistentId;
    request.worldItemPersistentId = worldItem.persistentId();
    request.amount = amount;
    request.actorPosition = {.x = actorPos.x, .y = actorPos.y, .z = actorPos.z};
    request.targetKey = target;
    request.source = sourceLocation != nullptr ? std::string_view(sourceLocation)
                                               : std::string_view("unknown");
    request.actorKey = actorIdentity;
    request.itemTemplateKey = templateKey;
    request.world = world.name();
    (void)submitClientInventory(request);
  }
  if(!isClientMmoDiagnosticsEnabled())
    return;

  std::string payload;
  payload.reserve(640);
  payload.append("{\"source\":"); appendEscaped(payload, sourceLocation);
  payload.append(",\"actor_key\":"); appendEscaped(payload, actorKey(actor));
  payload.append(",\"target_key\":"); appendEscaped(payload, target);
  payload.append(",\"item_template_key\":"); appendEscaped(payload, itemTemplateKey(itemSymbol));
  payload.append(",\"item_symbol\":"); appendUInt(payload, itemSymbol);
  payload.append(",\"item_persistent_id\":"); appendUInt(payload, sourceItemPersistentId);
  payload.append(",\"source_item_persistent_id\":"); appendUInt(payload, sourceItemPersistentId);
  payload.append(",\"world_item_persistent_id\":"); appendUInt(payload, worldItem.persistentId());
  payload.append(",\"amount\":"); appendUInt(payload, amount);
  payload.append(",\"reason\":\"player_drop_item\"");
  appendWorld(payload, world);
  appendVec3(payload, "actor_position", actor.position());
  appendVec3(payload, "item_position", worldItem.position());
  payload.push_back('}');

  if(isClientMmoDiagnosticsEnabled())
    submit(SemanticActionKind::DropCharacterItem, std::move(target), std::move(payload), world.tickCount());
}

void onTradeBuyFromNpc(Npc& buyer,
                       Npc& vendor,
                       std::size_t itemSymbol,
                       std::uint32_t vendorItemPersistentId,
                       std::size_t amount,
                       std::int32_t unitPrice,
                       std::size_t goldBefore,
                       std::size_t goldAfter,
                       const char* sourceLocation) noexcept {
  if((!isClientMmoDiagnosticsEnabled() && !isServerBoundClientModeEnabled()) || amount == 0 || !shouldCapturePlayerAction(buyer))
    return;
  auto& world = buyer.world();
  auto target = itemTemplateKey(itemSymbol);
  target.append(":buy-from:");
  appendUInt(target, vendor.persistentId());
  target.push_back(':');
  appendUInt(target, vendorItemPersistentId);

  const auto totalPrice = static_cast<std::int64_t>(unitPrice) * static_cast<std::int64_t>(amount);
  if(isServerBoundClientModeEnabled()) {
    const auto actorIdentity = actorKey(buyer);
    const auto npcIdentity = actorKey(vendor);
    const auto targetNpcIdentity =
        npcEntityKey(world.name(), vendor.persistentId(), vendor.instanceSymbol());
    const auto templateKey = itemTemplateKey(itemSymbol);
    const auto actorPos = buyer.position();
    ClientInventoryRequest request;
    request.clientTick = world.tickCount();
    request.action = ClientInventoryAction::TradeBuyFromNpc;
    request.itemSymbol = itemSymbol;
    request.vendorItemPersistentId = vendorItemPersistentId;
    request.amount = amount;
    request.actorPosition = {.x = actorPos.x, .y = actorPos.y, .z = actorPos.z};
    request.targetKey = target;
    request.source = sourceLocation != nullptr ? std::string_view(sourceLocation)
                                               : std::string_view("unknown");
    request.actorKey = actorIdentity;
    request.npcKey = npcIdentity;
    request.targetNpcEntityKey = targetNpcIdentity;
    request.itemTemplateKey = templateKey;
    request.world = world.name();
    (void)submitClientInventory(request);
  }
  if(!isClientMmoDiagnosticsEnabled())
    return;

  std::string payload;
  payload.reserve(768);
  payload.append("{\"source\":"); appendEscaped(payload, sourceLocation);
  appendNpcIdentity(payload, "buyer", buyer);
  appendNpcIdentity(payload, "npc", vendor);
  payload.append(",\"actor_key\":"); appendEscaped(payload, actorKey(buyer));
  payload.append(",\"item_template_key\":"); appendEscaped(payload, itemTemplateKey(itemSymbol));
  payload.append(",\"item_symbol\":"); appendUInt(payload, itemSymbol);
  payload.append(",\"vendor_item_persistent_id\":"); appendUInt(payload, vendorItemPersistentId);
  payload.append(",\"amount\":"); appendUInt(payload, amount);
  payload.append(",\"unit_price\":"); appendInt(payload, unitPrice);
  payload.append(",\"price_total\":"); appendInt(payload, totalPrice);
  payload.append(",\"currency_key\":\"g2notr:gold\"");
  payload.append(",\"wallet_before\":"); appendUInt(payload, goldBefore);
  payload.append(",\"wallet_after\":"); appendUInt(payload, goldAfter);
  payload.append(",\"reason\":\"trade_buy_from_npc\"");
  appendWorld(payload, world);
  appendVec3(payload, "buyer_position", buyer.position());
  payload.push_back('}');

  if(isClientMmoDiagnosticsEnabled())
    submit(SemanticActionKind::TradeBuyFromNpc, std::move(target), std::move(payload), world.tickCount());
}

void onTradeSellToNpc(Npc& seller,
                      Npc& buyer,
                      std::size_t itemSymbol,
                      std::uint32_t sellerItemPersistentId,
                      std::size_t amount,
                      std::int32_t unitPrice,
                      std::size_t goldBefore,
                      std::size_t goldAfter,
                      const char* sourceLocation) noexcept {
  if((!isClientMmoDiagnosticsEnabled() && !isServerBoundClientModeEnabled()) || amount == 0 || !shouldCapturePlayerAction(seller))
    return;
  auto& world = seller.world();
  auto target = itemTemplateKey(itemSymbol);
  target.append(":sell-to:");
  appendUInt(target, buyer.persistentId());
  target.push_back(':');
  appendUInt(target, sellerItemPersistentId);

  const auto totalPrice = static_cast<std::int64_t>(unitPrice) * static_cast<std::int64_t>(amount);
  if(isServerBoundClientModeEnabled()) {
    const auto actorIdentity = actorKey(seller);
    const auto npcIdentity = actorKey(buyer);
    const auto targetNpcIdentity =
        npcEntityKey(world.name(), buyer.persistentId(), buyer.instanceSymbol());
    const auto templateKey = itemTemplateKey(itemSymbol);
    const auto actorPos = seller.position();
    ClientInventoryRequest request;
    request.clientTick = world.tickCount();
    request.action = ClientInventoryAction::TradeSellToNpc;
    request.itemSymbol = itemSymbol;
    request.sellerItemPersistentId = sellerItemPersistentId;
    request.amount = amount;
    request.actorPosition = {.x = actorPos.x, .y = actorPos.y, .z = actorPos.z};
    request.targetKey = target;
    request.source = sourceLocation != nullptr ? std::string_view(sourceLocation)
                                               : std::string_view("unknown");
    request.actorKey = actorIdentity;
    request.npcKey = npcIdentity;
    request.targetNpcEntityKey = targetNpcIdentity;
    request.itemTemplateKey = templateKey;
    request.world = world.name();
    (void)submitClientInventory(request);
  }
  if(!isClientMmoDiagnosticsEnabled())
    return;

  std::string payload;
  payload.reserve(768);
  payload.append("{\"source\":"); appendEscaped(payload, sourceLocation);
  appendNpcIdentity(payload, "seller", seller);
  appendNpcIdentity(payload, "npc", buyer);
  payload.append(",\"actor_key\":"); appendEscaped(payload, actorKey(seller));
  payload.append(",\"item_template_key\":"); appendEscaped(payload, itemTemplateKey(itemSymbol));
  payload.append(",\"item_symbol\":"); appendUInt(payload, itemSymbol);
  payload.append(",\"seller_item_persistent_id\":"); appendUInt(payload, sellerItemPersistentId);
  payload.append(",\"item_persistent_id\":"); appendUInt(payload, sellerItemPersistentId);
  payload.append(",\"amount\":"); appendUInt(payload, amount);
  payload.append(",\"unit_price\":"); appendInt(payload, unitPrice);
  payload.append(",\"price_total\":"); appendInt(payload, totalPrice);
  payload.append(",\"currency_key\":\"g2notr:gold\"");
  payload.append(",\"wallet_before\":"); appendUInt(payload, goldBefore);
  payload.append(",\"wallet_after\":"); appendUInt(payload, goldAfter);
  payload.append(",\"reason\":\"trade_sell_to_npc\"");
  appendWorld(payload, world);
  appendVec3(payload, "seller_position", seller.position());
  payload.push_back('}');

  if(isClientMmoDiagnosticsEnabled())
    submit(SemanticActionKind::TradeSellToNpc, std::move(target), std::move(payload), world.tickCount());
}

void onCharacterItemConsumed(Npc& actor,
                             std::size_t itemSymbol,
                             std::uint32_t itemPersistentId,
                             std::size_t amount,
                             std::string_view reason,
                             const char* sourceLocation) noexcept {
  if((!isClientMmoDiagnosticsEnabled() && !isServerBoundClientModeEnabled()) || amount == 0 || !shouldCapturePlayerAction(actor))
    return;
  auto& world = actor.world();
  auto target = itemTemplateKey(itemSymbol);
  target.append(":consume:");
  appendUInt(target, itemPersistentId);

  if(isServerBoundClientModeEnabled()) {
    const auto actorIdentity = actorKey(actor);
    const auto templateKey = itemTemplateKey(itemSymbol);
    const auto actorPos = actor.position();
    ClientInventoryRequest request;
    request.clientTick = world.tickCount();
    request.action = ClientInventoryAction::ConsumeItem;
    request.itemSymbol = itemSymbol;
    request.itemPersistentId = itemPersistentId;
    request.amount = amount;
    request.actorPosition = {.x = actorPos.x, .y = actorPos.y, .z = actorPos.z};
    request.targetKey = target;
    request.source = sourceLocation != nullptr ? std::string_view(sourceLocation)
                                               : std::string_view("unknown");
    request.actorKey = actorIdentity;
    request.itemTemplateKey = templateKey;
    request.world = world.name();
    request.reason = reason;
    (void)submitClientInventory(request);
  }
  if(!isClientMmoDiagnosticsEnabled())
    return;

  std::string payload;
  payload.reserve(512);
  payload.append("{\"source\":"); appendEscaped(payload, sourceLocation);
  payload.append(",\"actor_key\":"); appendEscaped(payload, actorKey(actor));
  payload.append(",\"item_template_key\":"); appendEscaped(payload, itemTemplateKey(itemSymbol));
  payload.append(",\"item_symbol\":"); appendUInt(payload, itemSymbol);
  payload.append(",\"item_persistent_id\":"); appendUInt(payload, itemPersistentId);
  payload.append(",\"amount\":"); appendUInt(payload, amount);
  payload.append(",\"reason\":"); appendEscaped(payload, reason);
  appendWorld(payload, world);
  appendVec3(payload, "actor_position", actor.position());
  payload.push_back('}');

  if(isClientMmoDiagnosticsEnabled())
    submit(SemanticActionKind::ConsumeItem, std::move(target), std::move(payload), world.tickCount());
}

} // namespace Mmo::Hooks::Detail
