#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mmosemanticevents.h"

namespace Mmo::Net {

enum class PacketKind : std::uint16_t {
  ClientAction         = 1,
  ServerAck            = 2,
  ServerSnapshotChunk  = 3,
  ServerDiagnostic     = 4,
  ServerLiveDelta      = 5,
  ClientCombatDamage   = 6,
  ClientMovement       = 7,
  ClientInventory      = 8,
  ClientWorldState     = 9,
  ClientNpcState       = 10,
  ClientEconomy        = 11,
  ClientSessionControl = 12,
  ClientDialogState    = 13,
  ClientCharacterEvent = 14,
};

enum class ServerAckKind : std::uint16_t {
  GenericAction = 1,
  Bootstrap     = 2,
  Movement      = 3,
};

enum class ServerLiveDeltaKind : std::uint16_t {
  Generic             = 1,
  MovementCorrection  = 2,
  CharacterStats      = 3,
  Inventory           = 4,
  Equipment           = 5,
  WorldItem           = 6,
  InteractiveState    = 7,
  Combat              = 8,
  Story               = 9,
  PerceptionReaction  = 10,
};

enum ServerLiveDeltaFlags : std::uint32_t {
  ServerLiveDeltaHasPosition             = 0x00000001u,
  ServerLiveDeltaHasStats                = 0x00000002u,
  ServerLiveDeltaRequiresSnapshotRefresh = 0x00000004u,
  ServerLiveDeltaHasDebugPayload         = 0x80000000u,
};

inline constexpr std::size_t CombatDamageTypeCount = 8;

enum class ClientCombatDamageKind : std::uint16_t {
  Unknown = 0,
  Melee   = 1,
  Ranged  = 2,
  Magic   = 3,
  Fall    = 4,
};

enum class ClientCombatDamageModifier : std::uint16_t {
  Normal  = 0,
  Double  = 1,
  Half    = 2,
  Blocked = 3,
};

enum ClientCombatDamageFlags : std::uint32_t {
  ClientCombatDamageHasSourceProfile       = 0x00000001u,
  ClientCombatDamageHasTargetProfile       = 0x00000002u,
  ClientCombatDamageHasExplicitDamage      = 0x00000004u,
  ClientCombatDamageFatal                  = 0x00000008u,
  ClientCombatDamageCriticalHit            = 0x00000010u,
  ClientCombatDamageSourceMonster          = 0x00000020u,
  ClientCombatDamageSourceHasActiveWeapon  = 0x00000040u,
  ClientCombatDamageTargetMonster          = 0x00000080u,
  ClientCombatDamageTargetHasActiveWeapon  = 0x00000100u,
  ClientCombatDamageHasMeleeRoll           = 0x00000200u,
  ClientCombatDamageHasRangedRoll          = 0x00000400u,
  ClientCombatDamageProjectileSpell        = 0x00000800u,
  ClientCombatDamageProjectileCriticalHit  = 0x00001000u,
  ClientCombatDamageHasFallInput           = 0x00002000u,
  ClientCombatDamageHasTargetPosition      = 0x00004000u,
};

enum ClientMovementFlags : std::uint32_t {
  ClientMovementHasFromTransform      = 0x00000001u,
  ClientMovementHasToTransform        = 0x00000002u,
  ClientMovementHasStats              = 0x00000004u,
  ClientMovementFromInAir             = 0x00000008u,
  ClientMovementFromFalling           = 0x00000010u,
  ClientMovementFromFallingDeep       = 0x00000020u,
  ClientMovementFromSlide             = 0x00000040u,
  ClientMovementFromJump              = 0x00000080u,
  ClientMovementFromJumpUp            = 0x00000100u,
  ClientMovementFromSwim              = 0x00000200u,
  ClientMovementFromDive              = 0x00000400u,
  ClientMovementFromInWater           = 0x00000800u,
  ClientMovementToInAir               = 0x00001000u,
  ClientMovementToFalling             = 0x00002000u,
  ClientMovementToFallingDeep         = 0x00004000u,
  ClientMovementToSlide               = 0x00008000u,
  ClientMovementToJump                = 0x00010000u,
  ClientMovementToJumpUp              = 0x00020000u,
  ClientMovementToSwim                = 0x00040000u,
  ClientMovementToDive                = 0x00080000u,
  ClientMovementToInWater             = 0x00100000u,
};

enum ClientInventoryFlags : std::uint32_t {
  ClientInventoryMovedWholeInstance = 0x00000001u,
  ClientInventorySourceDead         = 0x00000002u,
  ClientInventorySourceUnconscious  = 0x00000004u,
  ClientInventoryContainer          = 0x00000008u,
  ClientInventoryHasActorPosition   = 0x00000010u,
  ClientInventoryHasItemPosition    = 0x00000020u,
  ClientInventoryHasSourcePosition  = 0x00000040u,
  ClientInventoryHasEquipmentSlot   = 0x00000080u,
  ClientInventoryHasTradePrice      = 0x00000100u,
  ClientInventoryHasWallet          = 0x00000200u,
};

enum ClientWorldStateFlags : std::uint32_t {
  ClientWorldStateHasActorPosition   = 0x00000001u,
  ClientWorldStateHasTargetPosition  = 0x00000002u,
  ClientWorldStateHasSourcePosition  = 0x00000004u,
  ClientWorldStateKnown              = 0x00000008u,
  ClientWorldStateRemoved            = 0x00000010u,
  ClientWorldStateLockedBefore       = 0x00000020u,
  ClientWorldStateLockedAfter        = 0x00000040u,
  ClientWorldStateCrackedBefore      = 0x00000080u,
  ClientWorldStateCrackedAfter       = 0x00000100u,
  ClientWorldStateContainer          = 0x00000200u,
  ClientWorldStateDoor               = 0x00000400u,
  ClientWorldStateLadder             = 0x00000800u,
  ClientWorldStateReady              = 0x00001000u,
};

enum ClientNpcStateFlags : std::uint32_t {
  ClientNpcStateDead                 = 0x00000001u,
  ClientNpcStateUnconscious          = 0x00000002u,
  ClientNpcStateDown                 = 0x00000004u,
  ClientNpcStateAttackAnim           = 0x00000008u,
  ClientNpcStatePrehit               = 0x00000010u,
  ClientNpcStateActorRunning         = 0x00000020u,
  ClientNpcStateOpponentRunning      = 0x00000040u,
  ClientNpcStateOpponentPrehit       = 0x00000080u,
  ClientNpcStateHasPosition          = 0x00000100u,
  ClientNpcStateHasTargetPosition    = 0x00000200u,
  ClientNpcStateHasAttackerCenter    = 0x00000400u,
  ClientNpcStateHasOpponentCenter    = 0x00000800u,
  ClientNpcStateHasFightDistance     = 0x00001000u,
};

enum ClientEconomyFlags : std::uint32_t {
  ClientEconomyHasWalletBefore = 0x00000001u,
  ClientEconomyHasWalletAfter  = 0x00000002u,
  ClientEconomyHasActorPosition = 0x00000004u,
};

enum ClientSessionControlFlags : std::uint32_t {
  ClientSessionControlServerBoundClientMode    = 0x00000001u,
  ClientSessionControlNativeSavePresent        = 0x00000002u,
  ClientSessionControlDbSaveSnapshotRequested  = 0x00000004u,
};

enum ClientDialogStateFlags : std::uint32_t {
  ClientDialogStateKnown          = 0x00000001u,
  ClientDialogStatePlayerLine     = 0x00000002u,
  ClientDialogStateNpcLine        = 0x00000004u,
  ClientDialogStateImportant      = 0x00000008u,
  ClientDialogStateAmbient        = 0x00000010u,
  ClientDialogStateHasLineIndex   = 0x00000020u,
  ClientDialogStateHasOutputIndex = 0x00000040u,
};

enum ClientCharacterEventFlags : std::uint32_t {
  ClientCharacterEventHasActorPosition    = 0x00000001u,
  ClientCharacterEventHasRequestedDelta   = 0x00000002u,
  ClientCharacterEventHasRequestedAmount  = 0x00000004u,
  ClientCharacterEventHasManaAmount       = 0x00000008u,
  ClientCharacterEventHasExperienceReward = 0x00000010u,
  ClientCharacterEventHasLearningReward   = 0x00000020u,
  ClientCharacterEventHasExplicitResource = 0x00000040u,
  ClientCharacterEventServerMustCalculate = 0x80000000u,
};

enum class DecodeError : std::uint8_t {
  None,
  TooSmall,
  BadMagic,
  BadVersion,
  BadPacketKind,
  BadActionKind,
  Truncated,
  StringTooLong,
  InvalidPayload,
};

struct ClientActionPacket final {
  SemanticActionKind kind = SemanticActionKind::ClientBootstrapRequest;
  std::uint16_t      flags = 0;
  std::uint64_t      packetSequence = 0;
  std::uint64_t      clientTick = 0;
  std::uint64_t      localSequence = 0;
  std::string        sessionKey;
  std::string        targetKey;
  std::string        idempotencyKey;
  std::string        payloadJson;
};

struct ClientCombatProfile final {
  std::int32_t strength = 0;
  std::int32_t dexterity = 0;
  std::int32_t damageTypeMask = 0;
  std::int32_t meleeTalentChance = 0;
  std::array<std::int32_t, CombatDamageTypeCount> damage {};
  std::array<std::int32_t, CombatDamageTypeCount> protection {};
};

struct ClientCombatDamagePacket final {
  SemanticActionKind           kind = SemanticActionKind::ApplyWorldEntityDamage;
  std::uint32_t                flags = 0;
  std::uint64_t                packetSequence = 0;
  std::uint64_t                clientTick = 0;
  std::uint64_t                localSequence = 0;
  std::string                  sessionKey;
  std::string                  targetKey;
  std::string                  idempotencyKey;
  std::string                  sourceActorKey;
  std::string                  sourceActorEntityKey;
  std::string                  targetCharacterKey;
  std::string                  targetEntityKey;
  std::string                  world;
  std::string                  reason;
  ClientCombatDamageKind       damageKind = ClientCombatDamageKind::Unknown;
  ClientCombatDamageModifier   modifier = ClientCombatDamageModifier::Normal;
  std::int32_t                 gothicGame = 2;
  std::int32_t                 damageAmount = 0;
  std::int32_t                 valueBefore = 0;
  std::int32_t                 valueAfter = 0;
  std::int32_t                 requestedDelta = 0;
  std::int32_t                 criticalDamageMultiplier = 0;
  std::int32_t                 meleeTalentChance = 0;
  std::int32_t                 meleeRandomRoll = 0;
  double                       projectileDistance = 0.0;
  double                       projectileWeaponChance = 0.0;
  double                       projectileRandomHitRoll = 0.0;
  double                       fallSpeed = 0.0;
  double                       fallGravity = 0.000981;
  std::int32_t                 fallHeightThreshold = 0;
  std::int32_t                 fallDamagePerMeter = 0;
  double                       targetPosX = 0.0;
  double                       targetPosY = 0.0;
  double                       targetPosZ = 0.0;
  ClientCombatProfile          source;
  ClientCombatProfile          target;
  std::array<std::int32_t, CombatDamageTypeCount> explicitDamage {};
};

struct ClientMovementStats final {
  std::int32_t level = 0;
  std::int32_t experience = 0;
  std::int32_t experienceNext = 0;
  std::int32_t learningPoints = 0;
  std::int32_t healthCurrent = 0;
  std::int32_t healthMax = 0;
  std::int32_t manaCurrent = 0;
  std::int32_t manaMax = 0;
  std::int32_t strength = 0;
  std::int32_t dexterity = 0;
  std::int32_t guild = 0;
  std::int32_t trueGuild = 0;
  std::int32_t permanentAttitude = 0;
  std::int32_t temporaryAttitude = 0;
};

struct ClientMovementPacket final {
  SemanticActionKind kind = SemanticActionKind::MovementProposal;
  std::uint32_t      flags = 0;
  std::uint64_t      packetSequence = 0;
  std::uint64_t      clientTick = 0;
  std::uint64_t      localSequence = 0;
  std::uint64_t      fromTick = 0;
  std::uint64_t      toTick = 0;
  std::uint64_t      deltaMs = 0;
  double             fromX = 0.0;
  double             fromY = 0.0;
  double             fromZ = 0.0;
  double             toX = 0.0;
  double             toY = 0.0;
  double             toZ = 0.0;
  double             fromYaw = 0.0;
  double             toYaw = 0.0;
  std::uint64_t      cadenceIntervalMs = 0;
  double             cadenceMinDistance = 0.0;
  double             cadenceMinYawDeg = 0.0;
  std::uint64_t      checkpointForceIntervalMs = 0;
  ClientMovementStats stats;
  std::string        sessionKey;
  std::string        targetKey;
  std::string        idempotencyKey;
  std::string        source;
  std::string        actorKey;
  std::string        characterKey;
  std::string        world;
  std::string        waypointKey;
  std::string        reason;
};

struct ClientInventoryPacket final {
  SemanticActionKind kind = SemanticActionKind::PickupWorldItem;
  std::uint32_t      flags = 0;
  std::uint64_t      packetSequence = 0;
  std::uint64_t      clientTick = 0;
  std::uint64_t      localSequence = 0;
  std::int64_t       itemSymbol = -1;
  std::int64_t       inventoryItemSymbol = -1;
  std::int64_t       itemPersistentId = -1;
  std::int64_t       sourceItemPersistentId = -1;
  std::int64_t       sourceWorldItemPersistentId = -1;
  std::int64_t       worldItemPersistentId = -1;
  std::int64_t       vendorItemPersistentId = -1;
  std::int64_t       sellerItemPersistentId = -1;
  std::int64_t       amount = 1;
  std::int64_t       slot = 0;
  std::int64_t       bagIndex = -1;
  std::int64_t       targetBagIndex = -1;
  std::int64_t       unitPrice = 0;
  std::int64_t       priceTotal = 0;
  std::int64_t       walletBefore = 0;
  std::int64_t       walletAfter = 0;
  std::uint64_t      slotId = 0;
  std::uint64_t      vobId = 0;
  double             actorPosX = 0.0;
  double             actorPosY = 0.0;
  double             actorPosZ = 0.0;
  double             itemPosX = 0.0;
  double             itemPosY = 0.0;
  double             itemPosZ = 0.0;
  double             sourcePosX = 0.0;
  double             sourcePosY = 0.0;
  double             sourcePosZ = 0.0;
  std::string        sessionKey;
  std::string        targetKey;
  std::string        idempotencyKey;
  std::string        source;
  std::string        actorKey;
  std::string        sourceActorKey;
  std::string        targetCharacterKey;
  std::string        itemTemplateKey;
  std::string        itemInstanceId;
  std::string        itemInstanceUuid;
  std::string        equipmentSlot;
  std::string        sourceEntityKey;
  std::string        sourceContainerKey;
  std::string        containerKey;
  std::string        sourceNpcKey;
  std::string        targetNpcEntityKey;
  std::string        npcKey;
  std::string        world;
  std::string        tag;
  std::string        focusName;
  std::string        displayName;
  std::string        scheme;
  std::string        reason;
  std::string        currencyKey;
};

struct ClientWorldStatePacket final {
  SemanticActionKind kind = SemanticActionKind::UseInteractive;
  std::uint32_t      flags = 0;
  std::uint64_t      packetSequence = 0;
  std::uint64_t      clientTick = 0;
  std::uint64_t      localSequence = 0;
  std::int64_t       valueBefore = 0;
  std::int64_t       valueAfter = 0;
  std::int64_t       valueDelta = 0;
  std::int64_t       secondaryBefore = 0;
  std::int64_t       secondaryAfter = 0;
  std::int64_t       secondaryDelta = 0;
  std::int64_t       tertiaryBefore = 0;
  std::int64_t       tertiaryAfter = 0;
  std::int64_t       tertiaryDelta = 0;
  std::int64_t       symbolIndex = -1;
  std::int64_t       valueIndex = -1;
  std::int64_t       scriptFunctionSymbol = -1;
  std::int64_t       npcSymbol = -1;
  std::int64_t       infoSymbol = -1;
  std::int64_t       entryCount = 0;
  std::int64_t       durationMs = 0;
  std::int64_t       worldTimeBeforeMs = 0;
  std::int64_t       worldTimeAfterMs = 0;
  std::int64_t       worldDayBefore = 0;
  std::int64_t       worldDayAfter = 0;
  std::int64_t       worldHourBefore = 0;
  std::int64_t       worldHourAfter = 0;
  std::int64_t       worldMinuteBefore = 0;
  std::int64_t       worldMinuteAfter = 0;
  std::int64_t       eventType = 0;
  std::int64_t       stateBefore = 0;
  std::int64_t       stateAfter = 0;
  std::int64_t       frame = 0;
  std::int64_t       targetFrame = 0;
  std::int64_t       stateCount = 0;
  std::int64_t       stateMask = 0;
  std::int64_t       slotId = 0;
  std::int64_t       vobId = 0;
  double             actorPosX = 0.0;
  double             actorPosY = 0.0;
  double             actorPosZ = 0.0;
  double             targetPosX = 0.0;
  double             targetPosY = 0.0;
  double             targetPosZ = 0.0;
  double             sourcePosX = 0.0;
  double             sourcePosY = 0.0;
  double             sourcePosZ = 0.0;
  std::string        sessionKey;
  std::string        targetKey;
  std::string        idempotencyKey;
  std::string        source;
  std::string        actorKey;
  std::string        characterKey;
  std::string        world;
  std::string        reason;
  std::string        resourceKey;
  std::string        interactiveKey;
  std::string        entityKey;
  std::string        tag;
  std::string        focusName;
  std::string        displayName;
  std::string        scheme;
  std::string        stateBeforeName;
  std::string        stateAfterName;
  std::string        eventTypeName;
  std::string        eventTarget;
  std::string        eventEmitter;
  std::string        triggerName;
  std::string        triggerTargetName;
  std::string        scriptKey;
  std::string        globalKey;
  std::string        symbolName;
  std::string        scriptFunctionName;
  std::string        questKey;
  std::string        questName;
  std::string        status;
  std::string        npcKey;
  std::string        npcSymbolName;
  std::string        infoKey;
  std::string        infoSymbolName;
  std::string        conversationKey;
  std::string        syncGroup;
  std::string        speakerKey;
  std::string        listenerKey;
  std::string        outputName;
  std::string        messageName;
  std::string        subtitleText;
};

struct ClientNpcStatePacket final {
  SemanticActionKind kind = SemanticActionKind::RecordNpcRoutineState;
  std::uint32_t      flags = 0;
  std::uint64_t      packetSequence = 0;
  std::uint64_t      clientTick = 0;
  std::uint64_t      localSequence = 0;
  std::int64_t       npcPersistentId = -1;
  std::int64_t       npcSymbol = -1;
  std::int64_t       targetNpcPersistentId = -1;
  std::int64_t       targetNpcSymbol = -1;
  std::int64_t       sourceNpcPersistentId = -1;
  std::int64_t       sourceNpcSymbol = -1;
  std::int64_t       healthCurrent = 0;
  std::int64_t       healthMax = 0;
  std::int64_t       aiStateFunction = 0;
  std::int64_t       remainingPathPoints = 0;
  std::int64_t       comboIndex = 0;
  std::int64_t       bodyState = 0;
  std::int64_t       weaponStateId = 0;
  std::int64_t       animationElapsedMs = 0;
  std::int64_t       attackAnimationElapsedMs = 0;
  std::int64_t       animationTotalMs = 0;
  std::int64_t       attackTotalMs = 0;
  std::int64_t       attackOptimalMs = 0;
  std::int64_t       attackHitEndMs = 0;
  std::int64_t       parryWindowStartMs = 0;
  std::int64_t       parryWindowEndMs = 0;
  std::int64_t       comboWindowStartMs = 0;
  std::int64_t       comboWindowEndMs = 0;
  double             posX = 0.0;
  double             posY = 0.0;
  double             posZ = 0.0;
  double             targetPosX = 0.0;
  double             targetPosY = 0.0;
  double             targetPosZ = 0.0;
  double             attackerCenterX = 0.0;
  double             attackerCenterY = 0.0;
  double             attackerCenterZ = 0.0;
  double             opponentCenterX = 0.0;
  double             opponentCenterY = 0.0;
  double             opponentCenterZ = 0.0;
  double             fightDistanceX = 0.0;
  double             fightDistanceY = 0.0;
  double             fightDistanceZ = 0.0;
  double             attackerYawRad = 0.0;
  double             opponentYawRad = 0.0;
  double             weaponRange = 0.0;
  double             attackRange = 0.0;
  double             opponentAttackRange = 0.0;
  double             attackerFightRangeBase = 0.0;
  double             opponentFightRangeBase = 0.0;
  std::string        sessionKey;
  std::string        targetKey;
  std::string        idempotencyKey;
  std::string        source;
  std::string        reason;
  std::string        actorKey;
  std::string        npcEntityKey;
  std::string        npcKey;
  std::string        targetNpcEntityKey;
  std::string        targetNpcKey;
  std::string        sourceNpcEntityKey;
  std::string        sourceActorKey;
  std::string        displayName;
  std::string        targetDisplayName;
  std::string        world;
  std::string        routineState;
  std::string        scheduleKey;
  std::string        currentWaypointKey;
  std::string        currentWaypointName;
  std::string        currentWaypointLegacy;
  std::string        targetWaypointKey;
  std::string        targetWaypointName;
  std::string        targetWaypointLegacy;
  std::string        nextWaypointKey;
  std::string        nextWaypointName;
  std::string        nextWaypointLegacy;
  std::string        aiState;
  std::string        aiIntent;
  std::string        aiTargetKey;
  std::string        perceptionState;
  std::string        actionKey;
  std::string        actionName;
  std::string        actionState;
  std::string        actionTargetKey;
  std::string        syncGroup;
  std::string        pathState;
  std::string        routeKey;
  std::string        moveHint;
  std::string        opponentKey;
  std::string        fightState;
  std::string        attackState;
  std::string        combatAction;
  std::string        intentState;
  std::string        weaponState;
  std::string        animationName;
  std::string        attackAnimationName;
};

struct ClientEconomyPacket final {
  SemanticActionKind kind = SemanticActionKind::WalletDelta;
  std::uint32_t      flags = 0;
  std::uint64_t      packetSequence = 0;
  std::uint64_t      clientTick = 0;
  std::uint64_t      localSequence = 0;
  std::int64_t       amount = 0;
  std::int64_t       deltaAmount = 0;
  std::int64_t       walletBefore = 0;
  std::int64_t       walletAfter = 0;
  std::int64_t       itemTemplateSymbol = -1;
  double             actorPosX = 0.0;
  double             actorPosY = 0.0;
  double             actorPosZ = 0.0;
  std::string        sessionKey;
  std::string        targetKey;
  std::string        idempotencyKey;
  std::string        source;
  std::string        actorKey;
  std::string        characterKey;
  std::string        currencyKey;
  std::string        currencyDisplayName;
  std::string        world;
  std::string        reason;
};

struct ClientSessionControlPacket final {
  SemanticActionKind kind = SemanticActionKind::ClientBootstrapRequest;
  std::uint32_t      flags = 0;
  std::uint64_t      packetSequence = 0;
  std::uint64_t      clientTick = 0;
  std::uint64_t      localSequence = 0;
  std::int64_t       serverTick = 0;
  std::int64_t       acknowledgedLocalSequence = 0;
  std::string        sessionKey;
  std::string        targetKey;
  std::string        idempotencyKey;
  std::string        source;
  std::string        sourceLocation;
  std::string        actorKey;
  std::string        characterKey;
  std::string        displayName;
  std::string        world;
  std::string        serverEndpoint;
  std::string        clientContentManifestHash;
  std::string        reason;
  std::string        actionKind;
  std::string        manifestKey;
  std::string        checkpointKind;
  std::string        saveSlotKey;
  std::string        slotPath;
  std::string        nativeSavePath;
  std::string        slotDisplayName;
  std::string        clientWorldName;
};

struct ClientDialogStatePacket final {
  SemanticActionKind kind = SemanticActionKind::SetKnownDialog;
  std::uint32_t      flags = 0;
  std::uint64_t      packetSequence = 0;
  std::uint64_t      clientTick = 0;
  std::uint64_t      localSequence = 0;
  std::int64_t       npcSymbol = -1;
  std::int64_t       infoSymbol = -1;
  std::int64_t       lineIndex = -1;
  std::int64_t       outputIndex = -1;
  std::int64_t       durationMs = 0;
  std::string        sessionKey;
  std::string        targetKey;
  std::string        idempotencyKey;
  std::string        source;
  std::string        actorKey;
  std::string        characterKey;
  std::string        world;
  std::string        reason;
  std::string        npcKey;
  std::string        npcSymbolName;
  std::string        infoKey;
  std::string        infoSymbolName;
  std::string        conversationKey;
  std::string        syncGroup;
  std::string        speakerKey;
  std::string        listenerKey;
  std::string        outputName;
  std::string        messageName;
  std::string        subtitleText;
  std::string        dialogState;
  std::string        topicKey;
};

struct ClientCharacterEventPacket final {
  SemanticActionKind kind = SemanticActionKind::AdjustProgression;
  std::uint32_t      flags = 0;
  std::uint64_t      packetSequence = 0;
  std::uint64_t      clientTick = 0;
  std::uint64_t      localSequence = 0;
  std::int64_t       requestedDelta = 0;
  std::int64_t       requestedAmount = 0;
  std::int64_t       manaAmount = 0;
  std::int64_t       experienceReward = 0;
  std::int64_t       learningPointsReward = 0;
  std::int64_t       attributeSymbol = -1;
  std::int64_t       skillSymbol = -1;
  double             actorPosX = 0.0;
  double             actorPosY = 0.0;
  double             actorPosZ = 0.0;
  std::string        sessionKey;
  std::string        targetKey;
  std::string        idempotencyKey;
  std::string        source;
  std::string        actorKey;
  std::string        characterKey;
  std::string        world;
  std::string        reason;
  std::string        resourceKey;
  std::string        resourceDisplayName;
  std::string        progressionKey;
  std::string        rewardKey;
  std::string        sourceActorKey;
  std::string        sourceEntityKey;
};

struct ServerAckPacket final {
  std::uint64_t packetSequence = 0;
  std::uint64_t localSequence = 0;
  ServerAckKind kind = ServerAckKind::GenericAction;
  bool          accepted = false;
  bool          ready = false;
};

struct ServerSnapshotChunkPacket final {
  std::uint64_t packetSequence = 0;
  std::uint64_t localSequence = 0;
  std::uint32_t snapshotId = 0;
  std::uint16_t chunkIndex = 0;
  std::uint16_t chunkCount = 0;
  std::uint32_t totalBytes = 0;
  std::string   payloadJsonFragment;
};

struct ServerDiagnosticPacket final {
  std::uint64_t packetSequence = 0;
  std::uint64_t localSequence = 0;
  std::uint16_t severity = 0;
  std::string   actionKind;
  std::string   reason;
  std::string   message;
};

struct ServerLiveDeltaPacket final {
  std::uint64_t packetSequence = 0;
  std::uint64_t localSequence = 0;
  std::uint64_t serverTick = 0;
  ServerLiveDeltaKind kind = ServerLiveDeltaKind::Generic;
  std::uint32_t flags = 0;
  std::string   actionKind;
  double        posX = 0.0;
  double        posY = 0.0;
  double        posZ = 0.0;
  double        yaw = 0.0;
  std::int32_t  level = 0;
  std::int32_t  experience = 0;
  std::int32_t  experienceNext = 0;
  std::int32_t  learningPoints = 0;
  std::int32_t  healthCurrent = 0;
  std::int32_t  healthMax = 0;
  std::int32_t  manaCurrent = 0;
  std::int32_t  manaMax = 0;
  std::int32_t  strength = 0;
  std::int32_t  dexterity = 0;
  std::int32_t  guild = 0;
  std::int32_t  trueGuild = 0;
  std::string   debugJson;
};

struct DecodeResult final {
  DecodeError        error = DecodeError::None;
  ClientActionPacket clientAction;

