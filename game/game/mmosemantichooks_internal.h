#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "mmosemantichooks.h"
#include "mmoclientadapter.h"
#include "../../../shared/game/mmo/mmosemanticevents.h"

class WayPoint;

namespace Mmo::Hooks::Detail {

void beginCaptureSuppression() noexcept;
void endCaptureSuppression() noexcept;
[[nodiscard]] bool isCaptureSuppressed() noexcept;

[[nodiscard]] constexpr ClientMovementState movementState(
    const bool inAir,
    const bool falling,
    const bool fallingDeep,
    const bool sliding,
    const bool jumping,
    const bool jumpingUp,
    const bool swimming,
    const bool diving,
    const bool inWater) noexcept {
  ClientMovementState state = ClientMovementState::None;
  if(inAir) state |= ClientMovementState::InAir;
  if(falling) state |= ClientMovementState::Falling;
  if(fallingDeep) state |= ClientMovementState::FallingDeep;
  if(sliding) state |= ClientMovementState::Sliding;
  if(jumping) state |= ClientMovementState::Jumping;
  if(jumpingUp) state |= ClientMovementState::JumpingUp;
  if(swimming) state |= ClientMovementState::Swimming;
  if(diving) state |= ClientMovementState::Diving;
  if(inWater) state |= ClientMovementState::InWater;
  return state;
}

void appendUInt(std::string& out, std::uint64_t value);
void appendInt(std::string& out, std::int64_t value);
void appendFloat(std::string& out, float value);
void appendBool(std::string& out, bool value);
void appendEscaped(std::string& out, std::string_view value);
void appendScriptContext(std::string& out, std::uint32_t scriptFunctionSymbol, std::string_view scriptFunctionName);
void appendWorld(std::string& out, const World& world);
template<class Vector3>
void appendVec3(std::string& out, const char* key, const Vector3& v) {
  out.append(",\"");
  out.append(key);
  out.append("\":{");
  out.append("\"x\":"); appendFloat(out, v.x);
  out.append(",\"y\":"); appendFloat(out, v.y);
  out.append(",\"z\":"); appendFloat(out, v.z);
  out.push_back('}');
}

[[nodiscard]] std::string_view characterKey() noexcept;
[[nodiscard]] std::string characterEntityKey();
[[nodiscard]] std::string characterTargetKey(std::string_view suffix);
[[nodiscard]] std::string actorKey(const Npc& npc);
[[nodiscard]] std::string playerOrDefaultKey(const World& world);
[[nodiscard]] std::string worldItemKey(std::string_view worldName, std::uint32_t persistentId, std::size_t symbol);
[[nodiscard]] std::string itemTemplateKey(std::size_t symbol);
[[nodiscard]] std::string interactiveEntityKey(World& world, Interactive& interactive);
[[nodiscard]] std::string triggerEntityKey(World& world, std::uint32_t vobId, std::string_view name);
[[nodiscard]] std::string moverEntityKey(World& world, std::uint32_t vobId, std::string_view name);
[[nodiscard]] std::string scriptKey(std::size_t symbolIndex, std::uint16_t valueIndex);
[[nodiscard]] std::string symbolKey(const char* prefix, std::size_t symbolIndex);
[[nodiscard]] std::string npcEntityKey(std::string_view worldName, std::uint32_t persistentId, std::size_t symbol);
[[nodiscard]] std::string_view waypointName(const WayPoint* waypoint) noexcept;
[[nodiscard]] std::string waypointKey(const World& world, const WayPoint* waypoint);
[[nodiscard]] std::string npcTargetKey(Npc* npc);
[[nodiscard]] std::string scriptFunctionKey(std::size_t function);
void appendInteractiveIdentity(std::string& out, World& world, Interactive& interactive);
void appendNpcIdentity(std::string& out, const char* prefix, Npc& npc);

[[nodiscard]] std::string weaponStateName(WeaponState state);
[[nodiscard]] float observedWeaponRange(Npc& actor);
[[nodiscard]] float observedAttackRange(Npc& actor, const Npc* target);
[[nodiscard]] float observedFightRangeBase(Npc& actor);

[[nodiscard]] bool isLiveWorldTick(const World& world) noexcept;
[[nodiscard]] bool shouldCapturePlayerAction(Npc& actor) noexcept;
[[nodiscard]] bool shouldCapturePlayerAction(Npc* actor) noexcept;
[[nodiscard]] bool shouldCaptureTransfer(const World& world, const Npc* sourceNpc) noexcept;
[[nodiscard]] bool shouldCaptureWorldAiAction(Npc& actor, Npc* other = nullptr) noexcept;
[[nodiscard]] bool shouldCapturePlayerRelated(Npc& actor, Npc* other = nullptr) noexcept;
[[nodiscard]] bool hasRecentPlayerWorldInteraction(World& world) noexcept;

void submit(SemanticActionKind kind, std::string targetKey, std::string payload, std::uint64_t tick) noexcept;
void submitObservedNpcState(SemanticActionKind kind, Npc& actor, std::string target, std::string payload) noexcept;

void onClientBootstrapRequest(World& world,
                              const char* sourceLocation,
                              const char* reason) noexcept;

bool shouldCaptureScriptAction(Npc* actor) noexcept;

void onWorldTimeChanged(World& world,
                        gtime before,
                        gtime after,
                        const char* sourceLocation,
                        const char* reason) noexcept;

void onCharacterMovementProposal(Npc& actor,
                                 std::uint64_t fromTick,
                                 float fromX,
                                 float fromY,
                                 float fromZ,
                                 float fromYaw,
                                 std::int32_t fromHealthCurrent,
                                 std::int32_t fromHealthMax,
                                 std::int32_t fromManaCurrent,
                                 std::int32_t fromManaMax,
                                 bool fromInAir,
                                 bool fromFalling,
                                 bool fromFallingDeep,
                                 bool fromSlide,
                                 bool fromJump,
                                 bool fromJumpUp,
                                 bool fromSwim,
                                 bool fromDive,
                                 bool fromInWater,
                                 const char* sourceLocation,
                                 const char* reason) noexcept;

void onCharacterCheckpoint(Npc& actor,
                           const char* sourceLocation,
                           const char* reason) noexcept;

void onSaveCheckpointManifest(World& world,
                              std::string_view slotPath,
                              std::string_view displayName,
                              const char* sourceLocation,
                              const char* reason) noexcept;

void onInteractiveUsed(World& world,
                       Interactive& interactive,
                       Npc& actor,
                       const char* sourceLocation,
                       const char* reason) noexcept;

void onInteractiveStateChanged(World& world,
                               Interactive& interactive,
                               const Npc* actor,
                               std::int32_t stateBefore,
                               std::int32_t stateAfter,
                               bool lockedBefore,
                               bool lockedAfter,
                               bool crackedBefore,
                               bool crackedAfter,
                               const char* sourceLocation,
                               const char* reason) noexcept;

void onWorldTriggerEvent(World& world,
                         std::uint32_t triggerVobId,
                         std::string_view triggerName,
                         std::string_view targetName,
                         std::string_view eventTarget,
                         std::string_view eventEmitter,
                         std::uint8_t eventType,
                         std::string_view eventTypeName,
                         const char* sourceLocation,
                         const char* reason) noexcept;

void onMoverStateChanged(World& world,
                         std::uint32_t moverVobId,
                         std::string_view moverName,
                         std::int32_t stateBefore,
                         std::int32_t stateAfter,
                         std::uint32_t frame,
                         std::uint32_t targetFrame,
                         std::string_view stateBeforeName,
                         std::string_view stateAfterName,
                         const char* sourceLocation,
                         const char* reason) noexcept;

void onWorldItemPickedUp(Npc& actor,
                         const Item& inventoryItem,
                         std::uint32_t sourceWorldItemPersistentId,
                         std::size_t sourceItemSymbol,
                         std::size_t sourceAmount,
                         const char* sourceLocation) noexcept;

void onWorldItemRemoved(World& world,
                        const Item& worldItem,
                        const char* sourceLocation) noexcept;

void onInventoryTransfer(World& world,
                         const Npc* sourceNpc,
                         std::size_t itemSymbol,
                         std::uint32_t sourceItemPersistentId,
                         std::size_t amount,
                         bool movedWholeInstance,
                         const char* sourceLocation) noexcept;

void onItemEquipped(Npc& actor,
                    const Item& item,
                    std::uint8_t slot,
                    const char* sourceLocation) noexcept;

void onItemUnequipped(Npc& actor,
                      const Item& item,
                      std::uint8_t slot,
                      const char* sourceLocation) noexcept;

void onWeaponStateChanged(Npc& actor,
                          WeaponState previousState,
                          WeaponState newState,
                          const char* sourceLocation,
                          const char* reason) noexcept;

void onCombatIntent(Npc& actor,
                    std::string_view combatAction,
                    std::string_view intentState,
                    const char* sourceLocation,
                    const char* reason) noexcept;

void onContainerInventoryTaken(Npc& actor,
                               Interactive& container,
                               std::size_t itemSymbol,
                               std::uint32_t sourceItemPersistentId,
                               std::size_t amount,
                               const char* sourceLocation) noexcept;

void onNpcInventoryLooted(Npc& looter,
                          Npc& sourceNpc,
                          std::size_t itemSymbol,
                          std::uint32_t sourceItemPersistentId,
                          std::size_t amount,
                          const char* sourceLocation) noexcept;

void onCharacterItemDropped(Npc& actor,
                            const Item& worldItem,
                            std::size_t itemSymbol,
                            std::uint32_t sourceItemPersistentId,
                            std::size_t amount,
                            const char* sourceLocation) noexcept;

void onTradeBuyFromNpc(Npc& buyer,
                       Npc& vendor,
                       std::size_t itemSymbol,
                       std::uint32_t vendorItemPersistentId,
                       std::size_t amount,
                       std::int32_t unitPrice,
                       std::size_t goldBefore,
                       std::size_t goldAfter,
                       const char* sourceLocation) noexcept;

void onTradeSellToNpc(Npc& seller,
                      Npc& buyer,
                      std::size_t itemSymbol,
                      std::uint32_t sellerItemPersistentId,
                      std::size_t amount,
                      std::int32_t unitPrice,
                      std::size_t goldBefore,
                      std::size_t goldAfter,
                      const char* sourceLocation) noexcept;

void onCharacterItemConsumed(Npc& actor,
                             std::size_t itemSymbol,
                             std::uint32_t itemPersistentId,
                             std::size_t amount,
                             std::string_view reason,
                             const char* sourceLocation) noexcept;

void onCharacterAttributeChanged(Npc& actor,
                                 Attribute attribute,
                                 std::int32_t valueBefore,
                                 std::int32_t valueAfter,
                                 std::int32_t requestedDelta,
                                 Npc* sourceActor,
                                 const char* sourceLocation) noexcept;

void onNpcLifecycleChanged(Npc& actor,
                           Npc* sourceActor,
                           bool dead,
                           bool unconscious,
                           const char* sourceLocation) noexcept;

void onObservedNpcAuthorityState(Npc& actor,
                                 const char* sourceLocation,
                                 const char* reason) noexcept;

void onNpcDialogLineQueued(Npc& speaker,
                           Npc& listener,
                           std::string_view outputName,
                           const char* sourceLocation) noexcept;

void onScriptIntChanged(Npc& actor,
                        std::uint32_t scriptFunctionSymbol,
                        std::string_view scriptFunctionName,
                        std::size_t symbolIndex,
                        std::uint16_t valueIndex,
                        std::string_view symbolName,
                        std::int32_t valueBefore,
                        std::int32_t valueAfter,
                        const char* sourceLocation) noexcept;

void onCharacterProgressionChanged(Npc& actor,
                                   std::uint32_t scriptFunctionSymbol,
                                   std::string_view scriptFunctionName,
                                   std::int32_t levelBefore,
                                   std::int32_t levelAfter,
                                   std::int32_t experienceBefore,
                                   std::int32_t experienceAfter,
                                   std::int32_t experienceNextBefore,
                                   std::int32_t experienceNextAfter,
                                   std::int32_t learningPointsBefore,
                                   std::int32_t learningPointsAfter,
                                   const char* sourceLocation) noexcept;

void onKnownDialogChanged(Npc& actor,
                          std::uint32_t scriptFunctionSymbol,
                          std::string_view scriptFunctionName,
                          std::size_t npcSymbol,
                          std::string_view npcSymbolName,
                          std::size_t infoSymbol,
                          std::string_view infoSymbolName,
                          bool known,
                          const char* sourceLocation) noexcept;

void onQuestChanged(Npc& actor,
                    std::uint32_t scriptFunctionSymbol,
                    std::string_view scriptFunctionName,
                    std::string_view questKey,
                    std::string_view status,
                    std::size_t entryCount,
                    const char* sourceLocation) noexcept;

} // namespace Mmo::Hooks::Detail
