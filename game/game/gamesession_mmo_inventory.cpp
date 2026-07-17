#include "gamesession.h"
#include "savegameheader.h"
#if OPENGOTHIC_MMO_SQLITE_TOOLING
#include "../../tools/mmo/mmoruntimesqlite.h"
#endif
#include "mmosemantichooks.h"
#include "mmoclientbridge.h"
#include "mmoclientpresentationcatalog.h"
#include "mmoserverpresentationbatchconsumer.h"
#include "mmorestoresnapshot.h"

#include <Tempest/Log>
#include <Tempest/MemReader>
#include <Tempest/MemWriter>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <set>
#include <chrono>
#include <charconv>
#include <thread>
#include <limits>
#include <iterator>
#include <functional>
#include <optional>
#include <utility>
#include <type_traits>

#include "utils/string_frm.h"
#include "worldstatestorage.h"
#include "world/objects/npc.h"
#include "world/objects/item.h"
#include "world/objects/interactive.h"
#include "world/world.h"
#include "world/waypoint.h"
#include "sound/soundfx.h"
#include "serialize.h"
#include "camera.h"
#include "gothic.h"
#include "commandline.h"

using namespace Tempest;

namespace {

struct MmoPresentationWeaponShape final {
  bool meleeEquipped = false;
  bool meleeTwoHanded = false;
  bool rangedEquipped = false;
  bool rangedCrossbow = false;
};

[[nodiscard]] constexpr bool equipmentOccupied(
    const Mmo::ClientPresentation::ServerPresentationEquipmentSlotRecord*
        state) noexcept {
  return state != nullptr &&
         (state->flags &
          Mmo::ClientPresentation::ServerPresentationEquipmentOccupied) != 0U;
}

[[nodiscard]] constexpr std::optional<
    Mmo::ClientPresentation::ServerPresentationEquipmentSlot>
presentationEquipmentSlot(
    const Mmo::ClientEquipmentSlot slot) noexcept {
  using Source = Mmo::ClientEquipmentSlot;
  using Destination =
      Mmo::ClientPresentation::ServerPresentationEquipmentSlot;
  switch(slot) {
    case Source::MeleeWeapon: return Destination::MeleeWeapon;
    case Source::RangedWeapon: return Destination::RangedWeapon;
    case Source::Armor: return Destination::Armor;
    case Source::Amulet: return Destination::Amulet;
    case Source::RingLeft: return Destination::RingLeft;
    case Source::RingRight: return Destination::RingRight;
    case Source::Belt: return Destination::Belt;
    case Source::Spell: return Destination::Spell;
  }
  return std::nullopt;
}

[[nodiscard]] constexpr WeaponState presentationWeaponState(
    const Mmo::ClientPresentation::ServerPresentationWeaponMode mode,
    const MmoPresentationWeaponShape shape) noexcept {
  using Mode = Mmo::ClientPresentation::ServerPresentationWeaponMode;
  switch(mode) {
    case Mode::None: return WeaponState::NoWeapon;
    case Mode::Melee:
      if(!shape.meleeEquipped)
        return WeaponState::Fist;
      return shape.meleeTwoHanded ? WeaponState::W2H : WeaponState::W1H;
    case Mode::Ranged:
      if(!shape.rangedEquipped)
        return WeaponState::NoWeapon;
      return shape.rangedCrossbow ? WeaponState::CBow : WeaponState::Bow;
    case Mode::Magic: return WeaponState::Mage;
    case Mode::Fist: return WeaponState::Fist;
  }
  return WeaponState::NoWeapon;
}


} // namespace

bool GameSession::trackMmoServerInventoryCommand(
    Mmo::ClientPresentation::ServerInventoryPendingCommand command) {
  return mmoServerInventoryPresentation_.markPending(std::move(command));
}

std::optional<std::string_view> GameSession::mmoItemInstanceName(
    const std::uint64_t archetypeId,
    const std::uint64_t presentationId) const noexcept {
  return mmoClientPresentationCatalog != nullptr
             ? mmoClientPresentationCatalog->itemInstanceName(
                   archetypeId, presentationId)
             : std::nullopt;
}

std::optional<std::string_view> GameSession::mmoItemDisplayName(
    const std::uint64_t archetypeId,
    const std::uint64_t presentationId) const noexcept {
  return mmoClientPresentationCatalog != nullptr
             ? mmoClientPresentationCatalog->itemDisplayName(
                   archetypeId, presentationId)
             : std::nullopt;
}