  [[nodiscard]] constexpr bool ok() const noexcept {
    return error == DecodeError::None;
  }
};

struct ServerAckDecodeResult final {
  DecodeError     error = DecodeError::None;
  ServerAckPacket serverAck;

  [[nodiscard]] constexpr bool ok() const noexcept {
    return error == DecodeError::None;
  }
};

struct ServerSnapshotChunkDecodeResult final {
  DecodeError               error = DecodeError::None;
  ServerSnapshotChunkPacket snapshotChunk;

  [[nodiscard]] constexpr bool ok() const noexcept {
    return error == DecodeError::None;
  }
};

struct ServerDiagnosticDecodeResult final {
  DecodeError            error = DecodeError::None;
  ServerDiagnosticPacket diagnostic;

  [[nodiscard]] constexpr bool ok() const noexcept {
    return error == DecodeError::None;
  }
};

struct ClientCombatDamageDecodeResult final {
  DecodeError              error = DecodeError::None;
  ClientCombatDamagePacket combatDamage;

  [[nodiscard]] constexpr bool ok() const noexcept {
    return error == DecodeError::None;
  }
};

struct ClientMovementDecodeResult final {
  DecodeError          error = DecodeError::None;
  ClientMovementPacket movement;

  [[nodiscard]] constexpr bool ok() const noexcept {
    return error == DecodeError::None;
  }
};

struct ClientInventoryDecodeResult final {
  DecodeError           error = DecodeError::None;
  ClientInventoryPacket inventory;

  [[nodiscard]] constexpr bool ok() const noexcept {
    return error == DecodeError::None;
  }
};

struct ClientWorldStateDecodeResult final {
  DecodeError            error = DecodeError::None;
  ClientWorldStatePacket worldState;

  [[nodiscard]] constexpr bool ok() const noexcept {
    return error == DecodeError::None;
  }
};

struct ClientNpcStateDecodeResult final {
  DecodeError          error = DecodeError::None;
  ClientNpcStatePacket npcState;

  [[nodiscard]] constexpr bool ok() const noexcept {
    return error == DecodeError::None;
  }
};

struct ClientEconomyDecodeResult final {
  DecodeError         error = DecodeError::None;
  ClientEconomyPacket economy;

  [[nodiscard]] constexpr bool ok() const noexcept {
    return error == DecodeError::None;
  }
};

struct ClientSessionControlDecodeResult final {
  DecodeError                error = DecodeError::None;
  ClientSessionControlPacket sessionControl;

  [[nodiscard]] constexpr bool ok() const noexcept {
    return error == DecodeError::None;
  }
};

struct ClientDialogStateDecodeResult final {
  DecodeError             error = DecodeError::None;
  ClientDialogStatePacket dialogState;

  [[nodiscard]] constexpr bool ok() const noexcept {
    return error == DecodeError::None;
  }
};

struct ClientCharacterEventDecodeResult final {
  DecodeError                error = DecodeError::None;
  ClientCharacterEventPacket characterEvent;

  [[nodiscard]] constexpr bool ok() const noexcept {
    return error == DecodeError::None;
  }
};

struct ServerLiveDeltaDecodeResult final {
  DecodeError           error = DecodeError::None;
  ServerLiveDeltaPacket liveDelta;

