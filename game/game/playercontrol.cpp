#include "playercontrol.h"

#include "../../../shared/game/gothic/gothic_pickup_limits.h"

#include <Tempest/Log>

#include <cmath>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>

#include "world/objects/npc.h"
#include "world/objects/item.h"
#include "world/objects/interactive.h"
#include "world/world.h"
#include "ui/dialogmenu.h"
#include "ui/inventorymenu.h"
#include "commandline.h"
#include "gamesession.h"
#include "gothic.h"
#include "mmoclientbridge.h"
#include "utils/nativetelemetry.h"

namespace {

[[nodiscard]] bool submitMmoInteraction(
    World& world,
    const Npc& player,
    const GameSession::MmoServerEntityTarget& target,
    const Mmo::ClientInteractionVerb verb,
    const std::string_view source,
    const std::string_view reason) {
  const auto position = player.position();
  std::string actorKey = "character:";
  actorKey.append(CommandLine::inst().mmoCharacterKey());
  const Mmo::ClientInteractionRequest request{
      .clientTick = world.tickCount(),
      .verb = verb,
      .targetHandle = target.handle,
      .expectedTargetRevision = target.revision,
      .actorPosition = {position.x, position.y, position.z},
      .targetKey = "server-entity",
      .source = source,
      .actorKey = actorKey,
      .characterKey = CommandLine::inst().mmoCharacterKey(),
      .world = world.name(),
      .reason = reason,
  };
  Tempest::Log::i(
      "MMO interaction submit: verb=", static_cast<unsigned>(verb),
      " target=", target.handle.worldId, ":", target.handle.worldGeneration,
      ":", target.handle.id, ":", target.handle.generation,
      " revision=", target.revision,
      " actor_position=", position.x, ",", position.y, ",", position.z,
      " source=", source);
  const auto result = Mmo::submitClientInteraction(request);
  Tempest::Log::i(
      "MMO interaction submission result: status=",
      static_cast<unsigned>(result.status),
      " submitted=", result.submitted() ? 1 : 0,
      " route_epoch=", result.command.routeEpoch,
      " sequence=", result.command.sequence,
      " dropped_count=", result.droppedCount,
      " target=", target.handle.id, ":", target.handle.generation);
  if(!result.accepted()) {
    Tempest::Log::e("MMO interaction submission failed source=", source,
                    " status=", static_cast<unsigned>(result.status),
                    " target=", target.handle.id, ":",
                    target.handle.generation);
  }
  return true;
}

[[nodiscard]] bool isBookInteractive(const Interactive& interactive) {
  const auto containsBookWord = [](const std::string_view value) {
    for(std::size_t index = 0U; index < value.size(); ++index) {
      if(index + 3U > value.size())
        break;
      auto lower = [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
      };
      const auto a = lower(static_cast<unsigned char>(value[index]));
      const auto b = lower(static_cast<unsigned char>(value[index + 1U]));
      const auto c = lower(static_cast<unsigned char>(value[index + 2U]));
      const auto d = index + 3U < value.size()
                         ? lower(static_cast<unsigned char>(value[index + 3U]))
                         : '\0';
      if((a == 'b' && b == 'o' && c == 'o' && d == 'k') ||
         (a == 's' && b == 'h' && c == 'e' && d == 'l') ||
         (a == 'r' && b == 'e' && c == 'g' && d == 'a') ||
         (a == 'b' && b == 'u' && c == 'c' && d == 'h') ||
         (a == 'r' && b == 'e' && c == 'a' && d == 'd')) {
        return true;
      }
    }
    return false;
  };
  return containsBookWord(interactive.tag()) ||
         containsBookWord(interactive.focusName()) ||
         containsBookWord(interactive.schemeName()) ||
         containsBookWord(interactive.ownerName());
}

[[nodiscard]] std::string_view combatActionName(
    const Mmo::ClientCombatRequest::Action action) noexcept {
  using Action = Mmo::ClientCombatRequest::Action;
  switch(action) {
    case Action::DrawWeapon: return "draw_weapon";
    case Action::HolsterWeapon: return "holster_weapon";
    case Action::PrimaryAttack: return "primary_attack";
    case Action::SecondaryAttack: return "secondary_attack";
    case Action::Parry: return "parry";
    case Action::Dodge: return "dodge";
    case Action::Cancel: return "cancel";
  }
  return "cancel";
}

void submitMmoCombatAction(
    World& world,
    const Npc& player,
    const Npc* targetNpc,
    const Mmo::ClientCombatRequest::Action action,
    const std::string_view source) {
  if(!CommandLine::inst().mmoClientUsesServer())
    return;

  const auto session = Mmo::clientMmoSessionSnapshot();
  if(!session.inWorld())
    return;

  std::optional<GameSession::MmoServerEntityTarget> target;
  if(targetNpc != nullptr) {
    if(auto* game = Gothic::inst().gameSession())
      target = game->mmoServerEntityTarget(*targetNpc);
  }

  const auto position = player.position();
  std::string actorKey = "character:";
  actorKey.append(CommandLine::inst().mmoCharacterKey());
  const auto actionName = combatActionName(action);

  Mmo::ClientCombatRequest request;
  request.clientTick = world.tickCount();
  request.protocolAction = action;
  request.lastAcknowledgedServerTick = session.lastServerTick;
  request.actorPosition = {position.x, position.y, position.z};
  request.targetKey = target.has_value() ? "server-entity" : "self";
  request.source = source;
  request.reason = "player_input";
  request.actorKey = actorKey;
  request.npcEntityKey = actorKey;
  request.targetNpcEntityKey = target.has_value() ? "server-entity" : "";
  request.world = world.name();
  request.combatAction = actionName;
  request.intentState = "pressed";
  if(target.has_value()) {
    constexpr std::uint16_t CombatFlagLockOn = 1U << 1U;
    request.targetHandle = target->handle;
    request.expectedTargetRevision = target->revision;
    request.flags |= CombatFlagLockOn;
  }

  const auto result = Mmo::submitClientCombat(request);
  if(!result.accepted()) {
    Tempest::Log::e("MMO combat submission failed source=", source,
                    " action=", actionName,
                    " status=", static_cast<unsigned>(result.status));
  }
}

} // namespace

PlayerControl::PlayerControl(DialogMenu& dlg, InventoryMenu &inv)
  :dlg(dlg),inv(inv) {
  Gothic::inst().onSettingsChanged.bind(this,&PlayerControl::setupSettings);
  setupSettings();
  }

PlayerControl::~PlayerControl() {
  Gothic::inst().onSettingsChanged.ubind(this,&PlayerControl::setupSettings);
  }

