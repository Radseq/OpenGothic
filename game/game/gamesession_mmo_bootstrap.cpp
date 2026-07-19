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

void appendMmoPresentationNumber(
    std::string& value,
    const std::uint64_t number) {
  char buffer[24]{};
  const auto [end, error] = std::to_chars(std::begin(buffer), std::end(buffer), number);
  if(error == std::errc{})
    value.append(buffer, end);
}

[[nodiscard]] std::string makeMmoPresentationRouteKey(
    const Mmo::ClientPresentation::ServerPresentationRouteIdentity& route) {
  std::string value;
  value.reserve(80U);
  value.append("v2:");
  appendMmoPresentationNumber(value, route.connectionId);
  value.push_back(':');
  appendMmoPresentationNumber(value, route.routeEpoch);
  value.push_back(':');
  appendMmoPresentationNumber(value, route.world.id);
  value.push_back(':');
  appendMmoPresentationNumber(value, route.world.generation);
  return value;
}

} // namespace

void GameSession::loadMmoClientPresentationCatalog() noexcept {
  const auto& cmd = CommandLine::inst();
  if(cmd.mmoClientPresentationCatalog().empty())
    return;

  auto runtime = std::make_unique<
      Mmo::ClientPresentation::ClientPresentationCatalogRuntime>();
  const auto result = runtime->load(
      std::filesystem::path{cmd.mmoClientPresentationCatalog()},
      Mmo::Presentation::ContentManifestId{
          cmd.mmoClientPresentationManifestId()});
  if(!result.applied()) {
    Log::e(
        "MMO client presentation catalog rejected: status=",
        Mmo::ClientPresentation::toString(result.status),
        " codec=", Mmo::Presentation::toString(result.codecStatus),
        " path=", cmd.mmoClientPresentationCatalog());
    return;
  }
  Log::i(
      "MMO client presentation catalog installed: manifest=",
      runtime->manifest().value,
      " fingerprint=", runtime->contentFingerprint(),
      " path=", cmd.mmoClientPresentationCatalog());
  mmoClientPresentationCatalog = std::move(runtime);
}


void GameSession::beginMmoLocalWorldObjectCatalog() noexcept {
  mmoServerWorldObjects.resetLocalCatalog();
}

void GameSession::registerMmoLocalWorldObject(
    const std::uint64_t worldObjectId,
    const std::uint32_t vobObjectId,
    const Mmo::ClientPresentation::ServerPresentationWorldObjectKind kind) {
  using namespace Mmo::ClientPresentation;
  const auto status = mmoServerWorldObjects.registerLocal(
      ServerWorldObjectId{worldObjectId},
      LocalVobToken{.vobObjectId = vobObjectId, .kind = kind});
  if(status != ServerWorldObjectRegisterStatus::Registered &&
     status != ServerWorldObjectRegisterStatus::Duplicate) {
    Log::e("MMO local world-object catalog rejected VOB: world_object=",
           worldObjectId,
           " vob=", vobObjectId,
           " kind=", static_cast<unsigned>(kind),
           " status=", static_cast<unsigned>(status));
  }
}

void GameSession::setMmoServerPresentationRoute(
    const Mmo::ClientPresentation::ServerPresentationRouteIdentity& route) {
  mmoPresentationRouteKey = makeMmoPresentationRouteKey(route);
  mmoServerWorldObjects.resetRoute(route.world);
  mmoServerProjectilePresentation.resetRoute(route.world);
}