  [[nodiscard]] constexpr bool ok() const noexcept {
    return error == DecodeError::None;
  }
};

inline constexpr std::uint32_t PacketMagic = 0x4d4d474f; // "OGMM" little endian.
inline constexpr std::uint16_t PacketVersion = 1;
inline constexpr std::size_t MaxStringBytes = 8192;
inline constexpr std::size_t MaxPayloadBytes = 48 * 1024;
inline constexpr std::size_t MaxDatagramBytes = 60 * 1024;

inline void appendU16(std::vector<std::uint8_t>& out, std::uint16_t v) {
  out.push_back(static_cast<std::uint8_t>(v & 0xffu));
  out.push_back(static_cast<std::uint8_t>((v >> 8u) & 0xffu));
}

inline void appendU32(std::vector<std::uint8_t>& out, std::uint32_t v) {
  for(unsigned i = 0; i != 4; ++i)
    out.push_back(static_cast<std::uint8_t>((v >> (i * 8u)) & 0xffu));
}

inline void appendU64(std::vector<std::uint8_t>& out, std::uint64_t v) {
  for(unsigned i = 0; i != 8; ++i)
    out.push_back(static_cast<std::uint8_t>((v >> (i * 8u)) & 0xffu));
}

inline void appendI64(std::vector<std::uint8_t>& out, std::int64_t v) {
  appendU64(out, static_cast<std::uint64_t>(v));
}

inline void appendI32(std::vector<std::uint8_t>& out, std::int32_t v) {
  appendU32(out, static_cast<std::uint32_t>(v));
}

inline void appendF64(std::vector<std::uint8_t>& out, double v) {
  std::uint64_t bits = 0;
  std::memcpy(&bits, &v, sizeof(bits));
  appendU64(out, bits);
}

inline bool readU16(std::string_view bytes, std::size_t& at, std::uint16_t& out) noexcept {
  if(bytes.size() - at < 2)
    return false;
  out = static_cast<std::uint16_t>(static_cast<unsigned char>(bytes[at])) |
        static_cast<std::uint16_t>(static_cast<unsigned char>(bytes[at + 1]) << 8u);
  at += 2;
  return true;
}

inline bool readU32(std::string_view bytes, std::size_t& at, std::uint32_t& out) noexcept {
  if(bytes.size() - at < 4)
    return false;
  out = 0;
  for(unsigned i = 0; i != 4; ++i)
    out |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at + i])) << (i * 8u);
  at += 4;
  return true;
}

inline bool readU64(std::string_view bytes, std::size_t& at, std::uint64_t& out) noexcept {
  if(bytes.size() - at < 8)
    return false;
  out = 0;
  for(unsigned i = 0; i != 8; ++i)
    out |= static_cast<std::uint64_t>(static_cast<unsigned char>(bytes[at + i])) << (i * 8u);
  at += 8;
  return true;
}

inline bool readI64(std::string_view bytes, std::size_t& at, std::int64_t& out) noexcept {
  std::uint64_t value = 0;
  if(!readU64(bytes, at, value))
    return false;
  out = static_cast<std::int64_t>(value);
  return true;
}

inline bool readI32(std::string_view bytes, std::size_t& at, std::int32_t& out) noexcept {
  std::uint32_t value = 0;
  if(!readU32(bytes, at, value))
    return false;
  out = static_cast<std::int32_t>(value);
  return true;
}

inline bool readF64(std::string_view bytes, std::size_t& at, double& out) noexcept {
  std::uint64_t bits = 0;
  if(!readU64(bytes, at, bits))
    return false;
  std::memcpy(&out, &bits, sizeof(out));
  return true;
}

inline bool appendString16(std::vector<std::uint8_t>& out, std::string_view text) {
  if(text.size() > std::numeric_limits<std::uint16_t>::max())
    return false;
  appendU16(out, static_cast<std::uint16_t>(text.size()));
  out.insert(out.end(), text.begin(), text.end());
  return true;
}

inline bool appendString32(std::vector<std::uint8_t>& out, std::string_view text) {
  if(text.size() > std::numeric_limits<std::uint32_t>::max())
    return false;
  appendU32(out, static_cast<std::uint32_t>(text.size()));
  out.insert(out.end(), text.begin(), text.end());
  return true;
}

inline bool readString16(std::string_view bytes, std::size_t& at, std::string& out) {
  std::uint16_t len = 0;
  if(!readU16(bytes, at, len))
    return false;
  if(len > MaxStringBytes || bytes.size() - at < len)
    return false;
  out.assign(bytes.data() + at, len);
  at += len;
  return true;
}

inline bool readString32(std::string_view bytes, std::size_t& at, std::string& out) {
  std::uint32_t len = 0;
  if(!readU32(bytes, at, len))
    return false;
  if(len > MaxPayloadBytes || bytes.size() - at < len)
    return false;
  out.assign(bytes.data() + at, len);
  at += len;
  return true;
}

inline std::vector<std::uint8_t> encodeClientActionPacket(const SemanticActionEnvelope& envelope,
                                                         std::string_view sessionKey) {
  std::vector<std::uint8_t> out;
  out.reserve(envelope.payloadJson.size() + envelope.targetKey.size() + envelope.idempotencyKey.size() + sessionKey.size() + 64);

  appendU32(out, PacketMagic);
  appendU16(out, PacketVersion);
  appendU16(out, static_cast<std::uint16_t>(PacketKind::ClientAction));
  appendU16(out, 0);
  appendU16(out, static_cast<std::uint16_t>(envelope.kind));
  appendU64(out, envelope.localSequence);
  appendU64(out, envelope.clientTick);
  appendU64(out, envelope.localSequence);

  if(!appendString16(out, sessionKey) ||
     !appendString16(out, envelope.targetKey) ||
     !appendString16(out, envelope.idempotencyKey) ||
     !appendString32(out, envelope.payloadJson)) {
    return {};
  }

  if(out.size() > MaxDatagramBytes)
    return {};
  return out;
}

inline DecodeResult decodeClientActionPacket(std::string_view bytes) {
  DecodeResult result;
  if(bytes.size() < 4 + 2 + 2 + 2 + 2 + 8 + 8 + 8) {
    result.error = DecodeError::TooSmall;
    return result;
  }

  std::size_t at = 0;
  std::uint32_t magic = 0;
  std::uint16_t version = 0;
  std::uint16_t packetKind = 0;
  std::uint16_t actionKind = 0;

  if(!readU32(bytes, at, magic) || magic != PacketMagic) {
    result.error = DecodeError::BadMagic;
    return result;
  }
  if(!readU16(bytes, at, version) || version != PacketVersion) {
    result.error = DecodeError::BadVersion;
    return result;
  }
  if(!readU16(bytes, at, packetKind) || packetKind != static_cast<std::uint16_t>(PacketKind::ClientAction)) {
    result.error = DecodeError::BadPacketKind;
    return result;
  }

  if(!readU16(bytes, at, result.clientAction.flags) ||
     !readU16(bytes, at, actionKind) ||
     !readU64(bytes, at, result.clientAction.packetSequence) ||
     !readU64(bytes, at, result.clientAction.clientTick) ||
     !readU64(bytes, at, result.clientAction.localSequence)) {
    result.error = DecodeError::Truncated;
    return result;
  }

  result.clientAction.kind = static_cast<SemanticActionKind>(actionKind);
  if(findSemanticAction(result.clientAction.kind) == nullptr) {
    result.error = DecodeError::BadActionKind;
    return result;
  }

  if(!readString16(bytes, at, result.clientAction.sessionKey) ||
     !readString16(bytes, at, result.clientAction.targetKey) ||
     !readString16(bytes, at, result.clientAction.idempotencyKey) ||
     !readString32(bytes, at, result.clientAction.payloadJson)) {
    result.error = DecodeError::Truncated;
    return result;
  }

  if(result.clientAction.payloadJson.empty() || result.clientAction.payloadJson.front() != '{') {
    result.error = DecodeError::InvalidPayload;
    return result;
  }
  return result;
}

inline void appendCombatProfile(std::vector<std::uint8_t>& out, const ClientCombatProfile& profile) {
  appendI32(out, profile.strength);
  appendI32(out, profile.dexterity);
  appendI32(out, profile.damageTypeMask);
  appendI32(out, profile.meleeTalentChance);
  for(const auto value : profile.damage)
    appendI32(out, value);
  for(const auto value : profile.protection)
    appendI32(out, value);
}

inline bool readCombatProfile(std::string_view bytes, std::size_t& at, ClientCombatProfile& out) noexcept {
  if(!readI32(bytes, at, out.strength) ||
     !readI32(bytes, at, out.dexterity) ||
     !readI32(bytes, at, out.damageTypeMask) ||
     !readI32(bytes, at, out.meleeTalentChance)) {
    return false;
  }
  for(auto& value : out.damage) {
    if(!readI32(bytes, at, value))
      return false;
  }
  for(auto& value : out.protection) {
    if(!readI32(bytes, at, value))
      return false;
  }
  return true;
}

inline std::vector<std::uint8_t> encodeClientCombatDamagePacket(const ClientCombatDamagePacket& packet) {
  std::vector<std::uint8_t> out;
  out.reserve(packet.sessionKey.size() + packet.targetKey.size() + packet.idempotencyKey.size() +
              packet.sourceActorKey.size() + packet.sourceActorEntityKey.size() +
              packet.targetCharacterKey.size() + packet.targetEntityKey.size() +
              packet.world.size() + packet.reason.size() + 360);

  appendU32(out, PacketMagic);
  appendU16(out, PacketVersion);
  appendU16(out, static_cast<std::uint16_t>(PacketKind::ClientCombatDamage));
  appendU16(out, 0);
  appendU16(out, static_cast<std::uint16_t>(packet.kind));
  appendU64(out, packet.packetSequence);
  appendU64(out, packet.clientTick);
  appendU64(out, packet.localSequence);
  appendU32(out, packet.flags);
  appendU16(out, static_cast<std::uint16_t>(packet.damageKind));
  appendU16(out, static_cast<std::uint16_t>(packet.modifier));
  appendI32(out, packet.gothicGame);
  appendI32(out, packet.damageAmount);
  appendI32(out, packet.valueBefore);
  appendI32(out, packet.valueAfter);
  appendI32(out, packet.requestedDelta);
  appendI32(out, packet.criticalDamageMultiplier);
  appendI32(out, packet.meleeTalentChance);
  appendI32(out, packet.meleeRandomRoll);
  appendF64(out, packet.projectileDistance);
  appendF64(out, packet.projectileWeaponChance);
  appendF64(out, packet.projectileRandomHitRoll);
  appendF64(out, packet.fallSpeed);
  appendF64(out, packet.fallGravity);
  appendI32(out, packet.fallHeightThreshold);
  appendI32(out, packet.fallDamagePerMeter);
  appendF64(out, packet.targetPosX);
  appendF64(out, packet.targetPosY);
  appendF64(out, packet.targetPosZ);
  appendCombatProfile(out, packet.source);
  appendCombatProfile(out, packet.target);
  for(const auto value : packet.explicitDamage)
    appendI32(out, value);

  if(!appendString16(out, packet.sessionKey) ||
     !appendString16(out, packet.targetKey) ||
     !appendString16(out, packet.idempotencyKey) ||
     !appendString16(out, packet.sourceActorKey) ||
     !appendString16(out, packet.sourceActorEntityKey) ||
     !appendString16(out, packet.targetCharacterKey) ||
     !appendString16(out, packet.targetEntityKey) ||
     !appendString16(out, packet.world) ||
     !appendString16(out, packet.reason)) {
    return {};
  }

  if(out.size() > MaxDatagramBytes)
    return {};
  return out;
}

inline ClientCombatDamageDecodeResult decodeClientCombatDamagePacket(std::string_view bytes) {
  ClientCombatDamageDecodeResult result;
  if(bytes.size() < 4 + 2 + 2 + 2 + 2 + 8 + 8 + 8 + 4 + 2 + 2) {
    result.error = DecodeError::TooSmall;
    return result;
  }

  std::size_t at = 0;
  std::uint32_t magic = 0;
  std::uint16_t version = 0;
  std::uint16_t packetKind = 0;
  std::uint16_t flags = 0;
  std::uint16_t actionKind = 0;
  std::uint16_t damageKind = 0;
  std::uint16_t modifier = 0;

  if(!readU32(bytes, at, magic) || magic != PacketMagic) {
    result.error = DecodeError::BadMagic;
    return result;
  }
  if(!readU16(bytes, at, version) || version != PacketVersion) {
    result.error = DecodeError::BadVersion;
    return result;
  }
  if(!readU16(bytes, at, packetKind) || packetKind != static_cast<std::uint16_t>(PacketKind::ClientCombatDamage)) {
    result.error = DecodeError::BadPacketKind;
    return result;
  }
  if(!readU16(bytes, at, flags) ||
     !readU16(bytes, at, actionKind) ||
     !readU64(bytes, at, result.combatDamage.packetSequence) ||
     !readU64(bytes, at, result.combatDamage.clientTick) ||
     !readU64(bytes, at, result.combatDamage.localSequence) ||
     !readU32(bytes, at, result.combatDamage.flags) ||
     !readU16(bytes, at, damageKind) ||
     !readU16(bytes, at, modifier) ||
     !readI32(bytes, at, result.combatDamage.gothicGame) ||
     !readI32(bytes, at, result.combatDamage.damageAmount) ||
     !readI32(bytes, at, result.combatDamage.valueBefore) ||
     !readI32(bytes, at, result.combatDamage.valueAfter) ||
     !readI32(bytes, at, result.combatDamage.requestedDelta) ||
     !readI32(bytes, at, result.combatDamage.criticalDamageMultiplier) ||
     !readI32(bytes, at, result.combatDamage.meleeTalentChance) ||
     !readI32(bytes, at, result.combatDamage.meleeRandomRoll) ||
     !readF64(bytes, at, result.combatDamage.projectileDistance) ||
     !readF64(bytes, at, result.combatDamage.projectileWeaponChance) ||
     !readF64(bytes, at, result.combatDamage.projectileRandomHitRoll) ||
     !readF64(bytes, at, result.combatDamage.fallSpeed) ||
     !readF64(bytes, at, result.combatDamage.fallGravity) ||
     !readI32(bytes, at, result.combatDamage.fallHeightThreshold) ||
     !readI32(bytes, at, result.combatDamage.fallDamagePerMeter) ||
     !readF64(bytes, at, result.combatDamage.targetPosX) ||
     !readF64(bytes, at, result.combatDamage.targetPosY) ||
     !readF64(bytes, at, result.combatDamage.targetPosZ) ||
     !readCombatProfile(bytes, at, result.combatDamage.source) ||
     !readCombatProfile(bytes, at, result.combatDamage.target)) {
    result.error = DecodeError::Truncated;
    return result;
  }
  for(auto& value : result.combatDamage.explicitDamage) {
    if(!readI32(bytes, at, value)) {
      result.error = DecodeError::Truncated;
      return result;
    }
  }
  if(!readString16(bytes, at, result.combatDamage.sessionKey) ||
     !readString16(bytes, at, result.combatDamage.targetKey) ||
     !readString16(bytes, at, result.combatDamage.idempotencyKey) ||
     !readString16(bytes, at, result.combatDamage.sourceActorKey) ||
     !readString16(bytes, at, result.combatDamage.sourceActorEntityKey) ||
     !readString16(bytes, at, result.combatDamage.targetCharacterKey) ||
     !readString16(bytes, at, result.combatDamage.targetEntityKey) ||
     !readString16(bytes, at, result.combatDamage.world) ||
     !readString16(bytes, at, result.combatDamage.reason)) {
    result.error = DecodeError::Truncated;
    return result;
  }

  (void)flags;
  result.combatDamage.kind = static_cast<SemanticActionKind>(actionKind);
  if(result.combatDamage.kind != SemanticActionKind::ApplyCharacterDamage &&
     result.combatDamage.kind != SemanticActionKind::ApplyWorldEntityDamage) {
    result.error = DecodeError::BadActionKind;
    return result;
  }
  result.combatDamage.damageKind = static_cast<ClientCombatDamageKind>(damageKind);
  result.combatDamage.modifier = static_cast<ClientCombatDamageModifier>(modifier);
  return result;
}