void PlayerControl::setupSettings() {
  if(Gothic::inst().version().game==2) {
    g2Ctrl = Gothic::inst().settingsGetI("GAME","USEGOTHIC1CONTROLS")==0;
    } else {
    g2Ctrl = false;
    }
  }

void PlayerControl::setTarget(Npc *other) {
  auto w  = Gothic::inst().world();
  auto pl = w ? w->player() : nullptr;
  if(pl==nullptr || pl->isFinishingMove())
    return;
  const auto ws    = pl->weaponState();
  const bool melle = (ws==WeaponState::Fist || ws==WeaponState::W1H || ws==WeaponState::W2H);
  if(other==nullptr) {
    if(!(melle && ctrl[Action::ActionGeneric])) {
      // dont lose focus in melee combat
      pl->setTarget(nullptr);
      }
    } else {
    pl->setTarget(other);
    }
  }

void PlayerControl::onKeyPressed(KeyCodec::Action a, Tempest::KeyEvent::KeyType key, KeyCodec::Mapping mapping) {
  auto       w    = Gothic::inst().world();
  auto       c    = Gothic::inst().camera();
  auto       pl   = w  ? w->player() : nullptr;
  auto       ws   = pl ? pl->weaponState() : WeaponState::NoWeapon;
  uint8_t    slot = pl ? pl->inventory().currentSpellSlot() : Item::NSLOT;

  if(w!=nullptr && w->isCutsceneLock())
    return;

  handleMovementAction(KeyCodec::ActionMapping{a,mapping}, true);

  if(pl!=nullptr && pl->interactive()!=nullptr && c!=nullptr && !c->isFree()) {
    auto inter = pl->interactive();
    if(inter->needToLockpick(*pl)) {
      processPickLock(*pl,*inter,a);
      return;
      }
    if(inter->isLadder()) {
      ctrl[a] = true;
      return;
      }
    }

  if(pl!=nullptr) {
    if(a==Action::Weapon) {
      submitMmoCombatAction(
          *w, *pl, pl->target(),
          ws!=WeaponState::NoWeapon
              ? Mmo::ClientCombatRequest::Action::HolsterWeapon
              : Mmo::ClientCombatRequest::Action::DrawWeapon,
          "PlayerControl::onKeyPressed(Weapon)");
      if(ws!=WeaponState::NoWeapon) //Currently a weapon is active
        wctrl[WeaponClose] = true;
      else {
        if(wctrlLast>=WeaponAction::Weapon3 && pl->inventory().currentSpell(static_cast<uint8_t>(wctrlLast-3))==nullptr)
          wctrlLast=WeaponAction::WeaponBow;  //Spell no longer available -> fallback to Bow.
        if(wctrlLast==WeaponAction::WeaponBow && pl->currentRangedWeapon()==nullptr)
          wctrlLast=WeaponAction::WeaponMele; //Bow no longer available -> fallback to Mele.
        wctrl[wctrlLast] = true;
        }
      return;
      }

    if(a==Action::WeaponMele) {
      submitMmoCombatAction(
          *w, *pl, pl->target(),
          (ws==WeaponState::Fist || ws==WeaponState::W1H ||
           ws==WeaponState::W2H)
              ? Mmo::ClientCombatRequest::Action::HolsterWeapon
              : Mmo::ClientCombatRequest::Action::DrawWeapon,
          "PlayerControl::onKeyPressed(WeaponMele)");
      if(ws==WeaponState::Fist || ws==WeaponState::W1H || ws==WeaponState::W2H)
        wctrl[WeaponClose] = true; else
        wctrl[WeaponMele ] = true;
      return;
      }

    if(a==Action::WeaponBow) {
      submitMmoCombatAction(
          *w, *pl, pl->target(),
          (ws==WeaponState::Bow || ws==WeaponState::CBow)
              ? Mmo::ClientCombatRequest::Action::HolsterWeapon
              : Mmo::ClientCombatRequest::Action::DrawWeapon,
          "PlayerControl::onKeyPressed(WeaponBow)");
      if(ws==WeaponState::Bow || ws==WeaponState::CBow)
        wctrl[WeaponClose] = true; else
        wctrl[WeaponBow  ] = true;
      return;
      }

    if(a>=Action::WeaponMage3 && a<=Action::WeaponMage10) {
      int id = (a-Action::WeaponMage3+3);
      submitMmoCombatAction(
          *w, *pl, pl->target(),
          (ws==WeaponState::Mage && slot==id)
              ? Mmo::ClientCombatRequest::Action::HolsterWeapon
              : Mmo::ClientCombatRequest::Action::DrawWeapon,
          "PlayerControl::onKeyPressed(WeaponMage)");
      if(ws==WeaponState::Mage && slot==id)
        wctrl[WeaponClose] = true; else
        wctrl[id         ] = true;
      return;
      }

    if(key==Tempest::KeyEvent::K_Return)
      ctrl[Action::K_ENTER] = true;
    }

  // this odd behaviour is from original game, seem more like a bug
  // const bool actTunneling = (pl!=nullptr && pl->isAttackAnim());
  const bool actTunneling = false;

  int fk = -1;
  if((ctrl[KeyCodec::ActionGeneric] || actTunneling) && !g2Ctrl) {
    if(a==Action::Forward) {
      if(pl!=nullptr && pl->target()!=nullptr && pl->canFinish(*pl->target()) && !pl->isAttackAnim()) {
        fk = ActKill;
        } else {
        fk = ActForward;
        }
      }
    if(ws==WeaponState::Fist || ws==WeaponState::W1H || ws==WeaponState::W2H) {
      if(a==Action::Back)
        fk = ActBack;
      }
    if(ws!=WeaponState::NoWeapon && !g2Ctrl && !pl->hasState(BS_RUN)) {
      if(a==Action::Left  || a==Action::RotateL)
        fk = ActLeft;
      if(a==Action::Right || a==Action::RotateR)
        fk = ActRight;
      }
    }

  if(g2Ctrl) {
    if(ws!=WeaponState::NoWeapon) {
      if(a==Action::ActionGeneric) {
        if(pl!=nullptr && pl->target()!=nullptr && pl->canFinish(*pl->target()) && !pl->isAttackAnim()) {
          fk = ActKill;
          } else {
          if(this->wantsToMoveForward())
            fk = ActMove; else
            fk = ActForward;
          }
        }
      }
    if(ws==WeaponState::Fist || ws==WeaponState::W1H || ws==WeaponState::W2H) {
      if(a==Action::Parade)
        fk = ActBack;
      }
    if(ws!=WeaponState::NoWeapon && !pl->hasState(BS_RUN)) {
      if(a==Action::ActionLeft)
        fk = ActLeft;
      if(a==Action::ActionRight)
        fk = ActRight;
      }
    }

  if(fk>=0) {
    if(pl!=nullptr && ws!=WeaponState::NoWeapon) {
      using CombatAction = Mmo::ClientCombatRequest::Action;
      const auto action = fk == ActBack
                              ? CombatAction::Parry
                              : ((fk == ActLeft || fk == ActRight)
                                     ? CombatAction::SecondaryAttack
                                     : CombatAction::PrimaryAttack);
      submitMmoCombatAction(*w, *pl, pl->target(), action,
                            "PlayerControl::onKeyPressed(Combat)");
    }
    std::memset(actrl,0,sizeof(actrl));
    actrl[ActGeneric] = ctrl[KeyCodec::ActionGeneric];
    actrl[fk]         = true;

    ctrl[a] = true;
    return;
    }

  if(a==KeyCodec::ActionGeneric) {
    if(CommandLine::inst().mmoClientUsesServer() && pl != nullptr) {
      const auto playerPosition = pl->position();
      Tempest::Log::i(
          "MMO action pressed: interactive=",
          currentFocus.interactive != nullptr ? 1 : 0,
          " npc=", currentFocus.npc != nullptr ? 1 : 0,
          " item=", currentFocus.item != nullptr ? 1 : 0,
          " player_position=", playerPosition.x, ",", playerPosition.y, ",",
          playerPosition.z);
    }
    FocusAction fk = ActGeneric;
    if(this->wantsToMoveForward())
      fk = ActMove;
    std::memset(actrl,0,sizeof(actrl));
    actrl[fk] = true;
    ctrl[a]   = true;
    return;
    }

  if(a==Action::Walk) {
    toggleWalkMode();
    return;
    }

  if(a==Action::Sneak) {
    toggleSneakMode();
    return;
    }

  if(a==Action::FirstPerson) {
    if(auto c = Gothic::inst().camera())
      c->setFirstPerson(!c->isFirstPerson());
    return;
    }

  if(a==Action::K_O && Gothic::inst().isMarvinEnabled())
    marvinO();

  ctrl[a] = true;
  }