void GameSession::installMmoServerPresentationBootstrap(
    const Mmo::ClientPresentation::ServerPresentationBootstrap& bootstrap,
    const bool projectionAlreadyReset) {
  if(!projectionAlreadyReset)
    resetMmoServerPresentationProjection();
  setMmoServerPresentationRoute(bootstrap.route);
  const auto inventoryStatus = mmoServerInventoryPresentation_.install(
      bootstrap.inventory, bootstrap.equipment);
  if(inventoryStatus !=
         Mmo::ClientPresentation::ServerInventoryApplyStatus::Applied &&
     inventoryStatus !=
         Mmo::ClientPresentation::ServerInventoryApplyStatus::Duplicate) {
    Log::e("MMO inventory bootstrap rejected: status=",
           static_cast<unsigned>(inventoryStatus));
    return;
  }

  using WorldObjectKind =
      Mmo::ClientPresentation::ServerPresentationWorldObjectKind;
  std::size_t moverCount = 0U;
  std::size_t moverBound = 0U;
  std::size_t moverUnresolved = 0U;
  std::size_t worldObjectBound = 0U;
  for(const auto& object : bootstrap.worldObjects) {
    const auto result = mmoServerWorldObjects.bindRuntime(
        object.entity,
        Mmo::ClientPresentation::ServerWorldObjectId{object.worldObjectId},
        object.kind);
    if(result.bound())
      ++worldObjectBound;
    if(object.kind == WorldObjectKind::Mover) {
      ++moverCount;
      if(result.bound())
        ++moverBound;
      else
        ++moverUnresolved;
    }
    if(!result.bound() &&
       mmoServerWorldObjects.shouldLogUnresolved(
           object.entity, object.stateRevision)) {
      Log::e("MMO typed world object unresolved during bootstrap: entity=",
             object.entity.id,
             " generation=", object.entity.generation,
             " world_object=", object.worldObjectId,
             " kind=", static_cast<unsigned>(object.kind),
             " revision=", object.stateRevision,
             " bind_status=", static_cast<unsigned>(result.status));
    }
  }

  const auto local = std::find_if(
      bootstrap.entities.begin(), bootstrap.entities.end(),
      [](const auto& entity) {
        return entity.kind ==
               Mmo::ClientPresentation::ServerPresentationEntityKind::LocalPlayer;
      });
  if(local != bootstrap.entities.end())
    materializeMmoServerEntity(*local, true);

  for(const auto& entity : bootstrap.entities) {
    if(entity.kind ==
       Mmo::ClientPresentation::ServerPresentationEntityKind::LocalPlayer) {
      continue;
    }
    materializeMmoServerEntity(entity, true);
  }
  for(const auto& state : bootstrap.npcStates)
    applyMmoServerNpcState(state);
  for(const auto& state : bootstrap.combatEquipment)
    applyMmoServerEquipmentSlot(state);
  for(const auto& state : bootstrap.weaponModes)
    applyMmoServerWeaponMode(state, false);
  for(const auto& state : bootstrap.lifeStates)
    applyMmoServerLifeState(state);
  for(const auto& action : bootstrap.combatActions)
    applyMmoServerCombatAction(action);
  for(const auto& state : bootstrap.interactives)
    applyMmoServerInteractiveState(state);
  for(const auto& state : bootstrap.movers)
    applyMmoServerMoverState(state);

  Mmo::recordClientMmoProcessGatePresentation(
      Mmo::ClientMmoProcessGatePresentationEvent::BootstrapApplied);
  Log::i("MMO typed presentation bootstrap installed",
         " route_epoch=", bootstrap.route.routeEpoch,
         " world=", bootstrap.route.world.id,
         " world_generation=", bootstrap.route.world.generation,
         " baseline_tick=", bootstrap.baseline.serverTick,
         " baseline_revision=", bootstrap.baseline.aggregateRevision,
         " inventory_revision=", bootstrap.inventory.revision,
         " inventory_stacks=", bootstrap.inventory.stacks.size(),
         " equipment_revision=", bootstrap.equipment.revision,
         " equipped_slots=", bootstrap.equipment.equipped.size(),
         " entities=", bootstrap.entities.size(),
         " world_objects=", bootstrap.worldObjects.size(),
         " world_objects_bound=", worldObjectBound,
         " local_world_objects=", mmoServerWorldObjects.localCount(),
         " npc_states=", bootstrap.npcStates.size(),
         " combat_equipment=", bootstrap.combatEquipment.size(),
         " weapon_modes=", bootstrap.weaponModes.size(),
         " combat_actions=", bootstrap.combatActions.size(),
         " life_states=", bootstrap.lifeStates.size(),
         " interactives=", bootstrap.interactives.size(),
         " movers=", bootstrap.movers.size(),
         " mover_descriptors=", moverCount,
         " movers_bound=", moverBound,
         " movers_unresolved=", moverUnresolved);
}