inline void appendMovementStats(std::vector<std::uint8_t>& out, const ClientMovementStats& stats) {
  appendI32(out, stats.level);
  appendI32(out, stats.experience);
  appendI32(out, stats.experienceNext);
  appendI32(out, stats.learningPoints);
  appendI32(out, stats.healthCurrent);
  appendI32(out, stats.healthMax);
  appendI32(out, stats.manaCurrent);
  appendI32(out, stats.manaMax);
  appendI32(out, stats.strength);
  appendI32(out, stats.dexterity);
  appendI32(out, stats.guild);
  appendI32(out, stats.trueGuild);
  appendI32(out, stats.permanentAttitude);
  appendI32(out, stats.temporaryAttitude);
}

inline bool readMovementStats(std::string_view bytes, std::size_t& at, ClientMovementStats& out) noexcept {
  return readI32(bytes, at, out.level) &&
         readI32(bytes, at, out.experience) &&
         readI32(bytes, at, out.experienceNext) &&
         readI32(bytes, at, out.learningPoints) &&
         readI32(bytes, at, out.healthCurrent) &&
         readI32(bytes, at, out.healthMax) &&
         readI32(bytes, at, out.manaCurrent) &&
         readI32(bytes, at, out.manaMax) &&
         readI32(bytes, at, out.strength) &&
         readI32(bytes, at, out.dexterity) &&
         readI32(bytes, at, out.guild) &&
         readI32(bytes, at, out.trueGuild) &&
         readI32(bytes, at, out.permanentAttitude) &&
         readI32(bytes, at, out.temporaryAttitude);
}

inline std::vector<std::uint8_t> encodeClientMovementPacket(const ClientMovementPacket& packet) {
  std::vector<std::uint8_t> out;
  out.reserve(packet.sessionKey.size() + packet.targetKey.size() + packet.idempotencyKey.size() +
              packet.source.size() + packet.actorKey.size() + packet.characterKey.size() +
              packet.world.size() + packet.waypointKey.size() + packet.reason.size() + 280);

  appendU32(out, PacketMagic);
  appendU16(out, PacketVersion);
  appendU16(out, static_cast<std::uint16_t>(PacketKind::ClientMovement));
  appendU16(out, 0);
  appendU16(out, static_cast<std::uint16_t>(packet.kind));
  appendU64(out, packet.packetSequence);
  appendU64(out, packet.clientTick);
  appendU64(out, packet.localSequence);
  appendU32(out, packet.flags);
  appendU64(out, packet.fromTick);
  appendU64(out, packet.toTick);
  appendU64(out, packet.deltaMs);
  appendF64(out, packet.fromX);
  appendF64(out, packet.fromY);
  appendF64(out, packet.fromZ);
  appendF64(out, packet.toX);
  appendF64(out, packet.toY);
  appendF64(out, packet.toZ);
  appendF64(out, packet.fromYaw);
  appendF64(out, packet.toYaw);
  appendU64(out, packet.cadenceIntervalMs);
  appendF64(out, packet.cadenceMinDistance);
  appendF64(out, packet.cadenceMinYawDeg);
  appendU64(out, packet.checkpointForceIntervalMs);
  appendMovementStats(out, packet.stats);

  if(!appendString16(out, packet.sessionKey) ||
     !appendString16(out, packet.targetKey) ||
     !appendString16(out, packet.idempotencyKey) ||
     !appendString16(out, packet.source) ||
     !appendString16(out, packet.actorKey) ||
     !appendString16(out, packet.characterKey) ||
     !appendString16(out, packet.world) ||
     !appendString16(out, packet.waypointKey) ||
     !appendString16(out, packet.reason)) {
    return {};
  }

  if(out.size() > MaxDatagramBytes)
    return {};
  return out;
}

inline ClientMovementDecodeResult decodeClientMovementPacket(std::string_view bytes) {
  ClientMovementDecodeResult result;
  if(bytes.size() < 4 + 2 + 2 + 2 + 2 + 8 + 8 + 8 + 4) {
    result.error = DecodeError::TooSmall;
    return result;
  }

  std::size_t at = 0;
  std::uint32_t magic = 0;
  std::uint16_t version = 0;
  std::uint16_t packetKind = 0;
  std::uint16_t flags = 0;
  std::uint16_t actionKind = 0;

  if(!readU32(bytes, at, magic) || magic != PacketMagic) {
    result.error = DecodeError::BadMagic;
    return result;
  }
  if(!readU16(bytes, at, version) || version != PacketVersion) {
    result.error = DecodeError::BadVersion;
    return result;
  }
  if(!readU16(bytes, at, packetKind) || packetKind != static_cast<std::uint16_t>(PacketKind::ClientMovement)) {
    result.error = DecodeError::BadPacketKind;
    return result;
  }
  if(!readU16(bytes, at, flags) ||
     !readU16(bytes, at, actionKind) ||
     !readU64(bytes, at, result.movement.packetSequence) ||
     !readU64(bytes, at, result.movement.clientTick) ||
     !readU64(bytes, at, result.movement.localSequence) ||
     !readU32(bytes, at, result.movement.flags) ||
     !readU64(bytes, at, result.movement.fromTick) ||
     !readU64(bytes, at, result.movement.toTick) ||
     !readU64(bytes, at, result.movement.deltaMs) ||
     !readF64(bytes, at, result.movement.fromX) ||
     !readF64(bytes, at, result.movement.fromY) ||
     !readF64(bytes, at, result.movement.fromZ) ||
     !readF64(bytes, at, result.movement.toX) ||
     !readF64(bytes, at, result.movement.toY) ||
     !readF64(bytes, at, result.movement.toZ) ||
     !readF64(bytes, at, result.movement.fromYaw) ||
     !readF64(bytes, at, result.movement.toYaw) ||
     !readU64(bytes, at, result.movement.cadenceIntervalMs) ||
     !readF64(bytes, at, result.movement.cadenceMinDistance) ||
     !readF64(bytes, at, result.movement.cadenceMinYawDeg) ||
     !readU64(bytes, at, result.movement.checkpointForceIntervalMs) ||
     !readMovementStats(bytes, at, result.movement.stats) ||
     !readString16(bytes, at, result.movement.sessionKey) ||
     !readString16(bytes, at, result.movement.targetKey) ||
     !readString16(bytes, at, result.movement.idempotencyKey) ||
     !readString16(bytes, at, result.movement.source) ||
     !readString16(bytes, at, result.movement.actorKey) ||
     !readString16(bytes, at, result.movement.characterKey) ||
     !readString16(bytes, at, result.movement.world) ||
     !readString16(bytes, at, result.movement.waypointKey) ||
     !readString16(bytes, at, result.movement.reason)) {
    result.error = DecodeError::Truncated;
    return result;
  }

  (void)flags;
  result.movement.kind = static_cast<SemanticActionKind>(actionKind);
  if(result.movement.kind != SemanticActionKind::MovementProposal &&
     result.movement.kind != SemanticActionKind::CharacterCheckpoint) {
    result.error = DecodeError::BadActionKind;
    return result;
  }
  return result;
}

inline bool isInventoryPacketAction(SemanticActionKind kind) noexcept {
  switch(kind) {
    case SemanticActionKind::PickupWorldItem:
    case SemanticActionKind::RemoveWorldItem:
    case SemanticActionKind::TransferCharacterItem:
    case SemanticActionKind::EquipCharacterItem:
    case SemanticActionKind::UnequipCharacterItem:
    case SemanticActionKind::DropCharacterItem:
    case SemanticActionKind::LootNpcInventory:
    case SemanticActionKind::TakeContainerItem:
    case SemanticActionKind::PutContainerItem:
    case SemanticActionKind::TradeBuyFromNpc:
    case SemanticActionKind::TradeSellToNpc:
    case SemanticActionKind::ConsumeItem:
    case SemanticActionKind::SplitItemStack:
    case SemanticActionKind::MergeItemStack:
      return true;
    default:
      return false;
  }
}

inline std::vector<std::uint8_t> encodeClientInventoryPacket(const ClientInventoryPacket& packet) {
  std::vector<std::uint8_t> out;
  out.reserve(packet.sessionKey.size() + packet.targetKey.size() + packet.idempotencyKey.size() +
              packet.source.size() + packet.actorKey.size() + packet.sourceActorKey.size() +
              packet.itemTemplateKey.size() + packet.itemInstanceId.size() + packet.equipmentSlot.size() + 520);

  appendU32(out, PacketMagic);
  appendU16(out, PacketVersion);
  appendU16(out, static_cast<std::uint16_t>(PacketKind::ClientInventory));
  appendU16(out, 0);
  appendU16(out, static_cast<std::uint16_t>(packet.kind));
  appendU64(out, packet.packetSequence);
  appendU64(out, packet.clientTick);
  appendU64(out, packet.localSequence);
  appendU32(out, packet.flags);
  appendI64(out, packet.itemSymbol);
  appendI64(out, packet.inventoryItemSymbol);
  appendI64(out, packet.itemPersistentId);
  appendI64(out, packet.sourceItemPersistentId);
  appendI64(out, packet.sourceWorldItemPersistentId);
  appendI64(out, packet.worldItemPersistentId);
  appendI64(out, packet.vendorItemPersistentId);
  appendI64(out, packet.sellerItemPersistentId);
  appendI64(out, packet.amount);
  appendI64(out, packet.slot);
  appendI64(out, packet.bagIndex);
  appendI64(out, packet.targetBagIndex);
  appendI64(out, packet.unitPrice);
  appendI64(out, packet.priceTotal);
  appendI64(out, packet.walletBefore);
  appendI64(out, packet.walletAfter);
  appendU64(out, packet.slotId);
  appendU64(out, packet.vobId);
  appendF64(out, packet.actorPosX);
  appendF64(out, packet.actorPosY);
  appendF64(out, packet.actorPosZ);
  appendF64(out, packet.itemPosX);
  appendF64(out, packet.itemPosY);
  appendF64(out, packet.itemPosZ);
  appendF64(out, packet.sourcePosX);
  appendF64(out, packet.sourcePosY);
  appendF64(out, packet.sourcePosZ);

  if(!appendString16(out, packet.sessionKey) ||
     !appendString16(out, packet.targetKey) ||
     !appendString16(out, packet.idempotencyKey) ||
     !appendString16(out, packet.source) ||
     !appendString16(out, packet.actorKey) ||
     !appendString16(out, packet.sourceActorKey) ||
     !appendString16(out, packet.targetCharacterKey) ||
     !appendString16(out, packet.itemTemplateKey) ||
     !appendString16(out, packet.itemInstanceId) ||
     !appendString16(out, packet.itemInstanceUuid) ||
     !appendString16(out, packet.equipmentSlot) ||
     !appendString16(out, packet.sourceEntityKey) ||
     !appendString16(out, packet.sourceContainerKey) ||
     !appendString16(out, packet.containerKey) ||
     !appendString16(out, packet.sourceNpcKey) ||
     !appendString16(out, packet.targetNpcEntityKey) ||
     !appendString16(out, packet.npcKey) ||
     !appendString16(out, packet.world) ||
     !appendString16(out, packet.tag) ||
     !appendString16(out, packet.focusName) ||
     !appendString16(out, packet.displayName) ||
     !appendString16(out, packet.scheme) ||
     !appendString16(out, packet.reason) ||
     !appendString16(out, packet.currencyKey)) {
    return {};
  }

  if(out.size() > MaxDatagramBytes)
    return {};
  return out;
}

inline ClientInventoryDecodeResult decodeClientInventoryPacket(std::string_view bytes) {
  ClientInventoryDecodeResult result;
  if(bytes.size() < 4 + 2 + 2 + 2 + 2 + 8 + 8 + 8 + 4) {
    result.error = DecodeError::TooSmall;
    return result;
  }

  std::size_t at = 0;
  std::uint32_t magic = 0;
  std::uint16_t version = 0;
  std::uint16_t packetKind = 0;
  std::uint16_t flags = 0;
  std::uint16_t actionKind = 0;

  if(!readU32(bytes, at, magic) || magic != PacketMagic) {
    result.error = DecodeError::BadMagic;
    return result;
  }
  if(!readU16(bytes, at, version) || version != PacketVersion) {
    result.error = DecodeError::BadVersion;
    return result;
  }
  if(!readU16(bytes, at, packetKind) || packetKind != static_cast<std::uint16_t>(PacketKind::ClientInventory)) {
    result.error = DecodeError::BadPacketKind;
    return result;
  }
  if(!readU16(bytes, at, flags) ||
     !readU16(bytes, at, actionKind) ||
     !readU64(bytes, at, result.inventory.packetSequence) ||
     !readU64(bytes, at, result.inventory.clientTick) ||
     !readU64(bytes, at, result.inventory.localSequence) ||
     !readU32(bytes, at, result.inventory.flags) ||
     !readI64(bytes, at, result.inventory.itemSymbol) ||
     !readI64(bytes, at, result.inventory.inventoryItemSymbol) ||
     !readI64(bytes, at, result.inventory.itemPersistentId) ||
     !readI64(bytes, at, result.inventory.sourceItemPersistentId) ||
     !readI64(bytes, at, result.inventory.sourceWorldItemPersistentId) ||
     !readI64(bytes, at, result.inventory.worldItemPersistentId) ||
     !readI64(bytes, at, result.inventory.vendorItemPersistentId) ||
     !readI64(bytes, at, result.inventory.sellerItemPersistentId) ||
     !readI64(bytes, at, result.inventory.amount) ||
     !readI64(bytes, at, result.inventory.slot) ||
     !readI64(bytes, at, result.inventory.bagIndex) ||
     !readI64(bytes, at, result.inventory.targetBagIndex) ||
     !readI64(bytes, at, result.inventory.unitPrice) ||
     !readI64(bytes, at, result.inventory.priceTotal) ||
     !readI64(bytes, at, result.inventory.walletBefore) ||
     !readI64(bytes, at, result.inventory.walletAfter) ||
     !readU64(bytes, at, result.inventory.slotId) ||
     !readU64(bytes, at, result.inventory.vobId) ||
     !readF64(bytes, at, result.inventory.actorPosX) ||
     !readF64(bytes, at, result.inventory.actorPosY) ||
     !readF64(bytes, at, result.inventory.actorPosZ) ||
     !readF64(bytes, at, result.inventory.itemPosX) ||
     !readF64(bytes, at, result.inventory.itemPosY) ||
     !readF64(bytes, at, result.inventory.itemPosZ) ||
     !readF64(bytes, at, result.inventory.sourcePosX) ||
     !readF64(bytes, at, result.inventory.sourcePosY) ||
     !readF64(bytes, at, result.inventory.sourcePosZ) ||
     !readString16(bytes, at, result.inventory.sessionKey) ||
     !readString16(bytes, at, result.inventory.targetKey) ||
     !readString16(bytes, at, result.inventory.idempotencyKey) ||
     !readString16(bytes, at, result.inventory.source) ||
     !readString16(bytes, at, result.inventory.actorKey) ||
     !readString16(bytes, at, result.inventory.sourceActorKey) ||
     !readString16(bytes, at, result.inventory.targetCharacterKey) ||
     !readString16(bytes, at, result.inventory.itemTemplateKey) ||
     !readString16(bytes, at, result.inventory.itemInstanceId) ||
     !readString16(bytes, at, result.inventory.itemInstanceUuid) ||
     !readString16(bytes, at, result.inventory.equipmentSlot) ||
     !readString16(bytes, at, result.inventory.sourceEntityKey) ||
     !readString16(bytes, at, result.inventory.sourceContainerKey) ||
     !readString16(bytes, at, result.inventory.containerKey) ||
     !readString16(bytes, at, result.inventory.sourceNpcKey) ||
     !readString16(bytes, at, result.inventory.targetNpcEntityKey) ||
     !readString16(bytes, at, result.inventory.npcKey) ||
     !readString16(bytes, at, result.inventory.world) ||
     !readString16(bytes, at, result.inventory.tag) ||
     !readString16(bytes, at, result.inventory.focusName) ||
     !readString16(bytes, at, result.inventory.displayName) ||
     !readString16(bytes, at, result.inventory.scheme) ||
     !readString16(bytes, at, result.inventory.reason) ||
     !readString16(bytes, at, result.inventory.currencyKey)) {
    result.error = DecodeError::Truncated;
    return result;
  }

  (void)flags;
  result.inventory.kind = static_cast<SemanticActionKind>(actionKind);
  if(!isInventoryPacketAction(result.inventory.kind)) {
    result.error = DecodeError::BadActionKind;
    return result;
  }
  return result;
}