void PlayerControl::onKeyReleased(KeyCodec::Action a, KeyCodec::Mapping mapping) {
  ctrl[a] = false;

  handleMovementAction(KeyCodec::ActionMapping{a, mapping}, false);

  auto w  = Gothic::inst().world();
  auto pl = w ? w->player() : nullptr;

  if(a==KeyCodec::Map && pl!=nullptr) {
    w->script().playerHotKeyScreenMap(*pl);
    }
  if(a==KeyCodec::Heal && pl!=nullptr) {
    w->script().playerHotLameHeal(*pl);
    }
  if(a==KeyCodec::Potion && pl!=nullptr) {
    w->script().playerHotLamePotion(*pl);
    }

  auto ws = pl==nullptr ? WeaponState::NoWeapon : pl->weaponState();
  if(ws==WeaponState::Bow || ws==WeaponState::CBow || ws==WeaponState::Mage) {
    if(a==KeyCodec::ActionGeneric || (!g2Ctrl && ws==WeaponState::Mage && a==KeyCodec::Forward))
      std::memset(actrl,0,sizeof(actrl));
    } else {
    std::memset(actrl,0,sizeof(actrl));
    }
  }

auto PlayerControl::handleMovementAction(KeyCodec::ActionMapping actionMapping, bool pressed) -> void {
  auto[action, mapping] = actionMapping;
  auto mappingIndex = (mapping == KeyCodec::Mapping::Primary ? size_t(0) : size_t(1));
  if (action == Action::Forward)
    movement.forwardBackward.main[mappingIndex] = pressed;
  else if (action == Action::Back)
    movement.forwardBackward.reverse[mappingIndex] = pressed;
  else if (action == Action::Right)
    movement.strafeRightLeft.main[mappingIndex] = pressed;
  else if (action == Action::Left)
    movement.strafeRightLeft.reverse[mappingIndex] = pressed;
  else if (action == Action::RotateR)
    movement.turnRightLeft.main[mappingIndex] = pressed;
  else if (action == Action::RotateL)
    movement.turnRightLeft.reverse[mappingIndex] = pressed;
  }

bool PlayerControl::isPressed(KeyCodec::Action a) const {
  return ctrl[a];
  }

void PlayerControl::onRotateMouse(float dAngleX, float dAngleY) {
  rotMouse  += dAngleX;
  rotMouseY += dAngleY;
  }

void PlayerControl::drawVobRay(DbgPainter& p) const {
  auto w = Gothic::inst().world();
  if(w==nullptr || w->player()==nullptr)
    return;
  auto pl    = w->player();
  auto focus = findFocus(&currentFocus);
  if(focus.interactive!=nullptr) {
    focus.interactive->drawVobRay(p, *pl);
    }
  if(focus.item!=nullptr) {
    focus.item->drawVobRay(p, *pl);
    }
  if(focus.npc!=nullptr) {
    pl->drawVobRay(p, *focus.npc);
    }
  }

void PlayerControl::tickFocus() {
  auto w = Gothic::inst().world();
  currentFocus = findFocus(&currentFocus);
  setTarget(currentFocus.npc);

  if(!ctrl[Action::ActionGeneric])
    return;

  // Server world items are created after the native Gothic focus cache. If
  // the ray misses such an item by a few pixels, still allow a nearby click
  // to pick the nearest authoritative item.
  if(currentFocus.interactive == nullptr && currentFocus.npc == nullptr &&
     currentFocus.item == nullptr &&
     CommandLine::inst().mmoClientUsesServer() && w != nullptr &&
     w->player() != nullptr) {
    if(auto* session = Gothic::inst().gameSession(); session != nullptr)
      currentFocus.item = session->mmoNearestServerWorldItem(
          w->player()->position(), w->player()->rotation());
    if(currentFocus.item != nullptr)
      Tempest::Log::i(
          "MMO item focus fallback: item=", currentFocus.item->displayName(),
          " position=", currentFocus.item->position().x, ",",
          currentFocus.item->position().y, ",",
          currentFocus.item->position().z);
  }

  auto focus = currentFocus;
  if(focus.interactive!=nullptr && interact(*focus.interactive)) {
    clearInput();
    }
  else if(focus.npc!=nullptr && interact(*focus.npc)) {
    clearInput();
    }
  else if(focus.item!=nullptr && interact(*focus.item)) {
    clearInput();
    }

  if(focus.npc)
    actionFocus(*focus.npc); else
    emptyFocus();
  }

void PlayerControl::clearFocus() {
  currentFocus = Focus();
  }

void PlayerControl::actionFocus(Npc& other) {
  setTarget(&other);
  }

void PlayerControl::emptyFocus() {
  setTarget(nullptr);
  }