void GameSession::applyMmoServerInventorySnapshot(
    const Mmo::ClientPresentation::ServerInventorySnapshotEvent& event) noexcept {
  using namespace Mmo::ClientPresentation;
  const auto* owner = mmoTypedServerPresentation.findEntity(event.owner);
  if(owner == nullptr ||
     owner->kind != ServerPresentationEntityKind::LocalPlayer) {
    return;
  }
  const auto status =
      mmoServerInventoryPresentation_.installInventory(event.snapshot);
  if(status != ServerInventoryApplyStatus::Applied &&
     status != ServerInventoryApplyStatus::Duplicate) {
    Log::e("MMO live inventory snapshot rejected: status=",
           static_cast<unsigned>(status),
           " revision=", event.snapshot.revision,
           " stacks=", event.snapshot.stacks.size());
    return;
  }
  refreshMmoServerEquipmentPresentation(event.header, event.owner);
}

void GameSession::applyMmoServerInventoryDelta(
    const Mmo::ClientPresentation::ServerInventoryDeltaEvent& event) noexcept {
  using namespace Mmo::ClientPresentation;
  const auto* owner = mmoTypedServerPresentation.findEntity(event.owner);
  if(owner == nullptr ||
     owner->kind != ServerPresentationEntityKind::LocalPlayer) {
    return;
  }

  ServerInventoryDelta delta{
      .revision = event.mutation.inventoryRevision,
  };
  switch(event.mutation.kind) {
    case ServerInventoryLiveMutationKind::StackAdded:
      delta.upserted.push_back(event.mutation.item);
      break;
    case ServerInventoryLiveMutationKind::StackRemoved:
      delta.removed.push_back(event.mutation.stack);
      break;
    case ServerInventoryLiveMutationKind::StackQuantityChanged: {
      const auto* current =
          mmoServerInventoryPresentation_.inventory().find(event.mutation.stack);
      if(current == nullptr) {
        Log::e("MMO live inventory quantity delta references missing stack: id=",
               event.mutation.stack.instanceId,
               " generation=", event.mutation.stack.generation);
        return;
      }
      auto updated = *current;
      updated.quantity = event.mutation.quantity;
      updated.itemRevision = event.mutation.itemRevision;
      delta.upserted.push_back(std::move(updated));
      break;
    }
  }

  const auto status =
      mmoServerInventoryPresentation_.applyAuthoritative(std::move(delta));
  if(status != ServerInventoryApplyStatus::Applied &&
     status != ServerInventoryApplyStatus::Duplicate) {
    Log::e("MMO live inventory delta rejected: status=",
           static_cast<unsigned>(status),
           " revision=", event.mutation.inventoryRevision);
  }
}

void GameSession::applyMmoServerEquipmentSnapshot(
    const Mmo::ClientPresentation::ServerEquipmentSnapshotEvent& event) noexcept {
  using namespace Mmo::ClientPresentation;
  const auto* owner = mmoTypedServerPresentation.findEntity(event.owner);
  if(owner == nullptr ||
     owner->kind != ServerPresentationEntityKind::LocalPlayer) {
    return;
  }
  const auto status =
      mmoServerInventoryPresentation_.installEquipment(event.snapshot);
  if(status != ServerInventoryApplyStatus::Applied &&
     status != ServerInventoryApplyStatus::Duplicate) {
    Log::e("MMO live equipment snapshot rejected: status=",
           static_cast<unsigned>(status),
           " revision=", event.snapshot.revision);
    return;
  }
  refreshMmoServerEquipmentPresentation(event.header, event.owner);
}

void GameSession::refreshMmoServerEquipmentPresentation(
    const Mmo::ClientPresentation::ServerPresentationEventHeader& header,
    const Mmo::ClientPresentation::ServerPresentationEntityHandle owner) noexcept {
  using namespace Mmo::ClientPresentation;
  const auto& equipment = mmoServerInventoryPresentation_.equipment();
  if(!equipment.ready())
    return;

  constexpr std::array slots{
      Mmo::ClientEquipmentSlot::MeleeWeapon,
      Mmo::ClientEquipmentSlot::RangedWeapon,
      Mmo::ClientEquipmentSlot::Armor,
      Mmo::ClientEquipmentSlot::Amulet,
      Mmo::ClientEquipmentSlot::RingLeft,
      Mmo::ClientEquipmentSlot::RingRight,
      Mmo::ClientEquipmentSlot::Belt,
      Mmo::ClientEquipmentSlot::Spell,
  };
  for(const auto slot : slots) {
    ServerEquipmentBindingChangedEvent changed{
        .header = header,
        .owner = owner,
        .change = {
            .revision = equipment.revision(),
            .slot = slot,
        },
    };
    if(const auto* binding = equipment.at(slot); binding != nullptr)
      changed.change.equipped = *binding;
    applyMmoServerEquipmentBinding(changed);
  }
}