inline bool isWorldStatePacketAction(SemanticActionKind kind) noexcept {
  switch(kind) {
    case SemanticActionKind::ReadyWeapon:
    case SemanticActionKind::HolsterWeapon:
    case SemanticActionKind::UseInteractive:
    case SemanticActionKind::UpdateInteractiveState:
    case SemanticActionKind::SetScriptInt:
    case SemanticActionKind::UpdateQuest:
    case SemanticActionKind::WorldTimeChanged:
    case SemanticActionKind::TriggerEvent:
    case SemanticActionKind::MoverStateChanged:
    case SemanticActionKind::RecordTriggerQueueState:
    case SemanticActionKind::RecordWorldTransitionState:
      return true;
    default:
      return false;
  }
}

inline std::vector<std::uint8_t> encodeClientWorldStatePacket(const ClientWorldStatePacket& packet) {
  std::vector<std::uint8_t> out;
  out.reserve(packet.sessionKey.size() + packet.targetKey.size() + packet.idempotencyKey.size() +
              packet.actorKey.size() + packet.interactiveKey.size() + packet.scriptKey.size() +
              packet.questKey.size() + packet.subtitleText.size() + 900);

  appendU32(out, PacketMagic);
  appendU16(out, PacketVersion);
  appendU16(out, static_cast<std::uint16_t>(PacketKind::ClientWorldState));
  appendU16(out, 0);
  appendU16(out, static_cast<std::uint16_t>(packet.kind));
  appendU64(out, packet.packetSequence);
  appendU64(out, packet.clientTick);
  appendU64(out, packet.localSequence);
  appendU32(out, packet.flags);
  appendI64(out, packet.valueBefore);
  appendI64(out, packet.valueAfter);
  appendI64(out, packet.valueDelta);
  appendI64(out, packet.secondaryBefore);
  appendI64(out, packet.secondaryAfter);
  appendI64(out, packet.secondaryDelta);
  appendI64(out, packet.tertiaryBefore);
  appendI64(out, packet.tertiaryAfter);
  appendI64(out, packet.tertiaryDelta);
  appendI64(out, packet.symbolIndex);
  appendI64(out, packet.valueIndex);
  appendI64(out, packet.scriptFunctionSymbol);
  appendI64(out, packet.npcSymbol);
  appendI64(out, packet.infoSymbol);
  appendI64(out, packet.entryCount);
  appendI64(out, packet.durationMs);
  appendI64(out, packet.worldTimeBeforeMs);
  appendI64(out, packet.worldTimeAfterMs);
  appendI64(out, packet.worldDayBefore);
  appendI64(out, packet.worldDayAfter);
  appendI64(out, packet.worldHourBefore);
  appendI64(out, packet.worldHourAfter);
  appendI64(out, packet.worldMinuteBefore);
  appendI64(out, packet.worldMinuteAfter);
  appendI64(out, packet.eventType);
  appendI64(out, packet.stateBefore);
  appendI64(out, packet.stateAfter);
  appendI64(out, packet.frame);
  appendI64(out, packet.targetFrame);
  appendI64(out, packet.stateCount);
  appendI64(out, packet.stateMask);
  appendI64(out, packet.slotId);
  appendI64(out, packet.vobId);
  appendF64(out, packet.actorPosX);
  appendF64(out, packet.actorPosY);
  appendF64(out, packet.actorPosZ);
  appendF64(out, packet.targetPosX);
  appendF64(out, packet.targetPosY);
  appendF64(out, packet.targetPosZ);
  appendF64(out, packet.sourcePosX);
  appendF64(out, packet.sourcePosY);
  appendF64(out, packet.sourcePosZ);

  if(!appendString16(out, packet.sessionKey) ||
     !appendString16(out, packet.targetKey) ||
     !appendString16(out, packet.idempotencyKey) ||
     !appendString16(out, packet.source) ||
     !appendString16(out, packet.actorKey) ||
     !appendString16(out, packet.characterKey) ||
     !appendString16(out, packet.world) ||
     !appendString16(out, packet.reason) ||
     !appendString16(out, packet.resourceKey) ||
     !appendString16(out, packet.interactiveKey) ||
     !appendString16(out, packet.entityKey) ||
     !appendString16(out, packet.tag) ||
     !appendString16(out, packet.focusName) ||
     !appendString16(out, packet.displayName) ||
     !appendString16(out, packet.scheme) ||
     !appendString16(out, packet.stateBeforeName) ||
     !appendString16(out, packet.stateAfterName) ||
     !appendString16(out, packet.eventTypeName) ||
     !appendString16(out, packet.eventTarget) ||
     !appendString16(out, packet.eventEmitter) ||
     !appendString16(out, packet.triggerName) ||
     !appendString16(out, packet.triggerTargetName) ||
     !appendString16(out, packet.scriptKey) ||
     !appendString16(out, packet.globalKey) ||
     !appendString16(out, packet.symbolName) ||
     !appendString16(out, packet.scriptFunctionName) ||
     !appendString16(out, packet.questKey) ||
     !appendString16(out, packet.questName) ||
     !appendString16(out, packet.status) ||
     !appendString16(out, packet.npcKey) ||
     !appendString16(out, packet.npcSymbolName) ||
     !appendString16(out, packet.infoKey) ||
     !appendString16(out, packet.infoSymbolName) ||
     !appendString16(out, packet.conversationKey) ||
     !appendString16(out, packet.syncGroup) ||
     !appendString16(out, packet.speakerKey) ||
     !appendString16(out, packet.listenerKey) ||
     !appendString16(out, packet.outputName) ||
     !appendString16(out, packet.messageName) ||
     !appendString16(out, packet.subtitleText)) {
    return {};
  }

  if(out.size() > MaxDatagramBytes)
    return {};
  return out;
}

inline ClientWorldStateDecodeResult decodeClientWorldStatePacket(std::string_view bytes) {
  ClientWorldStateDecodeResult result;
  if(bytes.size() < 4 + 2 + 2 + 2 + 2 + 8 + 8 + 8 + 4) {
    result.error = DecodeError::TooSmall;
    return result;
  }

  std::size_t at = 0;
  std::uint32_t magic = 0;
  std::uint16_t version = 0;
  std::uint16_t packetKind = 0;
  std::uint16_t flags = 0;
  std::uint16_t actionKind = 0;

  if(!readU32(bytes, at, magic) || magic != PacketMagic) {
    result.error = DecodeError::BadMagic;
    return result;
  }
  if(!readU16(bytes, at, version) || version != PacketVersion) {
    result.error = DecodeError::BadVersion;
    return result;
  }
  if(!readU16(bytes, at, packetKind) || packetKind != static_cast<std::uint16_t>(PacketKind::ClientWorldState)) {
    result.error = DecodeError::BadPacketKind;
    return result;
  }
  if(!readU16(bytes, at, flags) ||
     !readU16(bytes, at, actionKind) ||
     !readU64(bytes, at, result.worldState.packetSequence) ||
     !readU64(bytes, at, result.worldState.clientTick) ||
     !readU64(bytes, at, result.worldState.localSequence) ||
     !readU32(bytes, at, result.worldState.flags) ||
     !readI64(bytes, at, result.worldState.valueBefore) ||
     !readI64(bytes, at, result.worldState.valueAfter) ||
     !readI64(bytes, at, result.worldState.valueDelta) ||
     !readI64(bytes, at, result.worldState.secondaryBefore) ||
     !readI64(bytes, at, result.worldState.secondaryAfter) ||
     !readI64(bytes, at, result.worldState.secondaryDelta) ||
     !readI64(bytes, at, result.worldState.tertiaryBefore) ||
     !readI64(bytes, at, result.worldState.tertiaryAfter) ||
     !readI64(bytes, at, result.worldState.tertiaryDelta) ||
     !readI64(bytes, at, result.worldState.symbolIndex) ||
     !readI64(bytes, at, result.worldState.valueIndex) ||
     !readI64(bytes, at, result.worldState.scriptFunctionSymbol) ||
     !readI64(bytes, at, result.worldState.npcSymbol) ||
     !readI64(bytes, at, result.worldState.infoSymbol) ||
     !readI64(bytes, at, result.worldState.entryCount) ||
     !readI64(bytes, at, result.worldState.durationMs) ||
     !readI64(bytes, at, result.worldState.worldTimeBeforeMs) ||
     !readI64(bytes, at, result.worldState.worldTimeAfterMs) ||
     !readI64(bytes, at, result.worldState.worldDayBefore) ||
     !readI64(bytes, at, result.worldState.worldDayAfter) ||
     !readI64(bytes, at, result.worldState.worldHourBefore) ||
     !readI64(bytes, at, result.worldState.worldHourAfter) ||
     !readI64(bytes, at, result.worldState.worldMinuteBefore) ||
     !readI64(bytes, at, result.worldState.worldMinuteAfter) ||
     !readI64(bytes, at, result.worldState.eventType) ||
     !readI64(bytes, at, result.worldState.stateBefore) ||
     !readI64(bytes, at, result.worldState.stateAfter) ||
     !readI64(bytes, at, result.worldState.frame) ||
     !readI64(bytes, at, result.worldState.targetFrame) ||
     !readI64(bytes, at, result.worldState.stateCount) ||
     !readI64(bytes, at, result.worldState.stateMask) ||
     !readI64(bytes, at, result.worldState.slotId) ||
     !readI64(bytes, at, result.worldState.vobId) ||
     !readF64(bytes, at, result.worldState.actorPosX) ||
     !readF64(bytes, at, result.worldState.actorPosY) ||
     !readF64(bytes, at, result.worldState.actorPosZ) ||
     !readF64(bytes, at, result.worldState.targetPosX) ||
     !readF64(bytes, at, result.worldState.targetPosY) ||
     !readF64(bytes, at, result.worldState.targetPosZ) ||
     !readF64(bytes, at, result.worldState.sourcePosX) ||
     !readF64(bytes, at, result.worldState.sourcePosY) ||
     !readF64(bytes, at, result.worldState.sourcePosZ) ||
     !readString16(bytes, at, result.worldState.sessionKey) ||
     !readString16(bytes, at, result.worldState.targetKey) ||
     !readString16(bytes, at, result.worldState.idempotencyKey) ||
     !readString16(bytes, at, result.worldState.source) ||
     !readString16(bytes, at, result.worldState.actorKey) ||
     !readString16(bytes, at, result.worldState.characterKey) ||
     !readString16(bytes, at, result.worldState.world) ||
     !readString16(bytes, at, result.worldState.reason) ||
     !readString16(bytes, at, result.worldState.resourceKey) ||
     !readString16(bytes, at, result.worldState.interactiveKey) ||
     !readString16(bytes, at, result.worldState.entityKey) ||
     !readString16(bytes, at, result.worldState.tag) ||
     !readString16(bytes, at, result.worldState.focusName) ||
     !readString16(bytes, at, result.worldState.displayName) ||
     !readString16(bytes, at, result.worldState.scheme) ||
     !readString16(bytes, at, result.worldState.stateBeforeName) ||
     !readString16(bytes, at, result.worldState.stateAfterName) ||
     !readString16(bytes, at, result.worldState.eventTypeName) ||
     !readString16(bytes, at, result.worldState.eventTarget) ||
     !readString16(bytes, at, result.worldState.eventEmitter) ||
     !readString16(bytes, at, result.worldState.triggerName) ||
     !readString16(bytes, at, result.worldState.triggerTargetName) ||
     !readString16(bytes, at, result.worldState.scriptKey) ||
     !readString16(bytes, at, result.worldState.globalKey) ||
     !readString16(bytes, at, result.worldState.symbolName) ||
     !readString16(bytes, at, result.worldState.scriptFunctionName) ||
     !readString16(bytes, at, result.worldState.questKey) ||
     !readString16(bytes, at, result.worldState.questName) ||
     !readString16(bytes, at, result.worldState.status) ||
     !readString16(bytes, at, result.worldState.npcKey) ||
     !readString16(bytes, at, result.worldState.npcSymbolName) ||
     !readString16(bytes, at, result.worldState.infoKey) ||
     !readString16(bytes, at, result.worldState.infoSymbolName) ||
     !readString16(bytes, at, result.worldState.conversationKey) ||
     !readString16(bytes, at, result.worldState.syncGroup) ||
     !readString16(bytes, at, result.worldState.speakerKey) ||
     !readString16(bytes, at, result.worldState.listenerKey) ||
     !readString16(bytes, at, result.worldState.outputName) ||
     !readString16(bytes, at, result.worldState.messageName) ||
     !readString16(bytes, at, result.worldState.subtitleText)) {
    result.error = DecodeError::Truncated;
    return result;
  }

  (void)flags;
  result.worldState.kind = static_cast<SemanticActionKind>(actionKind);
  if(!isWorldStatePacketAction(result.worldState.kind)) {
    result.error = DecodeError::BadActionKind;
    return result;
  }
  return result;
}

inline bool isNpcStatePacketAction(SemanticActionKind kind) noexcept {
  switch(kind) {
    case SemanticActionKind::MarkNpcDead:
    case SemanticActionKind::RespawnNpc:
    case SemanticActionKind::RecordNpcRoutineState:
    case SemanticActionKind::RecordNpcAiState:
    case SemanticActionKind::RecordNpcPathState:
    case SemanticActionKind::RecordNpcFightState:
    case SemanticActionKind::RecordCombatIntent:
    case SemanticActionKind::RecordNpcActionState:
      return true;
    default:
      return false;
  }
}

inline void appendNpcStateNumbers(std::vector<std::uint8_t>& out, const ClientNpcStatePacket& packet) {
  appendI64(out, packet.npcPersistentId);
  appendI64(out, packet.npcSymbol);
  appendI64(out, packet.targetNpcPersistentId);
  appendI64(out, packet.targetNpcSymbol);
  appendI64(out, packet.sourceNpcPersistentId);
  appendI64(out, packet.sourceNpcSymbol);
  appendI64(out, packet.healthCurrent);
  appendI64(out, packet.healthMax);
  appendI64(out, packet.aiStateFunction);
  appendI64(out, packet.remainingPathPoints);
  appendI64(out, packet.comboIndex);
  appendI64(out, packet.bodyState);
  appendI64(out, packet.weaponStateId);
  appendI64(out, packet.animationElapsedMs);
  appendI64(out, packet.attackAnimationElapsedMs);
  appendI64(out, packet.animationTotalMs);
  appendI64(out, packet.attackTotalMs);
  appendI64(out, packet.attackOptimalMs);
  appendI64(out, packet.attackHitEndMs);
  appendI64(out, packet.parryWindowStartMs);
  appendI64(out, packet.parryWindowEndMs);
  appendI64(out, packet.comboWindowStartMs);
  appendI64(out, packet.comboWindowEndMs);
  appendF64(out, packet.posX);
  appendF64(out, packet.posY);
  appendF64(out, packet.posZ);
  appendF64(out, packet.targetPosX);
  appendF64(out, packet.targetPosY);
  appendF64(out, packet.targetPosZ);
  appendF64(out, packet.attackerCenterX);
  appendF64(out, packet.attackerCenterY);
  appendF64(out, packet.attackerCenterZ);
  appendF64(out, packet.opponentCenterX);
  appendF64(out, packet.opponentCenterY);
  appendF64(out, packet.opponentCenterZ);
  appendF64(out, packet.fightDistanceX);
  appendF64(out, packet.fightDistanceY);
  appendF64(out, packet.fightDistanceZ);
  appendF64(out, packet.attackerYawRad);
  appendF64(out, packet.opponentYawRad);
  appendF64(out, packet.weaponRange);
  appendF64(out, packet.attackRange);
  appendF64(out, packet.opponentAttackRange);
  appendF64(out, packet.attackerFightRangeBase);
  appendF64(out, packet.opponentFightRangeBase);
}