Focus PlayerControl::focus() const {
  return currentFocus;
  }

bool PlayerControl::hasActionFocus() const {
  if(!ctrl[Action::ActionGeneric])
    return false;
  return currentFocus.npc!=nullptr;
  }

bool PlayerControl::interact(Interactive &it) {
  auto w = Gothic::inst().world();
  if(w==nullptr || w->player()==nullptr)
    return false;
  auto pl = w->player();
  if(w->player()->isDown())
    return true;
  if(!canInteract())
    return false;

  if(CommandLine::inst().mmoClientUsesServer()) {
    auto* session = Gothic::inst().gameSession();
    if(session == nullptr)
      return true;
    const auto target = session->mmoServerEntityTarget(it);
    if(!target.has_value()) {
      Tempest::Log::e(
          "MMO interactive skipped: local object has no server binding");
      return true;
    }
    const bool readBookshelf = isBookInteractive(it);
    Tempest::Log::i("MMO interactive focus: tag=", it.tag(),
                    " focus=", it.focusName(),
                    " scheme=", it.schemeName(),
                    " owner=", it.ownerName(),
                    " read=", readBookshelf ? 1 : 0);
    const auto verb = readBookshelf ? Mmo::ClientInteractionVerb::Read
                                    : Mmo::ClientInteractionVerb::Use;
    if(readBookshelf) {
      // Native Gothic already owns the MOBSI animation timeline and invokes
      // doc_show from the bookshelf state function at the correct animation
      // point. Do not add a second MMO timer/onKeyInput path: it advances the
      // same state machine twice and makes a second click postpone/retrigger
      // the document. The server command remains the authority admission.
      // Native MOBSI execution is retained here only as the current graphical
      // fallback; semantic script effects are not authoritative and must be
      // replaced by explicit server-produced presentation events when that
      // pipeline is available.
      if(pl->interactive() == &it) {
        Tempest::Log::i(
            "MMO book duplicate action ignored: already_attached=1 tick=",
            w->tickCount());
        return true;
      }
      const bool attached = pl->setInteraction(&it);
      Tempest::Log::i("MMO book native presentation attach: attached=",
                      attached, " tick=", w->tickCount());
      if(!attached) {
        Tempest::Log::e(
            "MMO book native presentation blocked: attachment rejected");
        return true;
      }
      // attach() only puts the player on the bookshelf. The native MOBSI
      // state machine needs its first Forward step; otherwise the first
      // click only attaches and doc_show happens after a second click. The
      // native animation still decides the exact time for the document.
      it.onKeyInput(KeyCodec::Forward);
      Tempest::Log::i(
          "MMO book native presentation advance: action=forward tick=",
          w->tickCount());
    }
    return submitMmoInteraction(
        *w, *pl, *target, verb,
        "PlayerControl::interact(Interactive)",
        readBookshelf ? "read_bookshelf" :
        (it.isContainer() ? "use_container" :
        (it.isDoor() ? "use_door" : "use_interactive")));
  }

  if(it.isContainer()){
    inv.open(*pl,it);
    return true;
    }
  if(pl->setInteraction(&it)){
    }
  return true;
  }

bool PlayerControl::interact(Npc &other) {
  auto w = Gothic::inst().world();
  if(w==nullptr || w->player()==nullptr)
    return false;
  auto pl = w->player();
  if(pl->isDown())
    return true;
  if(!canInteract())
    return false;

  if(CommandLine::inst().mmoClientUsesServer()) {
    auto* session = Gothic::inst().gameSession();
    const auto target = session != nullptr
                            ? session->mmoServerEntityTarget(other)
                            : std::nullopt;
    Tempest::Log::i(
        "MMO NPC focus: instance=", other.instanceSymbol(),
        " persistent_id=", other.persistentId(),
        " server_replica=", other.isMmoServerReplica() ? 1 : 0,
        " position=", other.position().x, ",", other.position().y, ",",
        other.position().z,
        " target_binding=", target.has_value() ? 1 : 0);
    if(!target.has_value()) {
      Tempest::Log::e(
          "MMO NPC interaction skipped: local NPC has no server binding");
      return true;
    }

    const auto& loot = session->mmoServerCorpseLootPresentation();
    if(loot.replicatedDead(target->handle)) {
      if(!loot.canOpen(target->handle)) {
        w->script().printNothingToGet();
        return true;
      }
      static_cast<void>(inv.openServerCorpse(*pl,other));
      return true;
    }

    return submitMmoInteraction(
        *w, *pl, *target, Mmo::ClientInteractionVerb::Talk,
        "PlayerControl::interact(Npc)", "talk_to_npc");
  }

  auto state = pl->bodyStateMasked();
  if(other.isDown()) {
    if(state!=BS_STAND && state!=BS_SNEAK && state!=BS_SWIM && state!=BS_DIVE)
      return false;
    if(!inv.ransack(*w->player(),other))
      w->script().printNothingToGet();
    } else {
    if((state&BS_MAX)!=BS_NONE)
      return false;
    other.startDialog(*pl);
    }
  return true;
  }