void GameSession::applyMmoServerEquipmentBinding(
    const Mmo::ClientPresentation::ServerEquipmentBindingChangedEvent& event) noexcept {
  using namespace Mmo::ClientPresentation;
  const auto* owner = mmoTypedServerPresentation.findEntity(event.owner);
  if(owner == nullptr ||
     owner->kind != ServerPresentationEntityKind::LocalPlayer) {
    return;
  }

  const auto status =
      mmoServerInventoryPresentation_.applyAuthoritative(event.change);
  if(status != ServerInventoryApplyStatus::Applied &&
     status != ServerInventoryApplyStatus::Duplicate) {
    Log::e("MMO live equipment delta rejected: status=",
           static_cast<unsigned>(status),
           " revision=", event.change.revision);
    return;
  }

  const auto slot = presentationEquipmentSlot(event.change.slot);
  if(!slot.has_value())
    return;
  ServerPresentationEquipmentSlotRecord presentation{
      .entity = event.owner,
      .slot = *slot,
      .equipmentRevision = event.change.revision,
  };
  if(event.change.equipped.has_value()) {
    const auto& binding = *event.change.equipped;
    const auto* stack =
        mmoServerInventoryPresentation_.inventory().find(binding.item);
    if(stack == nullptr)
      return;
    presentation.item = {
        .id = binding.item.instanceId,
        .generation = binding.item.generation,
    };
    presentation.presentation = {
        .archetypeId = stack->archetypeId,
        .presentationId = stack->presentationId,
        .revision = stack->presentationRevision,
    };
    presentation.flags = ServerPresentationEquipmentOccupied;
  }
  if(presentation.valid())
    applyMmoServerEquipmentSlot(presentation);
}

void GameSession::applyMmoServerEquipmentSlot(
    const Mmo::ClientPresentation::ServerPresentationEquipmentSlotRecord& state) noexcept {
  if(wrld == nullptr)
    return;
  using namespace Mmo::ClientPresentation;
  if(state.slot != ServerPresentationEquipmentSlot::MeleeWeapon &&
     state.slot != ServerPresentationEquipmentSlot::RangedWeapon) {
    return;
  }

  auto* npc = resolveMmoServerEntity(state.entity);
  if(npc == nullptr)
    return;
  npc->setMmoServerReplica(true);

  const bool occupied = equipmentOccupied(&state);
  if(!occupied) {
    if(state.slot == ServerPresentationEquipmentSlot::MeleeWeapon)
      npc->setSword(MeshObjects::Mesh{});
    else
      npc->setRangedWeapon(MeshObjects::Mesh{});
  } else {
    const auto visual = mmoClientPresentationCatalog != nullptr
                            ? mmoClientPresentationCatalog->equippedWeaponVisual(
                                  state.presentation.archetypeId,
                                  state.presentation.presentationId)
                            : std::nullopt;
    if(!visual.has_value()) {
      if(state.slot == ServerPresentationEquipmentSlot::MeleeWeapon)
        npc->setSword(MeshObjects::Mesh{});
      else
        npc->setRangedWeapon(MeshObjects::Mesh{});
      Log::e("MMO typed equipment visual unresolved: entity=", state.entity.id,
             " slot=", static_cast<unsigned>(state.slot),
             " presentation=", state.presentation.presentationId,
             " archetype=", state.presentation.archetypeId);
    } else {
      auto mesh = wrld->addView(*visual);
      if(state.slot == ServerPresentationEquipmentSlot::MeleeWeapon)
        npc->setSword(std::move(mesh));
      else
        npc->setRangedWeapon(std::move(mesh));
    }
  }

  if(const auto* mode = mmoTypedServerPresentation.findWeaponMode(state.entity)) {
    applyMmoServerWeaponMode(*mode, false);
  } else {
    applyMmoServerWeaponMode(
        ServerPresentationWeaponModeRecord{
            .entity = state.entity,
            .mode = ServerPresentationWeaponMode::None,
            .weaponRevision = 1U,
        },
        false);
  }
}

void GameSession::applyMmoServerWeaponMode(
    const Mmo::ClientPresentation::ServerPresentationWeaponModeRecord& state,
    const bool animate) noexcept {
  auto* npc = resolveMmoServerEntity(state.entity);
  if(npc == nullptr)
    return;
  using namespace Mmo::ClientPresentation;
  const auto* melee = mmoTypedServerPresentation.findEquipment(
      state.entity, ServerPresentationEquipmentSlot::MeleeWeapon);
  const auto* ranged = mmoTypedServerPresentation.findEquipment(
      state.entity, ServerPresentationEquipmentSlot::RangedWeapon);
  const MmoPresentationWeaponShape shape{
      .meleeEquipped = equipmentOccupied(melee),
      .meleeTwoHanded =
          melee != nullptr &&
          (melee->flags & ServerPresentationEquipmentTwoHanded) != 0U,
      .rangedEquipped = equipmentOccupied(ranged),
      .rangedCrossbow =
          ranged != nullptr &&
          (ranged->flags & ServerPresentationEquipmentCrossbow) != 0U,
  };
  npc->setMmoServerReplica(true);
  npc->applyMmoServerPresentationWeaponMode(
      presentationWeaponState(state.mode, shape), animate,
      shape.meleeTwoHanded, shape.rangedCrossbow);
}