inline bool readNpcStateNumbers(std::string_view bytes, std::size_t& at, ClientNpcStatePacket& packet) noexcept {
  return readI64(bytes, at, packet.npcPersistentId) &&
         readI64(bytes, at, packet.npcSymbol) &&
         readI64(bytes, at, packet.targetNpcPersistentId) &&
         readI64(bytes, at, packet.targetNpcSymbol) &&
         readI64(bytes, at, packet.sourceNpcPersistentId) &&
         readI64(bytes, at, packet.sourceNpcSymbol) &&
         readI64(bytes, at, packet.healthCurrent) &&
         readI64(bytes, at, packet.healthMax) &&
         readI64(bytes, at, packet.aiStateFunction) &&
         readI64(bytes, at, packet.remainingPathPoints) &&
         readI64(bytes, at, packet.comboIndex) &&
         readI64(bytes, at, packet.bodyState) &&
         readI64(bytes, at, packet.weaponStateId) &&
         readI64(bytes, at, packet.animationElapsedMs) &&
         readI64(bytes, at, packet.attackAnimationElapsedMs) &&
         readI64(bytes, at, packet.animationTotalMs) &&
         readI64(bytes, at, packet.attackTotalMs) &&
         readI64(bytes, at, packet.attackOptimalMs) &&
         readI64(bytes, at, packet.attackHitEndMs) &&
         readI64(bytes, at, packet.parryWindowStartMs) &&
         readI64(bytes, at, packet.parryWindowEndMs) &&
         readI64(bytes, at, packet.comboWindowStartMs) &&
         readI64(bytes, at, packet.comboWindowEndMs) &&
         readF64(bytes, at, packet.posX) &&
         readF64(bytes, at, packet.posY) &&
         readF64(bytes, at, packet.posZ) &&
         readF64(bytes, at, packet.targetPosX) &&
         readF64(bytes, at, packet.targetPosY) &&
         readF64(bytes, at, packet.targetPosZ) &&
         readF64(bytes, at, packet.attackerCenterX) &&
         readF64(bytes, at, packet.attackerCenterY) &&
         readF64(bytes, at, packet.attackerCenterZ) &&
         readF64(bytes, at, packet.opponentCenterX) &&
         readF64(bytes, at, packet.opponentCenterY) &&
         readF64(bytes, at, packet.opponentCenterZ) &&
         readF64(bytes, at, packet.fightDistanceX) &&
         readF64(bytes, at, packet.fightDistanceY) &&
         readF64(bytes, at, packet.fightDistanceZ) &&
         readF64(bytes, at, packet.attackerYawRad) &&
         readF64(bytes, at, packet.opponentYawRad) &&
         readF64(bytes, at, packet.weaponRange) &&
         readF64(bytes, at, packet.attackRange) &&
         readF64(bytes, at, packet.opponentAttackRange) &&
         readF64(bytes, at, packet.attackerFightRangeBase) &&
         readF64(bytes, at, packet.opponentFightRangeBase);
}

inline bool appendNpcStateStrings(std::vector<std::uint8_t>& out, const ClientNpcStatePacket& packet) {
  return appendString16(out, packet.sessionKey) &&
         appendString16(out, packet.targetKey) &&
         appendString16(out, packet.idempotencyKey) &&
         appendString16(out, packet.source) &&
         appendString16(out, packet.reason) &&
         appendString16(out, packet.actorKey) &&
         appendString16(out, packet.npcEntityKey) &&
         appendString16(out, packet.npcKey) &&
         appendString16(out, packet.targetNpcEntityKey) &&
         appendString16(out, packet.targetNpcKey) &&
         appendString16(out, packet.sourceNpcEntityKey) &&
         appendString16(out, packet.sourceActorKey) &&
         appendString16(out, packet.displayName) &&
         appendString16(out, packet.targetDisplayName) &&
         appendString16(out, packet.world) &&
         appendString16(out, packet.routineState) &&
         appendString16(out, packet.scheduleKey) &&
         appendString16(out, packet.currentWaypointKey) &&
         appendString16(out, packet.currentWaypointName) &&
         appendString16(out, packet.currentWaypointLegacy) &&
         appendString16(out, packet.targetWaypointKey) &&
         appendString16(out, packet.targetWaypointName) &&
         appendString16(out, packet.targetWaypointLegacy) &&
         appendString16(out, packet.nextWaypointKey) &&
         appendString16(out, packet.nextWaypointName) &&
         appendString16(out, packet.nextWaypointLegacy) &&
         appendString16(out, packet.aiState) &&
         appendString16(out, packet.aiIntent) &&
         appendString16(out, packet.aiTargetKey) &&
         appendString16(out, packet.perceptionState) &&
         appendString16(out, packet.actionKey) &&
         appendString16(out, packet.actionName) &&
         appendString16(out, packet.actionState) &&
         appendString16(out, packet.actionTargetKey) &&
         appendString16(out, packet.syncGroup) &&
         appendString16(out, packet.pathState) &&
         appendString16(out, packet.routeKey) &&
         appendString16(out, packet.moveHint) &&
         appendString16(out, packet.opponentKey) &&
         appendString16(out, packet.fightState) &&
         appendString16(out, packet.attackState) &&
         appendString16(out, packet.combatAction) &&
         appendString16(out, packet.intentState) &&
         appendString16(out, packet.weaponState) &&
         appendString16(out, packet.animationName) &&
         appendString16(out, packet.attackAnimationName);
}

inline bool readNpcStateStrings(std::string_view bytes, std::size_t& at, ClientNpcStatePacket& packet) {
  return readString16(bytes, at, packet.sessionKey) &&
         readString16(bytes, at, packet.targetKey) &&
         readString16(bytes, at, packet.idempotencyKey) &&
         readString16(bytes, at, packet.source) &&
         readString16(bytes, at, packet.reason) &&
         readString16(bytes, at, packet.actorKey) &&
         readString16(bytes, at, packet.npcEntityKey) &&
         readString16(bytes, at, packet.npcKey) &&
         readString16(bytes, at, packet.targetNpcEntityKey) &&
         readString16(bytes, at, packet.targetNpcKey) &&
         readString16(bytes, at, packet.sourceNpcEntityKey) &&
         readString16(bytes, at, packet.sourceActorKey) &&
         readString16(bytes, at, packet.displayName) &&
         readString16(bytes, at, packet.targetDisplayName) &&
         readString16(bytes, at, packet.world) &&
         readString16(bytes, at, packet.routineState) &&
         readString16(bytes, at, packet.scheduleKey) &&
         readString16(bytes, at, packet.currentWaypointKey) &&
         readString16(bytes, at, packet.currentWaypointName) &&
         readString16(bytes, at, packet.currentWaypointLegacy) &&
         readString16(bytes, at, packet.targetWaypointKey) &&
         readString16(bytes, at, packet.targetWaypointName) &&
         readString16(bytes, at, packet.targetWaypointLegacy) &&
         readString16(bytes, at, packet.nextWaypointKey) &&
         readString16(bytes, at, packet.nextWaypointName) &&
         readString16(bytes, at, packet.nextWaypointLegacy) &&
         readString16(bytes, at, packet.aiState) &&
         readString16(bytes, at, packet.aiIntent) &&
         readString16(bytes, at, packet.aiTargetKey) &&
         readString16(bytes, at, packet.perceptionState) &&
         readString16(bytes, at, packet.actionKey) &&
         readString16(bytes, at, packet.actionName) &&
         readString16(bytes, at, packet.actionState) &&
         readString16(bytes, at, packet.actionTargetKey) &&
         readString16(bytes, at, packet.syncGroup) &&
         readString16(bytes, at, packet.pathState) &&
         readString16(bytes, at, packet.routeKey) &&
         readString16(bytes, at, packet.moveHint) &&
         readString16(bytes, at, packet.opponentKey) &&
         readString16(bytes, at, packet.fightState) &&
         readString16(bytes, at, packet.attackState) &&
         readString16(bytes, at, packet.combatAction) &&
         readString16(bytes, at, packet.intentState) &&
         readString16(bytes, at, packet.weaponState) &&
         readString16(bytes, at, packet.animationName) &&
         readString16(bytes, at, packet.attackAnimationName);
}

inline std::vector<std::uint8_t> encodeClientNpcStatePacket(const ClientNpcStatePacket& packet) {
  std::vector<std::uint8_t> out;
  out.reserve(packet.sessionKey.size() + packet.targetKey.size() + packet.idempotencyKey.size() +
              packet.actorKey.size() + packet.npcEntityKey.size() + packet.animationName.size() + 1200);

  appendU32(out, PacketMagic);
  appendU16(out, PacketVersion);
  appendU16(out, static_cast<std::uint16_t>(PacketKind::ClientNpcState));
  appendU16(out, 0);
  appendU16(out, static_cast<std::uint16_t>(packet.kind));
  appendU64(out, packet.packetSequence);
  appendU64(out, packet.clientTick);
  appendU64(out, packet.localSequence);
  appendU32(out, packet.flags);
  appendNpcStateNumbers(out, packet);
  if(!appendNpcStateStrings(out, packet))
    return {};
  if(out.size() > MaxDatagramBytes)
    return {};
  return out;
}

inline ClientNpcStateDecodeResult decodeClientNpcStatePacket(std::string_view bytes) {
  ClientNpcStateDecodeResult result;
  if(bytes.size() < 4 + 2 + 2 + 2 + 2 + 8 + 8 + 8 + 4) {
    result.error = DecodeError::TooSmall;
    return result;
  }

  std::size_t at = 0;
  std::uint32_t magic = 0;
  std::uint16_t version = 0;
  std::uint16_t packetKind = 0;
  std::uint16_t flags = 0;
  std::uint16_t actionKind = 0;

  if(!readU32(bytes, at, magic) || magic != PacketMagic) {
    result.error = DecodeError::BadMagic;
    return result;
  }
  if(!readU16(bytes, at, version) || version != PacketVersion) {
    result.error = DecodeError::BadVersion;
    return result;
  }
  if(!readU16(bytes, at, packetKind) || packetKind != static_cast<std::uint16_t>(PacketKind::ClientNpcState)) {
    result.error = DecodeError::BadPacketKind;
    return result;
  }
  if(!readU16(bytes, at, flags) ||
     !readU16(bytes, at, actionKind) ||
     !readU64(bytes, at, result.npcState.packetSequence) ||
     !readU64(bytes, at, result.npcState.clientTick) ||
     !readU64(bytes, at, result.npcState.localSequence) ||
     !readU32(bytes, at, result.npcState.flags) ||
     !readNpcStateNumbers(bytes, at, result.npcState) ||
     !readNpcStateStrings(bytes, at, result.npcState)) {
    result.error = DecodeError::Truncated;
    return result;
  }

  (void)flags;
  result.npcState.kind = static_cast<SemanticActionKind>(actionKind);
  if(!isNpcStatePacketAction(result.npcState.kind)) {
    result.error = DecodeError::BadActionKind;
    return result;
  }
  return result;
}

inline bool isEconomyPacketAction(SemanticActionKind kind) noexcept {
  return kind == SemanticActionKind::WalletDelta ||
         kind == SemanticActionKind::GrantGold ||
         kind == SemanticActionKind::SpendGold;
}

inline std::vector<std::uint8_t> encodeClientEconomyPacket(const ClientEconomyPacket& packet) {
  std::vector<std::uint8_t> out;
  out.reserve(packet.sessionKey.size() + packet.targetKey.size() + packet.idempotencyKey.size() +
              packet.actorKey.size() + packet.characterKey.size() + packet.currencyKey.size() + 220);

  appendU32(out, PacketMagic);
  appendU16(out, PacketVersion);
  appendU16(out, static_cast<std::uint16_t>(PacketKind::ClientEconomy));
  appendU16(out, 0);
  appendU16(out, static_cast<std::uint16_t>(packet.kind));
  appendU64(out, packet.packetSequence);
  appendU64(out, packet.clientTick);
  appendU64(out, packet.localSequence);
  appendU32(out, packet.flags);
  appendI64(out, packet.amount);
  appendI64(out, packet.deltaAmount);
  appendI64(out, packet.walletBefore);
  appendI64(out, packet.walletAfter);
  appendI64(out, packet.itemTemplateSymbol);
  appendF64(out, packet.actorPosX);
  appendF64(out, packet.actorPosY);
  appendF64(out, packet.actorPosZ);
  if(!appendString16(out, packet.sessionKey) ||
     !appendString16(out, packet.targetKey) ||
     !appendString16(out, packet.idempotencyKey) ||
     !appendString16(out, packet.source) ||
     !appendString16(out, packet.actorKey) ||
     !appendString16(out, packet.characterKey) ||
     !appendString16(out, packet.currencyKey) ||
     !appendString16(out, packet.currencyDisplayName) ||
     !appendString16(out, packet.world) ||
     !appendString16(out, packet.reason)) {
    return {};
  }
  if(out.size() > MaxDatagramBytes)
    return {};
  return out;
}

inline ClientEconomyDecodeResult decodeClientEconomyPacket(std::string_view bytes) {
  ClientEconomyDecodeResult result;
  if(bytes.size() < 4 + 2 + 2 + 2 + 2 + 8 + 8 + 8 + 4) {
    result.error = DecodeError::TooSmall;
    return result;
  }

  std::size_t at = 0;
  std::uint32_t magic = 0;
  std::uint16_t version = 0;
  std::uint16_t packetKind = 0;
  std::uint16_t flags = 0;
  std::uint16_t actionKind = 0;

  if(!readU32(bytes, at, magic) || magic != PacketMagic) {
    result.error = DecodeError::BadMagic;
    return result;
  }
  if(!readU16(bytes, at, version) || version != PacketVersion) {
    result.error = DecodeError::BadVersion;
    return result;
  }
  if(!readU16(bytes, at, packetKind) || packetKind != static_cast<std::uint16_t>(PacketKind::ClientEconomy)) {
    result.error = DecodeError::BadPacketKind;
    return result;
  }
  if(!readU16(bytes, at, flags) ||
     !readU16(bytes, at, actionKind) ||
     !readU64(bytes, at, result.economy.packetSequence) ||
     !readU64(bytes, at, result.economy.clientTick) ||
     !readU64(bytes, at, result.economy.localSequence) ||
     !readU32(bytes, at, result.economy.flags) ||
     !readI64(bytes, at, result.economy.amount) ||
     !readI64(bytes, at, result.economy.deltaAmount) ||
     !readI64(bytes, at, result.economy.walletBefore) ||
     !readI64(bytes, at, result.economy.walletAfter) ||
     !readI64(bytes, at, result.economy.itemTemplateSymbol) ||
     !readF64(bytes, at, result.economy.actorPosX) ||
     !readF64(bytes, at, result.economy.actorPosY) ||
     !readF64(bytes, at, result.economy.actorPosZ) ||
     !readString16(bytes, at, result.economy.sessionKey) ||
     !readString16(bytes, at, result.economy.targetKey) ||
     !readString16(bytes, at, result.economy.idempotencyKey) ||
     !readString16(bytes, at, result.economy.source) ||
     !readString16(bytes, at, result.economy.actorKey) ||
     !readString16(bytes, at, result.economy.characterKey) ||
     !readString16(bytes, at, result.economy.currencyKey) ||
     !readString16(bytes, at, result.economy.currencyDisplayName) ||
     !readString16(bytes, at, result.economy.world) ||
     !readString16(bytes, at, result.economy.reason)) {
    result.error = DecodeError::Truncated;
    return result;
  }
  (void)flags;
  result.economy.kind = static_cast<SemanticActionKind>(actionKind);
  if(!isEconomyPacketAction(result.economy.kind)) {
    result.error = DecodeError::BadActionKind;
    return result;
  }
  return result;
}