bool PlayerControl::interact(Item &item) {
  auto w = Gothic::inst().world();
  if(w==nullptr || w->player()==nullptr)
    return false;
  auto pl = w->player();
  if(item.isTorchBurn() && pl->isUsingTorch())
    return false;
  if(pl->isDown())
    return true;
  if(!canInteract())
    return false;
  if(CommandLine::inst().mmoClientUsesServer()) {
    const auto playerPosition = pl->position();
    const auto itemPosition = item.position();
    if(!Mmo::Gameplay::canPickUpItem(
           {.x = playerPosition.x, .y = playerPosition.y, .z = playerPosition.z},
           pl->rotation(),
           {.x = itemPosition.x, .y = itemPosition.y, .z = itemPosition.z})) {
      Tempest::Log::i("MMO pickup skipped: item outside native distance or facing angle");
      return true;
    }
    auto* session = Gothic::inst().gameSession();
    if(session == nullptr)
      return true;
    const auto target = session->mmoServerEntityTarget(item);
    if(!target.has_value()) {
      Tempest::Log::e("MMO pickup skipped: no authoritative item binding item=",
                      item.displayName(), " position=", item.position().x, ",",
                      item.position().y, ",", item.position().z);
      return true;
    }
    if(session->mmoServerPickupPending(target->handle)) {
      Tempest::Log::i(
          "MMO pickup duplicate ignored: entity=", target->handle.id, ":",
          target->handle.generation);
      return true;
    }
    const auto& inventory = session->mmoServerInventoryPresentation().inventory();
    if(inventory.revision() == 0U) {
      Tempest::Log::e(
          "MMO pickup skipped: server inventory revision is zero entity=",
          target->handle.id, ":", target->handle.generation,
          " item=", item.displayName());
      return true;
    }
    Tempest::Log::i(
        "MMO pickup submit: entity=", target->handle.id,
        " revision=", target->revision,
        " quantity=", target->quantity,
        " inventory_revision=", inventory.revision(),
        " item=", item.displayName());
    const auto submitted = Mmo::submitClientPickupItem({
        .worldItem = target->handle,
        .amount = target->quantity,
        .expectedWorldItemRevision = target->revision,
        .expectedInventoryRevision = inventory.revision(),
    });
    Tempest::Log::i(
        "MMO pickup submission result: status=",
        static_cast<unsigned>(submitted.status),
        " submitted=", submitted.submitted() ? 1 : 0,
        " route_epoch=", submitted.command.routeEpoch,
        " sequence=", submitted.command.sequence,
        " dropped_count=", submitted.droppedCount,
        " entity=", target->handle.id, ":", target->handle.generation);
    if(submitted.submitted())
      session->trackMmoServerPickup(submitted.command, target->handle);
    return true;
  }
  const auto playerPosition = pl->position();
  const auto playerYawDegrees = pl->rotation();
  const auto itemPosition = item.position();
  const std::string itemName(item.displayName());
  const auto itemSymbol = item.clsId();
  const auto itemAmount = item.count();
  const auto accepted = pl->takeItem(item) != nullptr;
  NativeTelemetry::itemPickupAttempt(
      itemName, itemSymbol, itemAmount, accepted,
      playerPosition.x, playerPosition.y, playerPosition.z, playerYawDegrees,
      itemPosition.x, itemPosition.y, itemPosition.z);
  return accepted;
  }

void PlayerControl::moveFocus(FocusAction act) {
  auto w = Gothic::inst().world();
  auto c = Gothic::inst().camera();
  if(w==nullptr || c==nullptr || currentFocus.npc==nullptr)
    return;

  auto vp  = c->viewProj();
  auto pos = currentFocus.npc->centerPosition();
  vp.project(pos);

  Npc* next = nullptr;
  auto npos = Tempest::Vec3();
  for(uint32_t i=0; i<w->npcCount(); ++i) {
    auto npc = w->npcById(i);
    if(npc->isPlayer())
      continue;
    auto p = npc->centerPosition();
    vp.project(p);

    if(std::abs(p.x)>1.f || std::abs(p.y)>1.f || p.z<0.f)
      continue;

    if(!w->testFocusNpc(npc))
      continue;

    if(act==ActLeft && p.x<pos.x && (next==nullptr || npos.x<p.x)) {
      npos = p;
      next = npc;
      }
    if(act==ActRight && p.x>pos.x && (next==nullptr || npos.x>p.x)) {
      npos = p;
      next = npc;
      }
    }

  if(next==nullptr)
    return;
  currentFocus.npc = next;
  }

void PlayerControl::toggleWalkMode() {
  auto w = Gothic::inst().world();
  if(w==nullptr || w->player()==nullptr)
    return;
  auto pl = w->player();
  pl->setWalkMode(pl->walkMode()^WalkBit::WM_Walk);
  }

void PlayerControl::toggleSneakMode() {
  auto w = Gothic::inst().world();
  if(w==nullptr || w->player()==nullptr)
    return;
  auto pl = w->player();
  if(pl->canSneak())
    pl->setWalkMode(pl->walkMode()^WalkBit::WM_Sneak);
  }

bool PlayerControl::canInteract() const {
  auto w = Gothic::inst().world();
  if(w==nullptr || w->player()==nullptr)
    return false;
  auto pl = w->player();
  if(pl->weaponState()!=WeaponState::NoWeapon)
    return false;
  // In server-bound mode the local AI queue is only presentation state. It
  // must not prevent sending an authoritative interaction intent.
  if(!CommandLine::inst().mmoClientUsesServer() && pl->isAiBusy())
    return false;
  return true;
  }

void PlayerControl::clearInput() {
  movement.reset();
  std::memset(ctrl, 0,sizeof(ctrl));
  std::memset(actrl,0,sizeof(actrl));
  std::memset(wctrl,0,sizeof(wctrl));
  }

void PlayerControl::marvinF8(uint64_t dt) {
  auto w = Gothic::inst().world();
  if(w==nullptr || w->player()==nullptr)
    return;

  auto& pl  = *w->player();
  auto  pos = pl.position();
  float rot = pl.rotationRad();
  float s   = std::sin(rot), c = std::cos(rot);

  Tempest::Vec3 dp(c,0.8f,s);
  pos += dp*6000*float(dt)/1000.f;

  pl.changeAttribute(ATR_HITPOINTS,pl.attribute(ATR_HITPOINTSMAX),false);
  pl.changeAttribute(ATR_MANA,     pl.attribute(ATR_MANAMAX),     false);
  pl.clearState(false);
  pl.clearSpeed();
  pl.clearAiQueue();
  pl.setPosition(pos);
  pl.setInteraction(nullptr,true);
  pl.setAnim(AnimationSolver::Idle);

  if(auto c = Gothic::inst().camera())
    c->reset();
  }

void PlayerControl::marvinK(uint64_t dt) {
  auto w = Gothic::inst().world();
  if (w == nullptr || w->player() == nullptr)
    return;

  auto& pl = *w->player();
  auto  pos = pl.position();
  float rot = pl.rotationRad();
  float s = std::sin(rot), c = std::cos(rot);

  Tempest::Vec3 dp(c, 0.0f, s);
  pos += dp * 6000 * float(dt) / 1000.f;

  pl.clearState(false);
  pl.clearSpeed();
  pl.setPosition(pos);
  pl.setInteraction(nullptr,true);
  // pl.setAnim(AnimationSolver::Idle); // Original G2 behaviour: K doesn't stop running
  }

void PlayerControl::marvinO() {
  auto w = Gothic::inst().world();
  if (w == nullptr || w->player() == nullptr || w->player()->target() == nullptr)
    return;

  auto target = w->player()->target();

  w->setPlayer(target);
  }

Focus PlayerControl::findFocus(const Focus* prev) const {
  auto w = Gothic::inst().world();
  auto c = Gothic::inst().camera();
  if(w==nullptr)
    return Focus();
  if(w->player()!=nullptr && w->player()->isDown())
    return Focus();
  if(c!=nullptr && c->isCutscene())
    return Focus();
  if(!cacheFocus)
    prev = nullptr;

  if(prev)
    return w->findFocus(*prev);
  return w->findFocus(Focus());
  }

