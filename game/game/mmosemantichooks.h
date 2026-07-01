#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "game/constants.h"
#include "gametime.h"

class Interactive;
class Item;
class Npc;
class World;


namespace Mmo::Hooks {

class ScopedCaptureSuppression final {
  public:
    ScopedCaptureSuppression() noexcept;
    ScopedCaptureSuppression(const ScopedCaptureSuppression&) = delete;
    ScopedCaptureSuppression& operator=(const ScopedCaptureSuppression&) = delete;
    ~ScopedCaptureSuppression();

  private:
    bool active = true;
};

[[nodiscard]] bool shouldCaptureScriptAction(Npc* actor) noexcept;
[[nodiscard]] bool isCaptureSuppressed() noexcept;

void onClientBootstrapRequest(World& world,
                              const char* sourceLocation,
                              const char* reason = "client_bootstrap_request") noexcept;

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
                                 const char* reason = "movement_delta_proposal") noexcept;

void onCharacterCheckpoint(Npc& actor,
                           const char* sourceLocation,
                           const char* reason = "step39_periodic_movement_checkpoint") noexcept;

void onSaveCheckpointManifest(World& world,
                              std::string_view slotPath,
                              std::string_view displayName,
                              const char* sourceLocation,
                              const char* reason = "native_save_checkpoint_manifest") noexcept;


void onWorldTimeChanged(World& world,
                        gtime before,
                        gtime after,
                        const char* sourceLocation,
                        const char* reason = "world_time_changed") noexcept;


void onInteractiveUsed(World& world,
                       Interactive& interactive,
                       Npc& actor,
                       const char* sourceLocation,
                       const char* reason = "interactive_use_accepted") noexcept;

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
                               const char* reason = "interactive_state_changed") noexcept;


void onWorldTriggerEvent(World& world,
                         std::uint32_t triggerVobId,
                         std::string_view triggerName,
                         std::string_view targetName,
                         std::string_view eventTarget,
                         std::string_view eventEmitter,
                         std::uint8_t eventType,
                         std::string_view eventTypeName,
                         const char* sourceLocation,
                         const char* reason = "world_trigger_event") noexcept;

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
                         const char* reason = "mover_state_changed") noexcept;

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
                          const char* reason = "weapon_state_changed") noexcept;

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

} // namespace Mmo::Hooks