inline bool isSessionControlPacketAction(SemanticActionKind kind) noexcept {
  return kind == SemanticActionKind::ClientBootstrapRequest ||
         kind == SemanticActionKind::ClientCorrectionAck ||
         kind == SemanticActionKind::SaveCheckpointManifest;
}

inline std::vector<std::uint8_t> encodeClientSessionControlPacket(const ClientSessionControlPacket& packet) {
  std::vector<std::uint8_t> out;
  out.reserve(packet.sessionKey.size() + packet.targetKey.size() + packet.idempotencyKey.size() +
              packet.characterKey.size() + packet.serverEndpoint.size() + packet.clientContentManifestHash.size() +
              packet.nativeSavePath.size() + 360);

  appendU32(out, PacketMagic);
  appendU16(out, PacketVersion);
  appendU16(out, static_cast<std::uint16_t>(PacketKind::ClientSessionControl));
  appendU16(out, 0);
  appendU16(out, static_cast<std::uint16_t>(packet.kind));
  appendU64(out, packet.packetSequence);
  appendU64(out, packet.clientTick);
  appendU64(out, packet.localSequence);
  appendU32(out, packet.flags);
  appendI64(out, packet.serverTick);
  appendI64(out, packet.acknowledgedLocalSequence);
  if(!appendString16(out, packet.sessionKey) ||
     !appendString16(out, packet.targetKey) ||
     !appendString16(out, packet.idempotencyKey) ||
     !appendString16(out, packet.source) ||
     !appendString16(out, packet.sourceLocation) ||
     !appendString16(out, packet.actorKey) ||
     !appendString16(out, packet.characterKey) ||
     !appendString16(out, packet.displayName) ||
     !appendString16(out, packet.world) ||
     !appendString16(out, packet.serverEndpoint) ||
     !appendString16(out, packet.reason) ||
     !appendString16(out, packet.actionKind) ||
     !appendString16(out, packet.manifestKey) ||
     !appendString16(out, packet.checkpointKind) ||
     !appendString16(out, packet.saveSlotKey) ||
     !appendString16(out, packet.slotPath) ||
     !appendString16(out, packet.nativeSavePath) ||
     !appendString16(out, packet.slotDisplayName) ||
     !appendString16(out, packet.clientWorldName) ||
     !appendString16(out, packet.clientContentManifestHash)) {
    return {};
  }
  if(out.size() > MaxDatagramBytes)
    return {};
  return out;
}

inline ClientSessionControlDecodeResult decodeClientSessionControlPacket(std::string_view bytes) {
  ClientSessionControlDecodeResult result;
  if(bytes.size() < 4 + 2 + 2 + 2 + 2 + 8 + 8 + 8 + 4) {
    result.error = DecodeError::TooSmall;
    return result;
  }

  std::size_t at = 0;
  std::uint32_t magic = 0;
  std::uint16_t version = 0;
  std::uint16_t packetKind = 0;
  std::uint16_t flags = 0;
  std::uint16_t actionKind = 0;

  if(!readU32(bytes, at, magic) || magic != PacketMagic) {
    result.error = DecodeError::BadMagic;
    return result;
  }
  if(!readU16(bytes, at, version) || version != PacketVersion) {
    result.error = DecodeError::BadVersion;
    return result;
  }
  if(!readU16(bytes, at, packetKind) || packetKind != static_cast<std::uint16_t>(PacketKind::ClientSessionControl)) {
    result.error = DecodeError::BadPacketKind;
    return result;
  }
  if(!readU16(bytes, at, flags) ||
     !readU16(bytes, at, actionKind) ||
     !readU64(bytes, at, result.sessionControl.packetSequence) ||
     !readU64(bytes, at, result.sessionControl.clientTick) ||
     !readU64(bytes, at, result.sessionControl.localSequence) ||
     !readU32(bytes, at, result.sessionControl.flags) ||
     !readI64(bytes, at, result.sessionControl.serverTick) ||
     !readI64(bytes, at, result.sessionControl.acknowledgedLocalSequence) ||
     !readString16(bytes, at, result.sessionControl.sessionKey) ||
     !readString16(bytes, at, result.sessionControl.targetKey) ||
     !readString16(bytes, at, result.sessionControl.idempotencyKey) ||
     !readString16(bytes, at, result.sessionControl.source) ||
     !readString16(bytes, at, result.sessionControl.sourceLocation) ||
     !readString16(bytes, at, result.sessionControl.actorKey) ||
     !readString16(bytes, at, result.sessionControl.characterKey) ||
     !readString16(bytes, at, result.sessionControl.displayName) ||
     !readString16(bytes, at, result.sessionControl.world) ||
     !readString16(bytes, at, result.sessionControl.serverEndpoint) ||
     !readString16(bytes, at, result.sessionControl.reason) ||
     !readString16(bytes, at, result.sessionControl.actionKind) ||
     !readString16(bytes, at, result.sessionControl.manifestKey) ||
     !readString16(bytes, at, result.sessionControl.checkpointKind) ||
     !readString16(bytes, at, result.sessionControl.saveSlotKey) ||
     !readString16(bytes, at, result.sessionControl.slotPath) ||
     !readString16(bytes, at, result.sessionControl.nativeSavePath) ||
     !readString16(bytes, at, result.sessionControl.slotDisplayName) ||
     !readString16(bytes, at, result.sessionControl.clientWorldName)) {
    result.error = DecodeError::Truncated;
    return result;
  }
  if(at < bytes.size() && !readString16(bytes, at, result.sessionControl.clientContentManifestHash)) {
    result.error = DecodeError::Truncated;
    return result;
  }
  (void)flags;
  result.sessionControl.kind = static_cast<SemanticActionKind>(actionKind);
  if(!isSessionControlPacketAction(result.sessionControl.kind)) {
    result.error = DecodeError::BadActionKind;
    return result;
  }
  return result;
}

inline bool isDialogStatePacketAction(SemanticActionKind kind) noexcept {
  return kind == SemanticActionKind::SetKnownDialog ||
         kind == SemanticActionKind::RecordNpcDialogLine;
}

inline std::vector<std::uint8_t> encodeClientDialogStatePacket(const ClientDialogStatePacket& packet) {
  std::vector<std::uint8_t> out;
  out.reserve(packet.sessionKey.size() + packet.targetKey.size() + packet.idempotencyKey.size() +
              packet.npcKey.size() + packet.infoKey.size() + packet.subtitleText.size() + 420);

  appendU32(out, PacketMagic);
  appendU16(out, PacketVersion);
  appendU16(out, static_cast<std::uint16_t>(PacketKind::ClientDialogState));
  appendU16(out, 0);
  appendU16(out, static_cast<std::uint16_t>(packet.kind));
  appendU64(out, packet.packetSequence);
  appendU64(out, packet.clientTick);
  appendU64(out, packet.localSequence);
  appendU32(out, packet.flags);
  appendI64(out, packet.npcSymbol);
  appendI64(out, packet.infoSymbol);
  appendI64(out, packet.lineIndex);
  appendI64(out, packet.outputIndex);
  appendI64(out, packet.durationMs);
  if(!appendString16(out, packet.sessionKey) ||
     !appendString16(out, packet.targetKey) ||
     !appendString16(out, packet.idempotencyKey) ||
     !appendString16(out, packet.source) ||
     !appendString16(out, packet.actorKey) ||
     !appendString16(out, packet.characterKey) ||
     !appendString16(out, packet.world) ||
     !appendString16(out, packet.reason) ||
     !appendString16(out, packet.npcKey) ||
     !appendString16(out, packet.npcSymbolName) ||
     !appendString16(out, packet.infoKey) ||
     !appendString16(out, packet.infoSymbolName) ||
     !appendString16(out, packet.conversationKey) ||
     !appendString16(out, packet.syncGroup) ||
     !appendString16(out, packet.speakerKey) ||
     !appendString16(out, packet.listenerKey) ||
     !appendString16(out, packet.outputName) ||
     !appendString16(out, packet.messageName) ||
     !appendString16(out, packet.subtitleText) ||
     !appendString16(out, packet.dialogState) ||
     !appendString16(out, packet.topicKey)) {
    return {};
  }
  if(out.size() > MaxDatagramBytes)
    return {};
  return out;
}

inline ClientDialogStateDecodeResult decodeClientDialogStatePacket(std::string_view bytes) {
  ClientDialogStateDecodeResult result;
  if(bytes.size() < 4 + 2 + 2 + 2 + 2 + 8 + 8 + 8 + 4) {
    result.error = DecodeError::TooSmall;
    return result;
  }

  std::size_t at = 0;
  std::uint32_t magic = 0;
  std::uint16_t version = 0;
  std::uint16_t packetKind = 0;
  std::uint16_t flags = 0;
  std::uint16_t actionKind = 0;

  if(!readU32(bytes, at, magic) || magic != PacketMagic) {
    result.error = DecodeError::BadMagic;
    return result;
  }
  if(!readU16(bytes, at, version) || version != PacketVersion) {
    result.error = DecodeError::BadVersion;
    return result;
  }
  if(!readU16(bytes, at, packetKind) || packetKind != static_cast<std::uint16_t>(PacketKind::ClientDialogState)) {
    result.error = DecodeError::BadPacketKind;
    return result;
  }
  if(!readU16(bytes, at, flags) ||
     !readU16(bytes, at, actionKind) ||
     !readU64(bytes, at, result.dialogState.packetSequence) ||
     !readU64(bytes, at, result.dialogState.clientTick) ||
     !readU64(bytes, at, result.dialogState.localSequence) ||
     !readU32(bytes, at, result.dialogState.flags) ||
     !readI64(bytes, at, result.dialogState.npcSymbol) ||
     !readI64(bytes, at, result.dialogState.infoSymbol) ||
     !readI64(bytes, at, result.dialogState.lineIndex) ||
     !readI64(bytes, at, result.dialogState.outputIndex) ||
     !readI64(bytes, at, result.dialogState.durationMs) ||
     !readString16(bytes, at, result.dialogState.sessionKey) ||
     !readString16(bytes, at, result.dialogState.targetKey) ||
     !readString16(bytes, at, result.dialogState.idempotencyKey) ||
     !readString16(bytes, at, result.dialogState.source) ||
     !readString16(bytes, at, result.dialogState.actorKey) ||
     !readString16(bytes, at, result.dialogState.characterKey) ||
     !readString16(bytes, at, result.dialogState.world) ||
     !readString16(bytes, at, result.dialogState.reason) ||
     !readString16(bytes, at, result.dialogState.npcKey) ||
     !readString16(bytes, at, result.dialogState.npcSymbolName) ||
     !readString16(bytes, at, result.dialogState.infoKey) ||
     !readString16(bytes, at, result.dialogState.infoSymbolName) ||
     !readString16(bytes, at, result.dialogState.conversationKey) ||
     !readString16(bytes, at, result.dialogState.syncGroup) ||
     !readString16(bytes, at, result.dialogState.speakerKey) ||
     !readString16(bytes, at, result.dialogState.listenerKey) ||
     !readString16(bytes, at, result.dialogState.outputName) ||
     !readString16(bytes, at, result.dialogState.messageName) ||
     !readString16(bytes, at, result.dialogState.subtitleText) ||
     !readString16(bytes, at, result.dialogState.dialogState) ||
     !readString16(bytes, at, result.dialogState.topicKey)) {
    result.error = DecodeError::Truncated;
    return result;
  }
  (void)flags;
  result.dialogState.kind = static_cast<SemanticActionKind>(actionKind);
  if(!isDialogStatePacketAction(result.dialogState.kind)) {
    result.error = DecodeError::BadActionKind;
    return result;
  }
  return result;
}

inline bool isCharacterEventPacketAction(SemanticActionKind kind) noexcept {
  return kind == SemanticActionKind::AdjustProgression ||
         kind == SemanticActionKind::ApplyExperienceReward ||
         kind == SemanticActionKind::ApplyCharacterResourceDelta ||
         kind == SemanticActionKind::ConsumeMana;
}

inline std::vector<std::uint8_t> encodeClientCharacterEventPacket(const ClientCharacterEventPacket& packet) {
  std::vector<std::uint8_t> out;
  out.reserve(packet.sessionKey.size() + packet.targetKey.size() + packet.idempotencyKey.size() +
              packet.characterKey.size() + packet.resourceKey.size() + packet.reason.size() + 320);

  appendU32(out, PacketMagic);
  appendU16(out, PacketVersion);
  appendU16(out, static_cast<std::uint16_t>(PacketKind::ClientCharacterEvent));
  appendU16(out, 0);
  appendU16(out, static_cast<std::uint16_t>(packet.kind));
  appendU64(out, packet.packetSequence);
  appendU64(out, packet.clientTick);
  appendU64(out, packet.localSequence);
  appendU32(out, packet.flags);
  appendI64(out, packet.requestedDelta);
  appendI64(out, packet.requestedAmount);
  appendI64(out, packet.manaAmount);
  appendI64(out, packet.experienceReward);
  appendI64(out, packet.learningPointsReward);
  appendI64(out, packet.attributeSymbol);
  appendI64(out, packet.skillSymbol);
  appendF64(out, packet.actorPosX);
  appendF64(out, packet.actorPosY);
  appendF64(out, packet.actorPosZ);
  if(!appendString16(out, packet.sessionKey) ||
     !appendString16(out, packet.targetKey) ||
     !appendString16(out, packet.idempotencyKey) ||
     !appendString16(out, packet.source) ||
     !appendString16(out, packet.actorKey) ||
     !appendString16(out, packet.characterKey) ||
     !appendString16(out, packet.world) ||
     !appendString16(out, packet.reason) ||
     !appendString16(out, packet.resourceKey) ||
     !appendString16(out, packet.resourceDisplayName) ||
     !appendString16(out, packet.progressionKey) ||
     !appendString16(out, packet.rewardKey) ||
     !appendString16(out, packet.sourceActorKey) ||
     !appendString16(out, packet.sourceEntityKey)) {
    return {};
  }
  if(out.size() > MaxDatagramBytes)
    return {};
  return out;
}