bool PlayerControl::tickCameraMove(uint64_t dt) {
  auto w = Gothic::inst().world();
  if(w==nullptr)
    return false;

  Npc*  pl     = w->player();
  auto  camera = Gothic::inst().camera();
  if(camera==nullptr || (pl!=nullptr && !camera->isFree()))
    return false;

  rotMouse = 0;
  if(ctrl[KeyCodec::Left] || (ctrl[KeyCodec::RotateL] && ctrl[KeyCodec::Jump])) {
    camera->moveLeft(dt);
    return true;
    }
  if(ctrl[KeyCodec::Right] || (ctrl[KeyCodec::RotateR] && ctrl[KeyCodec::Jump])) {
    camera->moveRight(dt);
    return true;
    }

  auto turningVal = movement.turnRightLeft.value();
  if(turningVal > 0.f)
    camera->rotateRight(dt);
  else if(turningVal < 0.f)
    camera->rotateLeft(dt);

  auto forwardVal = movement.forwardBackward.value();
  if(forwardVal > 0.f)
    camera->moveForward(dt);
  else if(forwardVal < 0.f)
    camera->moveBack(dt);
  return true;
  }

PlayerControl::MovementInputSnapshot
PlayerControl::movementInputSnapshot() const noexcept {
  return {
      .forward = movement.forwardBackward.value(),
      .right = movement.strafeRightLeft.value(),
      .turn = movement.turnRightLeft.value(),
      .jump = ctrl[Action::Jump],
      .action = ctrl[Action::ActionGeneric],
  };
}

bool PlayerControl::tickMove(uint64_t dt) {
  auto w = Gothic::inst().world();
  if(w==nullptr)
    return false;
  const float dtF = float(dt)/1000.f;

  Npc*  pl     = w->player();
  auto  camera = Gothic::inst().camera();

  if(w->isCutsceneLock())
    clearInput();

  if(tickCameraMove(dt))
    return true;

  if(ctrl[Action::K_F8] && Gothic::inst().isMarvinEnabled())
    marvinF8(dt);
  if(ctrl[Action::K_K] && Gothic::inst().isMarvinEnabled())
    marvinK(dt);
  cacheFocus = ctrl[Action::ActionGeneric];
  if(camera!=nullptr)
    camera->setLookBack(ctrl[Action::LookBack]);

  if(pl==nullptr)
    return true;

  implMove(dt);

  float runAngle = pl->runAngle();
  if(runAngle!=0.f || std::fabs(runAngleDest)>0.01f) {
    const float speed = 35.f;
    if(runAngle<runAngleDest) {
      runAngle+=speed*dtF;
      if(runAngle>runAngleDest)
        runAngle = runAngleDest;
      pl->setRunAngle(runAngle);
      }
    else if(runAngle>runAngleDest) {
      runAngle-=speed*dtF;
      if(runAngle<runAngleDest)
        runAngle = runAngleDest;
      pl->setRunAngle(runAngle);
      }
    }

  rotMouseY = 0;
  return true;
  }

void PlayerControl::implMove(uint64_t dt) {
  auto  w         = Gothic::inst().world();
  Npc&  pl        = *w->player();
  float rot       = pl.rotation();
  float rotY      = pl.rotationY();
  // 100 / 200 according to some sources, yet my mesures are 90/180
  float rspeed    = (pl.weaponState()==WeaponState::NoWeapon ? 90.f : 180.f)*(float(dt)/1000.f);
  auto  ws        = pl.weaponState();
  auto  bs        = pl.bodyStateMasked();
  bool  allowRot  = !ctrl[KeyCodec::ActionGeneric] && pl.isRotationAllowed();

  Npc::Anim ani = Npc::Anim::Idle;

  if(bs==BS_DEAD)
    return;
  if(bs==BS_UNCONSCIOUS)
    return;

  if(!pl.isAiQueueEmpty()) {
    runAngleDest = 0;
    return;
    }

  if(pl.interactive()!=nullptr) {
    runAngleDest = 0;
    implMoveMobsi(pl,dt);
    return;
    }

  if(pl.canSwitchWeapon()) {
    if(wctrl[WeaponClose]) {
      wctrl[WeaponClose] = !(pl.closeWeapon(false) || pl.isMonster());
      return;
      }
    if(wctrl[WeaponMele]) {
      bool ret=false;
      if(pl.currentMeleeWeapon()!=nullptr)
        ret = pl.drawWeaponMelee(); else
        ret = pl.drawWeaponFist();
      wctrl[WeaponMele] = !ret;
      wctrlLast         = WeaponMele;
      if(!wctrl[WeaponMele])
        return;
      }
    if(wctrl[WeaponBow]) {
      if(pl.currentRangedWeapon()!=nullptr) {
        wctrl[WeaponBow] = !pl.drawWeaponBow();
        wctrlLast        = WeaponBow;
        } else {
        wctrl[WeaponBow] = false;
        }
      if(!wctrl[WeaponBow])
        return;
      }
    for(uint8_t i=0;i<8;++i) {
      if(wctrl[Weapon3+i]){
        if(pl.inventory().currentSpell(i)!=nullptr){
          bool ret = pl.drawMage(uint8_t(3+i));
          wctrl[Weapon3+i] = !ret;
          wctrlLast = static_cast<WeaponAction>(Weapon3+i);
          if(ret) {
            if(auto spl = pl.inventory().currentSpell(i)) {
              Gothic::inst().onPrint(spl->description());
              }
            }
          } else {
          wctrl[Weapon3+i] = false;
          return;
          }
        }
      }
    }

  if(!pl.isInState(ScriptFn()) || dlg.isActive()) {
    runAngleDest = 0;
    return;
    }

  int rotation = 0;
  if(allowRot) {
    if(this->wantsToTurnLeft()) {
      rot += rspeed;
      rotation = -1;
      rotMouse=0;
      }
    if(this->wantsToTurnRight()) {
      rot -= rspeed;
      rotation = 1;
      rotMouse=0;
      }
    if(std::fabs(rotMouse)>0.f) {
      if(rotMouse>0)
        rotation = -1; else
        rotation = 1;
      rot += rotMouse;
      rotMouse  = 0;
      }
    rotY+=rotMouseY;
    } else {
    rotMouse  = 0;
    rotMouseY = 0;
    }

  pl.setDirectionY(rotY);
  if(pl.isFalling() || pl.isSlide() || pl.isInAir() || pl.isJump() || pl.isJumpUp()){
    pl.setDirection(rot);
    runAngleDest = 0;
    return;
    }

  if(casting) {
    if(!actrl[ActForward] || (Gothic::inst().version().game==1 && pl.attribute(ATR_MANA)==0)) {
      casting = false;
      pl.endCastSpell(true);
      }
    return;
    }

  if(ctrl[Action::K_ENTER]) {
    pl.transformBack();
    ctrl[Action::K_ENTER] = false;
    }

  if((ws==WeaponState::Bow || ws==WeaponState::CBow) && pl.hasAmmunition()) {
    if(actrl[ActGeneric] || actrl[ActForward]) {
      if(auto other = pl.target()) {
        auto dp = other->position()-pl.position();
        pl.turnTo(dp.x,dp.z,true,dt);
        pl.aimBow();
        }
      else if(currentFocus.interactive!=nullptr) {
        auto dp = currentFocus.interactive->position()-pl.position();
        pl.turnTo(dp.x,dp.z,false,dt);
        pl.aimBow();
        }
      else {
        pl.aimBow();
        }

      if(actrl[ActLeft]) {
        moveFocus(ActLeft);
        actrl[ActLeft]  = false;
        }
      if(actrl[ActRight]) {
        moveFocus(ActRight);
        actrl[ActRight]  = false;
        }
      if(!actrl[ActForward])
        return;
      }
    }

  if(ws==WeaponState::Mage) {
    if(actrl[ActGeneric] || actrl[ActForward] || ctrl[KeyCodec::ActionGeneric]) {
      if(auto other = pl.target()) {
        auto dp = other->centerPosition() - pl.centerPosition();
        pl.turnTo(dp.x,dp.z,true,dt);
        } else
      if(currentFocus.interactive!=nullptr) {
        auto dp = currentFocus.interactive->position()-pl.position();
        pl.turnTo(dp.x,dp.z,false,dt);
        }

      if(actrl[ActLeft]) {
        moveFocus(ActLeft);
        actrl[ActLeft]  = false;
        }
      if(actrl[ActRight]) {
        moveFocus(ActRight);
        actrl[ActRight]  = false;
        }
      if(!actrl[ActForward]) {
        pl.setAnim(Npc::Anim::Idle);
        return;
        }
      }
    }

  if(actrl[ActForward] || actrl[ActMove]) {
    ctrl [Action::Forward] = actrl[ActMove];
    actrl[ActMove]         = false;
    if(ws!=WeaponState::Mage && !(g2Ctrl && (ws==WeaponState::Bow || ws==WeaponState::CBow))) {
      actrl[ActForward] = false;
      if(!ctrl[Action::Forward])
        movement.reset();
      }
    switch(ws) {
      case WeaponState::NoWeapon:
        break;
      case WeaponState::Fist:
        pl.fistShoot();
        return;
      case WeaponState::W1H:
      case WeaponState::W2H: {
        pl.swingSword();
        return;
        }
      case WeaponState::Bow:
      case WeaponState::CBow: {
        pl.shootBow(currentFocus.interactive);
        return;
        }
      case WeaponState::Mage: {
        casting = (pl.beginCastSpell()==Npc::BC_Invest);
        if(!casting)
          actrl[ActForward] = false;
        return;
        }
      }
    }

  if(actrl[ActKill]) {
    if((ws==WeaponState::W1H || ws==WeaponState::W2H) && pl.target()!=nullptr && pl.canFinish(*pl.target()))
      pl.finishingMove();
    actrl[ActKill] = false;
    }

  if(actrl[ActLeft] || actrl[ActRight] || actrl[ActBack]) {
    auto ws = pl.weaponState();
    if(ws==WeaponState::Fist) {
      if(actrl[ActBack])
        pl.blockFist();
      return;
      }
    else if(ws==WeaponState::W1H || ws==WeaponState::W2H) {
      if(actrl[ActLeft] && pl.swingSwordL()) {
        movement.strafeRightLeft.reset();
        }
      else if(actrl[ActRight] && pl.swingSwordR()) {
        movement.strafeRightLeft.reset();
        }
      else if(actrl[ActBack] && pl.blockSword()) {
        // movement.forwardBackward.reset();
        }

      actrl[ActLeft]  = false;
      actrl[ActRight] = false;
      // actrl[ActBack]  = false;
      return;
      }
    else if(ws==WeaponState::Mage) {
      if(actrl[ActLeft]) {
        moveFocus(ActLeft);
        actrl[ActLeft]  = false;
        }
      if(actrl[ActRight]) {
        moveFocus(ActRight);
        actrl[ActRight]  = false;
        }
      }
    }

  if(this->wantsToStrafeLeft()) {
    ani = Npc::Anim::MoveL;
    }
  else if(this->wantsToStrafeRight()) {
    ani = Npc::Anim::MoveR;
    }
  else if(this->wantsToMoveForward()) {
    if((pl.walkMode()&WalkBit::WM_Dive)!=WalkBit::WM_Dive) {
      ani = Npc::Anim::Move;
      }
    else if(pl.isDive()) {
      pl.setDirectionY(rotY - rspeed);
      return;
      }
    }
  else if(this->wantsToMoveBackward()) {
    if((pl.walkMode()&WalkBit::WM_Dive)!=WalkBit::WM_Dive) {
      ani = Npc::Anim::MoveBack;
      } else if(pl.isDive()) {
      pl.setDirectionY(rotY + rspeed);
      return;
      }
    }


  if(ctrl[Action::Jump]) {
    if(pl.bodyStateMasked()==BS_JUMP) {
      ani = Npc::Anim::Idle;
      }
    else if(pl.isDive()) {
      ani = Npc::Anim::Move;
      }
    else if(pl.isSwim()) {
      pl.startDive();
      }
    else if(pl.isInWater()) {
      auto& g  = w->script().guildVal();
      auto  gl = pl.guild();

      if(0<=gl && gl<GIL_MAX && pl.isStanding()) {
        MoveAlgo::JumpStatus jump;
        jump.anim   = Npc::Anim::JumpUp;
        jump.height = float(g.jumpup_height[gl])+pl.position().y;
        pl.startClimb(jump);
        }
      }
    else if(pl.isStanding()) {
      auto jump = pl.tryJump();
      if(!pl.isFalling() && !pl.isSlide() && jump.anim!=Npc::Anim::Jump){
        pl.startClimb(jump);
        return;
        }
      ani = Npc::Anim::Jump;
      }
    else if(!pl.isAttackAnim() && !pl.isCasting()) {
      ani = Npc::Anim::Jump;
      }
    }

  if(!pl.isCasting()) {
    if(ani==Npc::Anim::Jump) {
      pl.setAnimRotate(0);
      rotation = 0;
      }

    if(pl.isAttackAnim()) {
      if((ani==Npc::Anim::MoveL || ani==Npc::Anim::MoveR/* || ani==Npc::Anim::MoveBack*/) && pl.hasState(BS_RUN)) {
        ani = Npc::Anim::Idle;
        }

      if(!pl.hasState(BS_RUN) && ani==Npc::Anim::Idle) {
        // charge-run
        ani = Npc::Anim::NoAnim;
        }
      if((ani==Npc::Anim::MoveL || ani==Npc::Anim::MoveR) &&
          pl.hasState(BS_STAND) && pl.hasState(BS_HIT)) {
        // no charge to strafe transition
        ani = Npc::Anim::NoAnim;
        }
      }

    if(bs==BS_LIE) {
      ani = (ani==Npc::Anim::Move) ? Npc::Anim::Idle : Npc::Anim::NoAnim;
      rot = pl.rotation();
      }

    if(ani!=Npc::Anim::NoAnim)
      pl.setAnim(ani);
    }

  setAnimRotate(pl, rot, ani==Npc::Anim::Idle ? rotation : 0, movement.turnRightLeft.any(), dt);
  if(actrl[ActGeneric] || ani==Npc::Anim::MoveL || ani==Npc::Anim::MoveR || pl.isFinishingMove()) {
    processAutoRotate(pl,rot,dt);
    }

  if(ani==Npc::Anim::Move && (rotation!=0 || rotY!=0)) {
    assignRunAngle(pl,rot,dt);
    } else {
    assignRunAngle(pl,pl.rotation(),dt);
    }
  pl.setDirection(rot);
  }