inline ClientCharacterEventDecodeResult decodeClientCharacterEventPacket(std::string_view bytes) {
  ClientCharacterEventDecodeResult result;
  if(bytes.size() < 4 + 2 + 2 + 2 + 2 + 8 + 8 + 8 + 4) {
    result.error = DecodeError::TooSmall;
    return result;
  }

  std::size_t at = 0;
  std::uint32_t magic = 0;
  std::uint16_t version = 0;
  std::uint16_t packetKind = 0;
  std::uint16_t flags = 0;
  std::uint16_t actionKind = 0;

  if(!readU32(bytes, at, magic) || magic != PacketMagic) {
    result.error = DecodeError::BadMagic;
    return result;
  }
  if(!readU16(bytes, at, version) || version != PacketVersion) {
    result.error = DecodeError::BadVersion;
    return result;
  }
  if(!readU16(bytes, at, packetKind) || packetKind != static_cast<std::uint16_t>(PacketKind::ClientCharacterEvent)) {
    result.error = DecodeError::BadPacketKind;
    return result;
  }
  if(!readU16(bytes, at, flags) ||
     !readU16(bytes, at, actionKind) ||
     !readU64(bytes, at, result.characterEvent.packetSequence) ||
     !readU64(bytes, at, result.characterEvent.clientTick) ||
     !readU64(bytes, at, result.characterEvent.localSequence) ||
     !readU32(bytes, at, result.characterEvent.flags) ||
     !readI64(bytes, at, result.characterEvent.requestedDelta) ||
     !readI64(bytes, at, result.characterEvent.requestedAmount) ||
     !readI64(bytes, at, result.characterEvent.manaAmount) ||
     !readI64(bytes, at, result.characterEvent.experienceReward) ||
     !readI64(bytes, at, result.characterEvent.learningPointsReward) ||
     !readI64(bytes, at, result.characterEvent.attributeSymbol) ||
     !readI64(bytes, at, result.characterEvent.skillSymbol) ||
     !readF64(bytes, at, result.characterEvent.actorPosX) ||
     !readF64(bytes, at, result.characterEvent.actorPosY) ||
     !readF64(bytes, at, result.characterEvent.actorPosZ) ||
     !readString16(bytes, at, result.characterEvent.sessionKey) ||
     !readString16(bytes, at, result.characterEvent.targetKey) ||
     !readString16(bytes, at, result.characterEvent.idempotencyKey) ||
     !readString16(bytes, at, result.characterEvent.source) ||
     !readString16(bytes, at, result.characterEvent.actorKey) ||
     !readString16(bytes, at, result.characterEvent.characterKey) ||
     !readString16(bytes, at, result.characterEvent.world) ||
     !readString16(bytes, at, result.characterEvent.reason) ||
     !readString16(bytes, at, result.characterEvent.resourceKey) ||
     !readString16(bytes, at, result.characterEvent.resourceDisplayName) ||
     !readString16(bytes, at, result.characterEvent.progressionKey) ||
     !readString16(bytes, at, result.characterEvent.rewardKey) ||
     !readString16(bytes, at, result.characterEvent.sourceActorKey) ||
     !readString16(bytes, at, result.characterEvent.sourceEntityKey)) {
    result.error = DecodeError::Truncated;
    return result;
  }
  (void)flags;
  result.characterEvent.kind = static_cast<SemanticActionKind>(actionKind);
  if(!isCharacterEventPacketAction(result.characterEvent.kind)) {
    result.error = DecodeError::BadActionKind;
    return result;
  }
  return result;
}

inline std::vector<std::uint8_t> encodeServerAckPacket(const ServerAckPacket& ack) {
  std::vector<std::uint8_t> out;
  out.reserve(4 + 2 + 2 + 2 + 2 + 8 + 8);
  appendU32(out, PacketMagic);
  appendU16(out, PacketVersion);
  appendU16(out, static_cast<std::uint16_t>(PacketKind::ServerAck));
  std::uint16_t flags = 0;
  if(ack.accepted)
    flags |= 0x0001u;
  if(ack.ready)
    flags |= 0x0002u;
  appendU16(out, flags);
  appendU16(out, static_cast<std::uint16_t>(ack.kind));
  appendU64(out, ack.packetSequence);
  appendU64(out, ack.localSequence);
  return out;
}

inline ServerAckDecodeResult decodeServerAckPacket(std::string_view bytes) {
  ServerAckDecodeResult result;
  if(bytes.size() < 4 + 2 + 2 + 2 + 2 + 8 + 8) {
    result.error = DecodeError::TooSmall;
    return result;
  }

  std::size_t at = 0;
  std::uint32_t magic = 0;
  std::uint16_t version = 0;
  std::uint16_t packetKind = 0;
  std::uint16_t flags = 0;
  std::uint16_t ackKind = 0;

  if(!readU32(bytes, at, magic) || magic != PacketMagic) {
    result.error = DecodeError::BadMagic;
    return result;
  }
  if(!readU16(bytes, at, version) || version != PacketVersion) {
    result.error = DecodeError::BadVersion;
    return result;
  }
  if(!readU16(bytes, at, packetKind) || packetKind != static_cast<std::uint16_t>(PacketKind::ServerAck)) {
    result.error = DecodeError::BadPacketKind;
    return result;
  }
  if(!readU16(bytes, at, flags) ||
     !readU16(bytes, at, ackKind) ||
     !readU64(bytes, at, result.serverAck.packetSequence) ||
     !readU64(bytes, at, result.serverAck.localSequence)) {
    result.error = DecodeError::Truncated;
    return result;
  }

  result.serverAck.kind = static_cast<ServerAckKind>(ackKind);
  result.serverAck.accepted = (flags & 0x0001u) != 0;
  result.serverAck.ready = (flags & 0x0002u) != 0;
  return result;
}

inline std::vector<std::uint8_t> encodeServerSnapshotChunkPacket(const ServerSnapshotChunkPacket& chunk) {
  std::vector<std::uint8_t> out;
  out.reserve(chunk.payloadJsonFragment.size() + 40);
  appendU32(out, PacketMagic);
  appendU16(out, PacketVersion);
  appendU16(out, static_cast<std::uint16_t>(PacketKind::ServerSnapshotChunk));
  appendU16(out, 0);
  appendU16(out, 1);
  appendU64(out, chunk.packetSequence);
  appendU64(out, chunk.localSequence);
  appendU32(out, chunk.snapshotId);
  appendU16(out, chunk.chunkIndex);
  appendU16(out, chunk.chunkCount);
  appendU32(out, chunk.totalBytes);
  if(!appendString32(out, chunk.payloadJsonFragment))
    return {};
  if(out.size() > MaxDatagramBytes)
    return {};
  return out;
}

inline ServerSnapshotChunkDecodeResult decodeServerSnapshotChunkPacket(std::string_view bytes) {
  ServerSnapshotChunkDecodeResult result;
  if(bytes.size() < 4 + 2 + 2 + 2 + 2 + 8 + 8 + 4 + 2 + 2 + 4 + 4) {
    result.error = DecodeError::TooSmall;
    return result;
  }

  std::size_t at = 0;
  std::uint32_t magic = 0;
  std::uint16_t version = 0;
  std::uint16_t packetKind = 0;
  std::uint16_t flags = 0;
  std::uint16_t snapshotKind = 0;

  if(!readU32(bytes, at, magic) || magic != PacketMagic) {
    result.error = DecodeError::BadMagic;
    return result;
  }
  if(!readU16(bytes, at, version) || version != PacketVersion) {
    result.error = DecodeError::BadVersion;
    return result;
  }
  if(!readU16(bytes, at, packetKind) || packetKind != static_cast<std::uint16_t>(PacketKind::ServerSnapshotChunk)) {
    result.error = DecodeError::BadPacketKind;
    return result;
  }
  if(!readU16(bytes, at, flags) ||
     !readU16(bytes, at, snapshotKind) ||
     !readU64(bytes, at, result.snapshotChunk.packetSequence) ||
     !readU64(bytes, at, result.snapshotChunk.localSequence) ||
     !readU32(bytes, at, result.snapshotChunk.snapshotId) ||
     !readU16(bytes, at, result.snapshotChunk.chunkIndex) ||
     !readU16(bytes, at, result.snapshotChunk.chunkCount) ||
     !readU32(bytes, at, result.snapshotChunk.totalBytes) ||
     !readString32(bytes, at, result.snapshotChunk.payloadJsonFragment)) {
    result.error = DecodeError::Truncated;
    return result;
  }
  (void)flags;
  (void)snapshotKind;
  if(result.snapshotChunk.chunkCount == 0 ||
     result.snapshotChunk.chunkIndex >= result.snapshotChunk.chunkCount ||
     result.snapshotChunk.totalBytes > MaxPayloadBytes * 64u) {
    result.error = DecodeError::InvalidPayload;
    return result;
  }
  return result;
}

inline std::vector<std::uint8_t> encodeServerDiagnosticPacket(const ServerDiagnosticPacket& diag) {
  std::vector<std::uint8_t> out;
  out.reserve(diag.actionKind.size() + diag.reason.size() + diag.message.size() + 56);
  appendU32(out, PacketMagic);
  appendU16(out, PacketVersion);
  appendU16(out, static_cast<std::uint16_t>(PacketKind::ServerDiagnostic));
  appendU16(out, 0);
  appendU16(out, diag.severity);
  appendU64(out, diag.packetSequence);
  appendU64(out, diag.localSequence);
  if(!appendString16(out, diag.actionKind) ||
     !appendString16(out, diag.reason) ||
     !appendString32(out, diag.message)) {
    return {};
  }
  if(out.size() > MaxDatagramBytes)
    return {};
  return out;
}

inline ServerDiagnosticDecodeResult decodeServerDiagnosticPacket(std::string_view bytes) {
  ServerDiagnosticDecodeResult result;
  if(bytes.size() < 4 + 2 + 2 + 2 + 2 + 8 + 8 + 2 + 2 + 4) {
    result.error = DecodeError::TooSmall;
    return result;
  }

  std::size_t at = 0;
  std::uint32_t magic = 0;
  std::uint16_t version = 0;
  std::uint16_t packetKind = 0;
  std::uint16_t flags = 0;

  if(!readU32(bytes, at, magic) || magic != PacketMagic) {
    result.error = DecodeError::BadMagic;
    return result;
  }
  if(!readU16(bytes, at, version) || version != PacketVersion) {
    result.error = DecodeError::BadVersion;
    return result;
  }
  if(!readU16(bytes, at, packetKind) || packetKind != static_cast<std::uint16_t>(PacketKind::ServerDiagnostic)) {
    result.error = DecodeError::BadPacketKind;
    return result;
  }
  if(!readU16(bytes, at, flags) ||
     !readU16(bytes, at, result.diagnostic.severity) ||
     !readU64(bytes, at, result.diagnostic.packetSequence) ||
     !readU64(bytes, at, result.diagnostic.localSequence) ||
     !readString16(bytes, at, result.diagnostic.actionKind) ||
     !readString16(bytes, at, result.diagnostic.reason) ||
     !readString32(bytes, at, result.diagnostic.message)) {
    result.error = DecodeError::Truncated;
    return result;
  }
  (void)flags;
  return result;
}

inline std::vector<std::uint8_t> encodeServerLiveDeltaPacket(const ServerLiveDeltaPacket& delta) {
  std::vector<std::uint8_t> out;
  out.reserve(delta.actionKind.size() + delta.debugJson.size() + 160);
  appendU32(out, PacketMagic);
  appendU16(out, PacketVersion);
  appendU16(out, static_cast<std::uint16_t>(PacketKind::ServerLiveDelta));
  appendU16(out, 0);
  appendU16(out, static_cast<std::uint16_t>(delta.kind));
  appendU64(out, delta.packetSequence);
  appendU64(out, delta.localSequence);
  appendU64(out, delta.serverTick);
  appendU32(out, delta.debugJson.empty() ? (delta.flags & ~ServerLiveDeltaHasDebugPayload)
                                         : (delta.flags | ServerLiveDeltaHasDebugPayload));
  appendF64(out, delta.posX);
  appendF64(out, delta.posY);
  appendF64(out, delta.posZ);
  appendF64(out, delta.yaw);
  appendI32(out, delta.level);
  appendI32(out, delta.experience);
  appendI32(out, delta.experienceNext);
  appendI32(out, delta.learningPoints);
  appendI32(out, delta.healthCurrent);
  appendI32(out, delta.healthMax);
  appendI32(out, delta.manaCurrent);
  appendI32(out, delta.manaMax);
  appendI32(out, delta.strength);
  appendI32(out, delta.dexterity);
  appendI32(out, delta.guild);
  appendI32(out, delta.trueGuild);
  if(!appendString16(out, delta.actionKind) ||
     !appendString32(out, delta.debugJson)) {
    return {};
  }
  if(out.size() > MaxDatagramBytes)
    return {};
  return out;
}

inline ServerLiveDeltaDecodeResult decodeServerLiveDeltaPacket(std::string_view bytes) {
  ServerLiveDeltaDecodeResult result;
  if(bytes.size() < 4 + 2 + 2 + 2 + 2 + 8 + 8 + 8 + 4 + (8 * 4) + (4 * 12) + 2 + 4) {
    result.error = DecodeError::TooSmall;
    return result;
  }

  std::size_t at = 0;
  std::uint32_t magic = 0;
  std::uint16_t version = 0;
  std::uint16_t packetKind = 0;
  std::uint16_t flags = 0;
  std::uint16_t deltaKind = 0;

  if(!readU32(bytes, at, magic) || magic != PacketMagic) {
    result.error = DecodeError::BadMagic;
    return result;
  }
  if(!readU16(bytes, at, version) || version != PacketVersion) {
    result.error = DecodeError::BadVersion;
    return result;
  }
  if(!readU16(bytes, at, packetKind) || packetKind != static_cast<std::uint16_t>(PacketKind::ServerLiveDelta)) {
    result.error = DecodeError::BadPacketKind;
    return result;
  }
  if(!readU16(bytes, at, flags) ||
     !readU16(bytes, at, deltaKind) ||
     !readU64(bytes, at, result.liveDelta.packetSequence) ||
     !readU64(bytes, at, result.liveDelta.localSequence) ||
     !readU64(bytes, at, result.liveDelta.serverTick) ||
     !readU32(bytes, at, result.liveDelta.flags) ||
     !readF64(bytes, at, result.liveDelta.posX) ||
     !readF64(bytes, at, result.liveDelta.posY) ||
     !readF64(bytes, at, result.liveDelta.posZ) ||
     !readF64(bytes, at, result.liveDelta.yaw) ||
     !readI32(bytes, at, result.liveDelta.level) ||
     !readI32(bytes, at, result.liveDelta.experience) ||
     !readI32(bytes, at, result.liveDelta.experienceNext) ||
     !readI32(bytes, at, result.liveDelta.learningPoints) ||
     !readI32(bytes, at, result.liveDelta.healthCurrent) ||
     !readI32(bytes, at, result.liveDelta.healthMax) ||
     !readI32(bytes, at, result.liveDelta.manaCurrent) ||
     !readI32(bytes, at, result.liveDelta.manaMax) ||
     !readI32(bytes, at, result.liveDelta.strength) ||
     !readI32(bytes, at, result.liveDelta.dexterity) ||
     !readI32(bytes, at, result.liveDelta.guild) ||
     !readI32(bytes, at, result.liveDelta.trueGuild) ||
     !readString16(bytes, at, result.liveDelta.actionKind) ||
     !readString32(bytes, at, result.liveDelta.debugJson)) {
    result.error = DecodeError::Truncated;
    return result;
  }
  (void)flags;
  result.liveDelta.kind = static_cast<ServerLiveDeltaKind>(deltaKind);
  if(!result.liveDelta.debugJson.empty() && result.liveDelta.debugJson.front() != '{')
    result.error = DecodeError::InvalidPayload;
  return result;
}

inline const char* decodeErrorName(DecodeError error) noexcept {
  switch(error) {
    case DecodeError::None: return "none";
    case DecodeError::TooSmall: return "too_small";
    case DecodeError::BadMagic: return "bad_magic";
    case DecodeError::BadVersion: return "bad_version";
    case DecodeError::BadPacketKind: return "bad_packet_kind";
    case DecodeError::BadActionKind: return "bad_action_kind";
    case DecodeError::Truncated: return "truncated";
    case DecodeError::StringTooLong: return "string_too_long";
    case DecodeError::InvalidPayload: return "invalid_payload";
  }
  return "unknown";
}

} // namespace Mmo::Net