void PlayerControl::implMoveMobsi(Npc& pl, uint64_t /*dt*/) {
  // animation handled in MOBSI
  auto inter = pl.interactive();

  if(ctrl[KeyCodec::Back] && !inter->isLadder()) {
    pl.setInteraction(nullptr);
    return;
    }

  if(inter->needToLockpick(pl) && !inter->isCracked()) {
    return;
    }

  if(!inter->isLadder() && inter->isStaticState() && !inter->isDetachState(pl)) {
    auto stateId = inter->stateId();
    if(inter->canQuitAtState(pl,stateId))
      pl.setInteraction(nullptr,false);
    }

  if(inter->isLadder()) {
    if(ctrl[KeyCodec::ActionGeneric]) {
      inter->onKeyInput(KeyCodec::ActionGeneric);
      ctrl[KeyCodec::ActionGeneric] = false;
      }
    else if(ctrl[KeyCodec::Forward]) {
      inter->onKeyInput(KeyCodec::Forward);
      }
    else if(ctrl[KeyCodec::Back]) {
      inter->onKeyInput(KeyCodec::Back);
      }
    }
  }

void PlayerControl::processPickLock(Npc& pl, Interactive& inter, KeyCodec::Action k) {
  auto                   w             = Gothic::inst().world();
  auto&                  script        = w->script();
  const size_t           ItKE_lockpick = script.lockPickId();

  char ch = '\0';
  if(k==KeyCodec::Left || k==KeyCodec::RotateL)
    ch = 'L';
  else if(k==KeyCodec::Right || k==KeyCodec::RotateR)
    ch = 'R';
  else if(k==KeyCodec::Back) {
    quitPicklock(pl);
    return;
    }
  else
    return;

  auto cmp = inter.pickLockCode();
  while(pickLockProgress<cmp.size()) {
    auto c = cmp[pickLockProgress];
    if(c=='l' || c=='L' || c=='r' || c=='R')
      break;
    ++pickLockProgress;
    }

  if(pickLockProgress<cmp.size() && std::toupper(cmp[pickLockProgress])!=ch) {
    pickLockProgress = 0;
    const int32_t dex = Gothic::inst().version().game==2 ? pl.attribute(ATR_DEXTERITY) : (100 - pl.talentValue(TALENT_PICKLOCK));
    if(dex<=int32_t(script.rand(100)))  {
      script.invokePickLock(pl,0,1);
      pl.delItem(ItKE_lockpick,1);
      if(pl.inventory().itemCount(ItKE_lockpick)==0) {
        quitPicklock(pl);
        return;
        }
      } else {
      script.invokePickLock(pl,0,0);
      }
    } else {
    pickLockProgress++;
    if(pickLockProgress>=cmp.size()) {
      script.invokePickLock(pl,1,1);
      inter.setAsCracked(true);
      pickLockProgress = 0;
      } else {
      script.invokePickLock(pl,1,0);
      }
    }
  }

void PlayerControl::processLadder(Npc& pl, Interactive& inter, KeyCodec::Action key) {
  if(key!=KeyCodec::ActionGeneric && key!=KeyCodec::Forward && key!=KeyCodec::Back)
    return;

  ctrl[key] = true;
  inter.onKeyInput(key);
  }

void PlayerControl::quitPicklock(Npc& pl) {
  inv.close();
  pickLockProgress = 0;
  pl.setInteraction(nullptr);
  }

void PlayerControl::assignRunAngle(Npc& pl, float rotation, uint64_t dt) {
  float dtF    = (float(dt)/1000.f);
  auto  camera = Gothic::inst().camera();

  float dest = 0;
  if(camera!=nullptr && pl.walkMode()==WalkBit::WM_Run && pl.bodyState()==BS_RUN) {
    const float az   = camera->azimuth();
    const float maxV = 14.5f;
    dest = std::min(std::abs(az), maxV)*(az>=0 ? 1 : -1);
    }

  float a = std::min(dtF*5.f, 1.f);
  runAngleDest = runAngleDest*(1.f-a)+dest*a;
  }

void PlayerControl::setAnimRotate(Npc& pl, float rotation, int anim, bool force, uint64_t dt) {
  float dtF    = (float(dt)/1000.f);
  float angle  = pl.rotation();
  float dangle = (rotation-angle)/dtF;
  auto& wrld   = pl.world();

  if(std::fabs(dangle)<30.f && !force) // 30 deg per second threshold
    anim = 0;
  if(anim!=0 && pl.isAttackAnim())
    anim = 0;
  if(rotationAni==anim && anim!=0)
    force = true;
  if(!force && wrld.tickCount()<turnAniSmooth)
    return;
  turnAniSmooth = wrld.tickCount() + 100;
  rotationAni   = anim;
  pl.setAnimRotate(anim);
  }

void PlayerControl::processAutoRotate(Npc& pl, float& rot, uint64_t dt) {
  if(auto other = pl.target()) {
    if(pl.weaponState()==WeaponState::NoWeapon || pl.isFinishingMove()){
      pl.setTarget(nullptr);
      }
    else if(!pl.isAttack()) {
      auto  dp   = other->centerPosition() - pl.centerPosition();
      auto  gl   = pl.guild();
      float step = float(pl.world().script().guildVal().turn_speed[gl]);
      if(actrl[ActGeneric])
        step*=2.f;
      pl.rotateTo(dp.x,dp.z,step,AnimationSolver::TurnType::Std,dt);
      rot = pl.rotation();
      }
    }
  }
