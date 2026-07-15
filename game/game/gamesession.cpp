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

// rate 14.5 to 1
const uint64_t GameSession::multTime=14500;
const uint64_t GameSession::divTime =1000;

namespace {

float checkpointYawDelta(float a, float b) noexcept {
  float d = std::fabs(a - b);
  if(d > 360.f)
    d = std::fmod(d, 360.f);
  if(d > 180.f)
    d = 360.f - d;
  return d;
}

constexpr uint64_t MmoServerSnapshotPollInterval = 250;
constexpr uint64_t MmoServerSnapshotWaitingLogDelay = 10000;
constexpr uint64_t MmoServerLiveSnapshotPollInterval = 500;
constexpr uint64_t MmoNpcAuthoritySampleInterval = 2500;
constexpr uint64_t MmoNpcAuthoritySampleStaleInterval = 10000;
constexpr uint64_t MmoNpcAuthoritySamplePruneInterval = 60000;
constexpr float    MmoNpcAuthoritySampleRadius = 12000.f;
constexpr size_t   MmoNpcAuthoritySampleMaxPerSweep = 8;
constexpr float    MmoNpcAuthoritySaveSampleRadius = 60000.f;
constexpr size_t   MmoNpcAuthoritySaveSampleMaxPerSweep = 512;

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
    const Mmo::ClientPresentation::ClientEquipmentSlot slot) noexcept {
  using Source = Mmo::ClientPresentation::ClientEquipmentSlot;
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

[[nodiscard]] constexpr Npc::MmoPresentationLifeState presentationLifeState(
    const Mmo::ClientPresentation::ServerPresentationNpcLifeState state) noexcept {
  using State = Mmo::ClientPresentation::ServerPresentationNpcLifeState;
  switch(state) {
    case State::Alive: return Npc::MmoPresentationLifeState::Alive;
    case State::Unconscious: return Npc::MmoPresentationLifeState::Unconscious;
    case State::Dead: return Npc::MmoPresentationLifeState::Dead;
  }
  return Npc::MmoPresentationLifeState::Alive;
}

[[nodiscard]] constexpr Npc::MmoPresentationCombatAction
presentationCombatAction(
    const Mmo::ClientPresentation::ServerPresentationCombatActionKind action) noexcept {
  using Source = Mmo::ClientPresentation::ServerPresentationCombatActionKind;
  switch(action) {
    case Source::LightAttack:
      return Npc::MmoPresentationCombatAction::LightAttack;
    case Source::HeavyAttack:
      return Npc::MmoPresentationCombatAction::HeavyAttack;
    case Source::ComboAttack:
      return Npc::MmoPresentationCombatAction::ComboAttack;
    case Source::Parry:
      return Npc::MmoPresentationCombatAction::Parry;
    case Source::Dodge:
      return Npc::MmoPresentationCombatAction::Dodge;
    case Source::CancelAction:
      return Npc::MmoPresentationCombatAction::Cancel;
  }
  return Npc::MmoPresentationCombatAction::Cancel;
}

[[nodiscard]] constexpr Npc::MmoPresentationHitReaction
presentationHitReaction(
    const Mmo::ClientPresentation::ServerPresentationHitReactionKind reaction) noexcept {
  using Source = Mmo::ClientPresentation::ServerPresentationHitReactionKind;
  switch(reaction) {
    case Source::Light: return Npc::MmoPresentationHitReaction::Light;
    case Source::Heavy: return Npc::MmoPresentationHitReaction::Heavy;
    case Source::Blocked: return Npc::MmoPresentationHitReaction::Blocked;
    case Source::Knockback: return Npc::MmoPresentationHitReaction::Knockback;
    case Source::Knockdown: return Npc::MmoPresentationHitReaction::Knockdown;
  }
  return Npc::MmoPresentationHitReaction::Light;
}

void hashCombine(std::uint64_t& seed, std::uint64_t value) noexcept {
  seed ^= value + 0x9E3779B97F4A7C15ull + (seed << 6) + (seed >> 2);
}

std::uint64_t hashString(std::string_view value) noexcept {
  return static_cast<std::uint64_t>(std::hash<std::string_view>{}(value));
}

std::uint64_t quantizedFloatHash(float value, float scale) noexcept {
  return static_cast<std::uint64_t>(static_cast<std::int64_t>(std::lround(value / scale)));
}

std::uint64_t mmoInteractiveStateSignature(
    const Mmo::ClientPresentation::ServerPresentationInteractiveStateRecord& state) noexcept {
  std::uint64_t signature = 0x6B8B4567327B23C6ULL;
  hashCombine(signature, state.stateId);
  hashCombine(signature, state.flags);
  hashCombine(signature, state.user.world.id);
  hashCombine(signature, state.user.world.generation);
  hashCombine(signature, state.user.id);
  hashCombine(signature, state.user.generation);
  return signature;
}

std::uint64_t mmoMoverStateSignature(
    const Mmo::ClientPresentation::ServerPresentationMoverStateRecord& state) noexcept {
  std::uint64_t signature = 0x643C986966334873ULL;
  hashCombine(signature, static_cast<std::uint64_t>(state.phase));
  hashCombine(signature, state.keyframe);
  hashCombine(signature, state.normalizedProgress);
  hashCombine(signature, state.flags);
  return signature;
}

std::string_view waypointName(const WayPoint* waypoint) noexcept {
  if(waypoint == nullptr)
    return {};
  return waypoint->name;
}

std::string mmoNpcAuthorityCacheKey(const Npc& npc) {
  std::string out;
  out.reserve(48);
  out.append(std::to_string(npc.persistentId()));
  out.push_back(':');
  out.append(std::to_string(npc.instanceSymbol()));
  return out;
}

std::uint64_t mmoNpcAuthoritySignature(Npc& npc) noexcept {
  std::uint64_t out = 0xC0FFEE117ull;
  const auto pos = npc.position();
  hashCombine(out, npc.persistentId());
  hashCombine(out, npc.instanceSymbol());
  hashCombine(out, quantizedFloatHash(pos.x, 50.f));
  hashCombine(out, quantizedFloatHash(pos.y, 50.f));
  hashCombine(out, quantizedFloatHash(pos.z, 50.f));
  hashCombine(out, quantizedFloatHash(npc.rotationY(), 5.f));
  hashCombine(out, static_cast<std::uint64_t>(npc.attribute(ATR_HITPOINTS)));
  hashCombine(out, static_cast<std::uint64_t>(npc.attribute(ATR_HITPOINTSMAX)));
  hashCombine(out, static_cast<std::uint64_t>(npc.bodyStateMasked()));
  hashCombine(out, static_cast<std::uint64_t>(npc.weaponState()));
  hashCombine(out, npc.currentAiStateFunction());
  hashCombine(out, hashString(npc.currentAiStateName()));
  hashCombine(out, hashString(waypointName(npc.currentWayPoint())));
  hashCombine(out, hashString(waypointName(npc.currentTaPoint())));
  hashCombine(out, hashString(waypointName(npc.moveTargetWayPoint())));
  hashCombine(out, hashString(waypointName(npc.nextPathWayPoint())));
  hashCombine(out, hashString(waypointName(npc.finalPathWayPoint())));
  hashCombine(out, npc.remainingPathPointCount());
  hashCombine(out, static_cast<std::uint64_t>(npc.moveHint()));
  hashCombine(out, npc.isDead() ? 1u : 0u);
  hashCombine(out, npc.isUnconscious() ? 1u : 0u);
  hashCombine(out, npc.isDown() ? 1u : 0u);
  hashCombine(out, npc.isAttack() ? 1u : 0u);
  hashCombine(out, npc.isAttackAnim() ? 1u : 0u);
  if(auto* target = npc.target()) {
    hashCombine(out, target->persistentId());
    hashCombine(out, target->instanceSymbol());
    }
  if(auto* victim = npc.stateVictim()) {
    hashCombine(out, victim->persistentId());
    hashCombine(out, victim->instanceSymbol());
    }
  return out;
}

struct MmoNpcIdentity final {
  bool        valid = false;
  std::string world;
  std::size_t persistentId = std::size_t(-1);
  std::size_t symbolIndex = std::size_t(-1);
};

struct MmoNpcRoutineAuthorityApplyStats final {
  std::size_t applied = 0;
  std::size_t fallback = 0;
  std::size_t missingNpc = 0;
  std::size_t skipped = 0;
};

std::optional<std::size_t> parseSizeToken(std::string_view text) noexcept {
  if(text.empty())
    return std::nullopt;
  std::size_t value = 0;
  auto r = std::from_chars(text.data(), text.data() + text.size(), value);
  if(r.ec != std::errc{} || r.ptr != text.data() + text.size())
    return std::nullopt;
  return value;
}

MmoNpcIdentity parseMmoNpcEntityKey(std::string_view key) {
  MmoNpcIdentity out;
  constexpr std::string_view prefix = "npc:";
  if(key.substr(0, prefix.size()) != prefix)
    return out;
  auto rest = key.substr(prefix.size());
  auto marker = rest.find(":pid:");
  if(marker == std::string_view::npos)
    return out;
  out.world = std::string(rest.substr(0, marker));
  rest.remove_prefix(marker + 5);
  marker = rest.find(":sym:");
  if(marker == std::string_view::npos)
    return out;
  auto pid = parseSizeToken(rest.substr(0, marker));
  rest.remove_prefix(marker + 5);
  auto sym = parseSizeToken(rest);
  if(!pid || !sym)
    return out;
  out.persistentId = *pid;
  out.symbolIndex = *sym;
  out.valid = true;
  return out;
}

std::optional<std::size_t> parseScriptFunctionKey(std::string_view key) noexcept {
  constexpr std::string_view prefix = "script-fn:";
  if(key.substr(0, prefix.size()) != prefix)
    return std::nullopt;
  return parseSizeToken(key.substr(prefix.size()));
}

std::string waypointNameFromAuthorityKey(std::string_view key) {
  constexpr std::string_view prefix = "waypoint:";
  if(key.empty())
    return {};
  if(key.substr(0, prefix.size()) != prefix)
    return std::string(key);
  const auto tail = key.substr(prefix.size());
  const auto sep = tail.rfind(':');
  if(sep == std::string_view::npos)
    return {};
  return std::string(tail.substr(sep + 1));
}

Npc* findNpcByAuthorityKey(World& world, std::string_view key) {
  const auto identity = parseMmoNpcEntityKey(key);
  if(!identity.valid)
    return nullptr;

  for(uint32_t i = 0, count = world.npcCount(); i < count; ++i) {
    auto* npc = world.npcById(i);
    if(npc == nullptr || npc->isPlayer())
      continue;
    if(npc->persistentId() == identity.persistentId && npc->instanceSymbol() == identity.symbolIndex)
      return npc;
    }
  return nullptr;
}

MmoNpcRoutineAuthorityApplyStats applyMmoNpcRoutineAuthorityState(World& world,
                                                                  const Mmo::RestoreSnapshot::Result& snapshot) {
  MmoNpcRoutineAuthorityApplyStats stats;
  for(const auto& row : snapshot.npcRoutineStates) {
    auto* npc = findNpcByAuthorityKey(world, row.npcEntityKey);
    if(npc == nullptr) {
      ++stats.missingNpc;
      continue;
      }
    if(npc->isPlayer() || npc->isDead() || npc->isDown() || npc->isUnconscious()) {
      ++stats.skipped;
      continue;
      }

    const auto fn = parseScriptFunctionKey(row.scheduleKey);
    std::string waypoint = waypointNameFromAuthorityKey(row.currentWaypointKey);
    if(waypoint.empty())
      waypoint = waypointNameFromAuthorityKey(row.targetWaypointKey);

    if(fn && *fn != 0) {
      const bool started = npc->startState(ScriptFn(*fn), waypoint, gtime::endOfTime(), false);
      if(started) {
        ++stats.applied;
        continue;
        }
      }

    npc->resumeAiRoutine();
    ++stats.fallback;
    }
  return stats;
}

std::optional<Mmo::ServerBootstrapSnapshot> latestMmoBootstrapSnapshot() noexcept {
  try {
    // Pull newly completed snapshots from client_sandbox before consulting the
    // retained latest value. No production file polling is involved.
    (void)Mmo::drainServerBootstrapSnapshots();
    return Mmo::latestServerBootstrapSnapshot();
  } catch(...) {
    return std::nullopt;
  }
}

bool canReuseMmoDbContinuePreWorldSnapshot() noexcept {
  const auto& cmd = CommandLine::inst();
  if(!cmd.mmoClientUsesServer() || !cmd.mmoDbContinueWithoutNativeSave())
    return false;

  const auto snapshot = latestMmoBootstrapSnapshot();
  if(!snapshot)
    return false;
  const auto result = Mmo::RestoreSnapshot::parseAndValidateBootstrapSnapshot(
      snapshot->payload, cmd.mmoCharacterKey());
  if(!result.ok) {
    Log::e("MMO DB continue pre-world snapshot reuse rejected: ", result.message,
           " snapshot_id=", snapshot->snapshotId);
    return false;
  }
  if(result.snapshotSource != "db_save_checkpoint_v1") {
    Log::e("MMO DB continue pre-world snapshot reuse rejected: snapshot_source=",
           result.snapshotSource,
           " snapshot_id=", snapshot->snapshotId);
    return false;
  }

  Log::i("MMO DB continue pre-world snapshot reuse enabled",
         " world=", result.worldName,
         " manifest=", result.dbSaveCheckpointManifestUuid,
         " snapshot_id=", snapshot->snapshotId);
  return true;
}

bool loadMmoDbContinuePreWorldClock(gtime& out) noexcept {
  const auto& cmd = CommandLine::inst();
  if(!cmd.mmoClientUsesServer() || !cmd.mmoDbContinueWithoutNativeSave())
    return false;

  const auto snapshot = latestMmoBootstrapSnapshot();
  if(!snapshot)
    return false;
  const auto result = Mmo::RestoreSnapshot::parseAndValidateBootstrapSnapshot(
      snapshot->payload, cmd.mmoCharacterKey());
  if(!result.ok) {
    Log::e("MMO DB continue pre-world clock rejected: ", result.message,
           " snapshot_id=", snapshot->snapshotId);
    return false;
  }
  if(!result.worldClock.present)
    return false;

  out = gtime::fromInt(result.worldClock.currentWorldTimeMs);
  Log::i("MMO DB continue pre-world clock selected: world_time_ms=", result.worldClock.currentWorldTimeMs,
         " hour=", out.hour(),
         " minute=", out.minute(),
         " world=", result.worldClock.worldName,
         " snapshot_source=", result.snapshotSource,
         " snapshot_id=", snapshot->snapshotId);
  return true;
}

class ScopedMmoDbContinueVideoSuppression final {
  public:
    explicit ScopedMmoDbContinueVideoSuppression(bool enabled) noexcept : enabled(enabled) {
      if(enabled)
        Gothic::inst().pushMmoDbContinueVideoSuppression();
      }

    ScopedMmoDbContinueVideoSuppression(const ScopedMmoDbContinueVideoSuppression&) = delete;
    ScopedMmoDbContinueVideoSuppression& operator=(const ScopedMmoDbContinueVideoSuppression&) = delete;

    ~ScopedMmoDbContinueVideoSuppression() {
      if(enabled)
        Gothic::inst().popMmoDbContinueVideoSuppression();
      }

  private:
    bool enabled = false;
  };

QuestLog::Status toQuestStatus(std::uint8_t status) noexcept {
  switch(status) {
    case 2:  return QuestLog::Status::Success;
    case 3:  return QuestLog::Status::Failed;
    case 4:  return QuestLog::Status::Obsolete;
    default: return QuestLog::Status::Running;
    }
}

QuestLog::Section toQuestSection(std::uint8_t section) noexcept {
  return section == 1 ? QuestLog::Section::Note : QuestLog::Section::Mission;
}

Npc::PersistentStats toPersistentStats(const Mmo::RestoreSnapshot::CharacterStats& stats) noexcept {
  Npc::PersistentStats out;
  out.level = stats.level;
  out.experience = stats.experience;
  out.experienceNext = stats.experienceNext;
  out.learningPoints = stats.learningPoints;
  out.healthCurrent = stats.healthCurrent;
  out.healthMax = stats.healthMax;
  out.manaCurrent = stats.manaCurrent;
  out.manaMax = stats.manaMax;
  out.strength = stats.strength;
  out.dexterity = stats.dexterity;
  out.guild = stats.guild;
  out.trueGuild = stats.trueGuild;
  return out;
}

struct MmoWorldSnapshotApplyStats final {
  std::size_t removedWorldItems = 0;
  std::size_t alreadyAbsentWorldItems = 0;
  std::size_t skippedWorldItemDeltas = 0;
  std::size_t spawnedWorldItems = 0;
  std::size_t updatedWorldItems = 0;
  std::size_t skippedActiveWorldItems = 0;
  std::size_t authoritativeWindowLocalItems = 0;
  std::size_t authoritativeWindowPreservedItems = 0;
  std::size_t authoritativeWindowRemovedItems = 0;
  std::size_t authoritativeWindowSkipped = 0;
  std::size_t appliedInteractives = 0;
  std::size_t missingInteractives = 0;
  std::size_t appliedNpcLifecycle = 0;
  std::size_t missingNpcLifecycle = 0;
  std::size_t skippedNpcLifecycle = 0;
};

[[nodiscard]] bool isValidWorldItemPersistentId(std::size_t id) noexcept {
  return id != std::size_t(-1) && id <= std::numeric_limits<std::uint32_t>::max();
}

[[nodiscard]] bool isValidWorldItemSymbol(std::size_t symbol) noexcept {
  return symbol != std::size_t(-1);
}

[[nodiscard]] bool worldItemSymbolMatches(const Item& item, std::size_t symbol) noexcept {
  return !isValidWorldItemSymbol(symbol) || item.clsId() == symbol;
}

[[nodiscard]] bool isValidNpcPersistentId(std::size_t id) noexcept {
  return id != std::size_t(-1) && id <= std::numeric_limits<std::uint32_t>::max();
}

[[nodiscard]] bool isValidNpcSymbol(std::size_t symbol) noexcept {
  return symbol != std::size_t(-1) && symbol <= std::numeric_limits<std::uint32_t>::max();
}

[[nodiscard]] bool npcSymbolMatches(const Npc& npc, std::size_t symbol) noexcept {
  return !isValidNpcSymbol(symbol) || npc.instanceSymbol() == static_cast<std::uint32_t>(symbol);
}

Npc* findNpcByIdentity(World& world, std::size_t persistentId, std::size_t symbol) noexcept {
  for(std::uint32_t id = 0; ; ++id) {
    auto* npc = world.npcById(id);
    if(npc == nullptr)
      return nullptr;
    if(isValidNpcPersistentId(persistentId) && npc->persistentId() == static_cast<std::uint32_t>(persistentId) && npcSymbolMatches(*npc, symbol))
      return npc;
    }
}

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

[[nodiscard]] std::string mmoPresentationEntityKey(
    const Mmo::ClientPresentation::ServerPresentationEntityRecord& entity) {
  std::string value;
  value.reserve(96U);
  value.append("v2-entity:");
  appendMmoPresentationNumber(value, entity.handle.id);
  value.push_back(':');
  appendMmoPresentationNumber(value, entity.handle.generation);
  value.push_back(':');
  appendMmoPresentationNumber(value, entity.presentation.presentationId);
  value.push_back(':');
  appendMmoPresentationNumber(value, entity.presentation.archetypeId);
  value.push_back(':');
  appendMmoPresentationNumber(value, entity.presentation.revision);
  return value;
}

[[nodiscard]] constexpr Mmo::ClientPresentation::ServerEntityHandle
presentationRegistryHandle(
    const Mmo::ClientPresentation::ServerPresentationEntityHandle handle) noexcept {
  return {.id = handle.id, .generation = handle.generation};
}

[[nodiscard]] constexpr Mmo::ClientPresentation::ServerEntityKind
presentationRegistryKind(
    const Mmo::ClientPresentation::ServerPresentationEntityKind kind) noexcept {
  using Typed = Mmo::ClientPresentation::ServerPresentationEntityKind;
  using Legacy = Mmo::ClientPresentation::ServerEntityKind;
  switch(kind) {
    case Typed::LocalPlayer:  return Legacy::LocalPlayer;
    case Typed::RemotePlayer: return Legacy::RemotePlayer;
    case Typed::Npc:          return Legacy::Npc;
  }
  return Legacy::Npc;
}

struct MmoServerEntityMaterialization final {
  Npc* npc = nullptr;
  bool materializedByMmo = false;
};

Npc* findNpcByObjectToken(World& world, const std::uintptr_t token) noexcept {
  if(token == 0U)
    return nullptr;
  const auto count = world.npcCount();
  for(std::uint32_t id = 0; id < count; ++id) {
    auto* npc = world.npcById(id);
    if(reinterpret_cast<std::uintptr_t>(npc) == token)
      return npc;
  }
  return nullptr;
}

Npc* findNpcByPresentationIdentity(
    World& world,
    const Mmo::ClientPresentation::LocalNpcPresentationIdentity& local) noexcept {
  auto* npc = world.npcById(local.localNpcId);
  if(npc != nullptr &&
     (local.localObjectToken == 0U ||
      reinterpret_cast<std::uintptr_t>(npc) == local.localObjectToken)) {
    return npc;
  }
  return findNpcByObjectToken(world, local.localObjectToken);
}

MmoServerEntityMaterialization materializeMmoServerEntityNpc(
    World& world,
    GameScript& scripts,
    const Mmo::ClientPresentation::ClientPresentationCatalogRuntime* catalog,
    const Mmo::ClientPresentation::ServerPresentationEntityRecord& entity) noexcept {
  using Kind = Mmo::ClientPresentation::ServerPresentationEntityKind;
  if(entity.kind == Kind::LocalPlayer)
    return {world.player(), false};

  if(!entity.presentation.valid())
    return {};

  std::optional<std::uint32_t> catalogSymbol;
  if(catalog != nullptr && catalog->installed()) {
    auto instanceName = entity.kind == Kind::RemotePlayer
                            ? catalog->playerInstanceName(
                                  entity.presentation.archetypeId,
                                  entity.presentation.presentationId)
                            : catalog->npcInstanceName(
                                  entity.presentation.archetypeId,
                                  entity.presentation.presentationId);
    if(!instanceName.has_value() && entity.kind == Kind::RemotePlayer) {
      instanceName = catalog->npcInstanceName(
          entity.presentation.archetypeId,
          entity.presentation.presentationId);
    }
    if(instanceName.has_value()) {
      const auto symbol = scripts.findSymbolIndex(*instanceName);
      if(symbol != size_t(-1) &&
         symbol <= std::numeric_limits<std::uint32_t>::max()) {
        catalogSymbol = static_cast<std::uint32_t>(symbol);
      }
    }
  }

  const auto* localPlayer = world.player();
  const auto localPlayerSymbol = localPlayer != nullptr
                                     ? localPlayer->instanceSymbol()
                                     : 0U;
  const auto resolvedSymbol = catalogSymbol.has_value()
                                  ? catalogSymbol
                                  : resolveServerReplicaInstanceSymbol(
                                        presentationRegistryKind(entity.kind),
                                        entity.presentation.archetypeId,
                                        localPlayerSymbol);
  if(!resolvedSymbol.has_value() || !isValidNpcSymbol(*resolvedSymbol))
    return {};
  const auto instanceSymbol = *resolvedSymbol;

  try {
    const Tempest::Vec3 position{
        static_cast<float>(entity.transform.posX),
        static_cast<float>(entity.transform.posY),
        static_cast<float>(entity.transform.posZ)};
    auto* npc = world.addMmoServerReplica(instanceSymbol, position);
    if(npc == nullptr)
      return {};
    return {npc, true};
  } catch(const std::exception& error) {
    Log::e("MMO typed entity materialization failed: ", error.what());
  } catch(...) {
    Log::e("MMO typed entity materialization failed with an unknown exception");
  }
  return {};
}

[[nodiscard]] std::optional<Mmo::ClientPresentation::LocalNpcPresentationIdentity>
localMmoPresentationIdentity(
    World& world,
    Npc* npc,
    const Mmo::ClientPresentation::ServerEntityKind kind,
    const bool materializedByMmo) noexcept {
  using namespace Mmo::ClientPresentation;
  if(npc == nullptr ||
     ((kind == ServerEntityKind::LocalPlayer) != npc->isPlayer())) {
    return std::nullopt;
  }
  const auto localNpcId = world.npcId(npc);
  if(localNpcId == InvalidLocalNpcId)
    return std::nullopt;
  return LocalNpcPresentationIdentity{
      .localNpcId = localNpcId,
      .persistentId = npc->persistentId(),
      .instanceSymbol = npc->instanceSymbol(),
      .localObjectToken = reinterpret_cast<std::uintptr_t>(npc),
      .materializedByMmo = materializedByMmo,
  };
}

Npc* resolveMmoPresentationBinding(
    World& world,
    const Mmo::ClientPresentation::ServerEntityPresentationBinding& binding) noexcept {
  auto* npc = findNpcByPresentationIdentity(world, binding.local);
  if(npc == nullptr ||
     npc->persistentId() != binding.local.persistentId ||
     npc->instanceSymbol() != binding.local.instanceSymbol ||
     ((binding.kind == Mmo::ClientPresentation::ServerEntityKind::LocalPlayer) !=
      npc->isPlayer())) {
    return nullptr;
  }
  return npc;
}

Npc* findNpcByApproxPosition(World& world, std::size_t symbol, const Tempest::Vec3& pos) noexcept {
  if(!isValidNpcSymbol(symbol))
    return nullptr;

  constexpr float MaxPositionDeltaSq = 600.f * 600.f;
  Npc* best = nullptr;
  float bestDistance = MaxPositionDeltaSq;
  for(std::uint32_t id = 0; ; ++id) {
    auto* npc = world.npcById(id);
    if(npc == nullptr)
      return best;
    if(npc->instanceSymbol() != static_cast<std::uint32_t>(symbol))
      continue;
    const auto d = npc->position() - pos;
    const float dist = d.quadLength();
    if(dist < bestDistance) {
      bestDistance = dist;
      best = npc;
      }
    }
}

Item* findWorldItemByIdentity(World& world, std::size_t persistentId, std::size_t symbol) noexcept {
  if(!isValidWorldItemPersistentId(persistentId))
    return nullptr;

  const auto expectedPersistentId = static_cast<std::uint32_t>(persistentId);
  for(std::uint32_t id = 0; ; ++id) {
    auto* item = world.itmById(id);
    if(item == nullptr)
      return nullptr;
    if(item->persistentId() == expectedPersistentId && worldItemSymbolMatches(*item, symbol))
      return item;
    }
}

Item* findWorldItemByApproxPosition(World& world, std::size_t symbol, const Tempest::Vec3& pos) noexcept {
  if(!isValidWorldItemSymbol(symbol))
    return nullptr;

  constexpr float MaxPositionDeltaSq = 120.f * 120.f;
  Item* best = nullptr;
  float bestDistance = MaxPositionDeltaSq;
  for(std::uint32_t id = 0; ; ++id) {
    auto* item = world.itmById(id);
    if(item == nullptr)
      return best;
    if(item->clsId() != symbol)
      continue;
    const auto d = item->position() - pos;
    const float dist = d.quadLength();
    if(dist < bestDistance) {
      bestDistance = dist;
      best = item;
      }
    }
}

[[nodiscard]] bool worldItemMatchesServerActiveItem(const Item& item,
                                                    const Mmo::RestoreSnapshot::WorldInventoryItem& src) noexcept {
  if(!src.isActiveWorldItem())
    return false;
  if(isValidWorldItemPersistentId(src.persistentId) &&
     item.persistentId() == static_cast<std::uint32_t>(src.persistentId) &&
     worldItemSymbolMatches(item, src.symbolIndex))
    return true;

  if(!isValidWorldItemSymbol(src.symbolIndex) || item.clsId() != src.symbolIndex)
    return false;

  constexpr float MaxServerItemPositionMatchSq = 160.f * 160.f;
  const Tempest::Vec3 srcPos {static_cast<float>(src.x), static_cast<float>(src.y), static_cast<float>(src.z)};
  const auto d = item.position() - srcPos;
  return d.quadLength() <= MaxServerItemPositionMatchSq;
}

[[nodiscard]] bool worldItemIsInServerActiveSet(const Item& item,
                                                const std::vector<Mmo::RestoreSnapshot::WorldInventoryItem>& activeItems) noexcept {
  for(const auto& src : activeItems) {
    if(worldItemMatchesServerActiveItem(item, src))
      return true;
    }
  return false;
}

void clearNativeWorldItemsInAuthoritativeWindow(World& world,
                                                const Mmo::RestoreSnapshot::Result& result,
                                                MmoWorldSnapshotApplyStats& stats) {
  if(!result.activeWorldItemWindowPresent || !result.position.present) {
    ++stats.authoritativeWindowSkipped;
    return;
    }

  if(!std::isfinite(result.activeWorldItemRadius) || result.activeWorldItemRadius <= 0.0) {
    ++stats.authoritativeWindowSkipped;
    return;
    }

  constexpr double MaxReasonableAuthorityRadius = 30000.0;
  const float radius = static_cast<float>(std::min(result.activeWorldItemRadius, MaxReasonableAuthorityRadius));
  const float radiusSq = radius * radius;
  const Tempest::Vec3 center {static_cast<float>(result.position.x),
                              static_cast<float>(result.position.y),
                              static_cast<float>(result.position.z)};

  std::vector<Item*> toRemove;
  for(std::uint32_t id = 0; ; ++id) {
    auto* item = world.itmById(id);
    if(item == nullptr)
      break;

    const auto d = item->position() - center;
    if(d.quadLength() > radiusSq)
      continue;

    ++stats.authoritativeWindowLocalItems;
    if(worldItemIsInServerActiveSet(*item, result.activeWorldItems)) {
      ++stats.authoritativeWindowPreservedItems;
      continue;
      }

    toRemove.push_back(item);
    }

  for(auto* item : toRemove) {
    if(item == nullptr)
      continue;
    world.removeItem(*item);
    ++stats.authoritativeWindowRemovedItems;
    }
}

MmoWorldSnapshotApplyStats applyMmoWorldSnapshotState(World& world,
                                                     const Mmo::RestoreSnapshot::Result& result) {
  MmoWorldSnapshotApplyStats stats;

  clearNativeWorldItemsInAuthoritativeWindow(world, result, stats);

  for(const auto& delta : result.worldEntityDeltas) {
    if(!delta.isRemovedWorldItem())
      continue;
    if(!isValidWorldItemPersistentId(delta.persistentId)) {
      ++stats.skippedWorldItemDeltas;
      continue;
      }
    auto* item = findWorldItemByIdentity(world, delta.persistentId, delta.symbolIndex);
    if(item == nullptr) {
      ++stats.alreadyAbsentWorldItems;
      continue;
      }
    world.removeItem(*item);
    ++stats.removedWorldItems;
    }

  for(const auto& src : result.activeWorldItems) {
    if(!src.isActiveWorldItem()) {
      ++stats.skippedActiveWorldItems;
      continue;
      }

    const Tempest::Vec3 pos {static_cast<float>(src.x), static_cast<float>(src.y), static_cast<float>(src.z)};
    auto* item = findWorldItemByIdentity(world, src.persistentId, src.symbolIndex);
    if(item == nullptr)
      item = findWorldItemByApproxPosition(world, src.symbolIndex, pos);

    if(item != nullptr) {
      item->setPosition(pos.x, pos.y, pos.z);
      item->setCount(src.amount);
      ++stats.updatedWorldItems;
      continue;
      }

    item = world.addItem(src.symbolIndex, pos);
    if(item == nullptr) {
      ++stats.skippedActiveWorldItems;
      continue;
      }
    if(isValidWorldItemPersistentId(src.persistentId))
      item->setPersistentId(static_cast<std::uint32_t>(src.persistentId));
    item->setCount(src.amount);
    ++stats.spawnedWorldItems;
    }

  for(const auto& src : result.interactiveStates) {
    if(src.slotId == std::size_t(-1) || src.slotId > std::numeric_limits<std::uint32_t>::max()) {
      ++stats.missingInteractives;
      continue;
      }

    auto* interactive = world.mobsiById(static_cast<std::uint32_t>(src.slotId));
    if(interactive == nullptr) {
      ++stats.missingInteractives;
      continue;
      }

    const std::int32_t stateId = src.hasStateId ? src.stateId : interactive->stateId();
    const bool locked = src.hasLocked ? src.locked : interactive->isLocked();
    const bool cracked = src.hasCracked ? src.cracked : interactive->isCracked();
    interactive->restorePersistentState(stateId, locked, cracked);
    ++stats.appliedInteractives;
    }

  for(const auto& src : result.npcLifecycleStates) {
    if(!src.hasStableIdentity() || !src.isLifecycleRelevant()) {
      ++stats.skippedNpcLifecycle;
      continue;
      }

    Npc* npc = findNpcByIdentity(world, src.persistentId, src.symbolIndex);
    if(npc == nullptr && src.hasPosition) {
      const Tempest::Vec3 pos {static_cast<float>(src.x), static_cast<float>(src.y), static_cast<float>(src.z)};
      npc = findNpcByApproxPosition(world, src.symbolIndex, pos);
      }
    if(npc == nullptr) {
      ++stats.missingNpcLifecycle;
      continue;
      }

    const bool dead = src.lifecycleState == "dead" || src.lifecycleState == "removed" ||
                      src.lifecycleState == "disabled" || src.lifecycleState == "archived" ||
                      (src.hasHealthCurrent && src.healthCurrent <= 0);
    const std::int32_t hp = src.hasHealthCurrent ? src.healthCurrent : (dead ? 0 : -1);
    const std::int32_t hpMax = src.hasHealthMax ? src.healthMax : -1;
    npc->restorePersistentLifecycle(hp, hpMax, dead);
    ++stats.appliedNpcLifecycle;
    }

  return stats;
}

} // namespace

const char* GameSession::mmoActionCheckpointReason(const Npc& npc, uint64_t now) const noexcept {
  const auto& cmd = CommandLine::inst();
  const uint64_t interval = cmd.mmoActionCheckpointIntervalMs();
  if(interval == 0)
    return nullptr;

  const auto& prev = lastMmoActionCheckpoint;
  if(!prev.initialized)
    return "initial_checkpoint";

  if(now < prev.lastEmitTick + interval)
    return nullptr;

  const uint64_t forceInterval = cmd.mmoActionCheckpointForceIntervalMs();
  if(forceInterval != 0 && now >= prev.lastEmitTick + forceInterval)
    return "forced_checkpoint_keepalive";

  const bool statsChanged =
    prev.level              != npc.level() ||
    prev.experience         != npc.experience() ||
    prev.experienceNext     != npc.experienceNext() ||
    prev.learningPoints     != npc.learningPoints() ||
    prev.healthCurrent      != npc.attribute(ATR_HITPOINTS) ||
    prev.healthMax          != npc.attribute(ATR_HITPOINTSMAX) ||
    prev.manaCurrent        != npc.attribute(ATR_MANA) ||
    prev.manaMax            != npc.attribute(ATR_MANAMAX) ||
    prev.strength           != npc.attribute(ATR_STRENGTH) ||
    prev.dexterity          != npc.attribute(ATR_DEXTERITY) ||
    prev.guild              != npc.guild() ||
    prev.trueGuild          != npc.trueGuild() ||
    prev.permanentAttitude  != static_cast<int32_t>(npc.attitude()) ||
    prev.temporaryAttitude  != static_cast<int32_t>(npc.tempAttitude());
  if(statsChanged)
    return "stat_delta_checkpoint";

  const auto pos = npc.position();
  const float dx = pos.x - prev.posX;
  const float dy = pos.y - prev.posY;
  const float dz = pos.z - prev.posZ;
  const float minDistance = cmd.mmoActionCheckpointMinDistance();
  if(minDistance <= 0.f || dx*dx + dy*dy + dz*dz >= minDistance*minDistance)
    return minDistance <= 0.f ? "periodic_checkpoint" : "distance_delta_checkpoint";

  const float minYaw = cmd.mmoActionCheckpointMinYawDeg();
  if(minYaw > 0.f && checkpointYawDelta(npc.rotationY(), prev.yaw) >= minYaw)
    return "yaw_delta_checkpoint";

  return nullptr;
}

void GameSession::recordMmoActionCheckpointState(const Npc& npc, uint64_t now) noexcept {
  auto& out = lastMmoActionCheckpoint;
  const auto pos = npc.position();
  out.initialized = true;
  out.lastEmitTick = now;
  out.posX = pos.x;
  out.posY = pos.y;
  out.posZ = pos.z;
  out.yaw = npc.rotationY();
  out.level = npc.level();
  out.experience = npc.experience();
  out.experienceNext = npc.experienceNext();
  out.learningPoints = npc.learningPoints();
  out.healthCurrent = npc.attribute(ATR_HITPOINTS);
  out.healthMax = npc.attribute(ATR_HITPOINTSMAX);
  out.manaCurrent = npc.attribute(ATR_MANA);
  out.manaMax = npc.attribute(ATR_MANAMAX);
  out.strength = npc.attribute(ATR_STRENGTH);
  out.dexterity = npc.attribute(ATR_DEXTERITY);
  out.guild = npc.guild();
  out.trueGuild = npc.trueGuild();
  out.permanentAttitude = static_cast<int32_t>(npc.attitude());
  out.temporaryAttitude = static_cast<int32_t>(npc.tempAttitude());
}

const char* GameSession::mmoActionMovementProposalReason(const Npc& npc, uint64_t now) const noexcept {
  const auto& cmd = CommandLine::inst();
  const uint64_t interval = cmd.mmoActionMovementProposalIntervalMs();
  if(interval == 0)
    return nullptr;

  const auto& prev = lastMmoActionMovementProposal;
  if(!prev.initialized)
    return nullptr;

  if(now < prev.lastEmitTick + interval)
    return nullptr;

  const auto pos = npc.position();
  const float dx = pos.x - prev.posX;
  const float dy = pos.y - prev.posY;
  const float dz = pos.z - prev.posZ;
  const float minDistance = cmd.mmoActionMovementProposalMinDistance();
  if(minDistance <= 0.f || dx*dx + dy*dy + dz*dz >= minDistance*minDistance)
    return minDistance <= 0.f ? "periodic_movement_proposal" : "distance_delta_movement_proposal";

  const float minYaw = cmd.mmoActionMovementProposalMinYawDeg();
  if(minYaw > 0.f && checkpointYawDelta(npc.rotationY(), prev.yaw) >= minYaw)
    return "yaw_delta_movement_proposal";

  return nullptr;
}

void GameSession::recordMmoActionMovementProposalState(const Npc& npc, uint64_t now) noexcept {
  auto& out = lastMmoActionMovementProposal;
  const auto pos = npc.position();
  out.initialized = true;
  out.lastEmitTick = now;
  out.posX = pos.x;
  out.posY = pos.y;
  out.posZ = pos.z;
  out.yaw = npc.rotationY();
  out.healthCurrent = npc.attribute(ATR_HITPOINTS);
  out.healthMax = npc.attribute(ATR_HITPOINTSMAX);
  out.manaCurrent = npc.attribute(ATR_MANA);
  out.manaMax = npc.attribute(ATR_MANAMAX);
  out.inAir = npc.isInAir();
  out.falling = npc.isFalling();
  out.fallingDeep = npc.isFallingDeep();
  out.slide = npc.isSlide();
  out.jump = npc.isJump();
  out.jumpUp = npc.isJumpUp();
  out.swim = npc.isSwim();
  out.dive = npc.isDive();
  out.inWater = npc.isInWater();
}

void GameSession::tickMmoMovementProposal(Npc& npc, uint64_t now) noexcept {
  const auto& cmd = CommandLine::inst();
  if(cmd.mmoClientUsesServer() || cmd.mmoActionMovementProposalIntervalMs() == 0)
    return;

  if(!lastMmoActionMovementProposal.initialized) {
    recordMmoActionMovementProposalState(npc, now);
    return;
    }

  if(const char* reason = mmoActionMovementProposalReason(npc, now)) {
    const auto& prev = lastMmoActionMovementProposal;
    Mmo::Hooks::onCharacterMovementProposal(npc, prev.lastEmitTick, prev.posX, prev.posY, prev.posZ, prev.yaw,
                                            prev.healthCurrent, prev.healthMax, prev.manaCurrent, prev.manaMax,
                                            prev.inAir, prev.falling, prev.fallingDeep, prev.slide,
                                            prev.jump, prev.jumpUp, prev.swim, prev.dive, prev.inWater,
                                            "GameSession::tick", reason);
    recordMmoActionMovementProposalState(npc, now);
    }
}

void GameSession::emitMmoNpcAuthoritySamples(Npc& hero, uint64_t now, const char* reason, bool force,
                                             float radius, size_t maxPerSweep) noexcept {
  const auto& cmd = CommandLine::inst();
  if(!cmd.mmoClientUsesServer() || wrld == nullptr)
    return;

  auto& state = mmoNpcAuthoritySamples;
  if(!force && state.lastSweepTick != 0 && now - state.lastSweepTick < MmoNpcAuthoritySampleInterval)
    return;
  state.lastSweepTick = now;

  const auto center = hero.position();
  size_t emitted = 0;
  wrld->detectNpc(center, radius, [&](Npc& npc) {
    if(emitted >= maxPerSweep || npc.isPlayer())
      return;

    const auto key = mmoNpcAuthorityCacheKey(npc);
    const auto signature = mmoNpcAuthoritySignature(npc);
    auto& cached = state.observed[key];
    const bool changed = cached.lastEmitTick == 0 || cached.signature != signature;
    const bool stale = cached.lastEmitTick == 0 || now - cached.lastEmitTick >= MmoNpcAuthoritySampleStaleInterval;
    if(!force && !changed && !stale)
      return;

    Mmo::Hooks::onObservedNpcAuthorityState(npc, "GameSession::emitMmoNpcAuthoritySamples",
                                            reason != nullptr ? reason :
                                            (changed ? "observed_npc_authority_changed" : "observed_npc_authority_refresh"));
    cached.lastEmitTick = now;
    cached.signature = signature;
    ++emitted;
    });

  if(state.observed.size() > 1024) {
    for(auto it = state.observed.begin(); it != state.observed.end();) {
      if(now - it->second.lastEmitTick > MmoNpcAuthoritySamplePruneInterval)
        it = state.observed.erase(it);
      else
        ++it;
      }
    }
}

void GameSession::tickMmoNpcAuthoritySamples(Npc& hero, uint64_t now) noexcept {
  emitMmoNpcAuthoritySamples(hero, now, nullptr, false,
                             MmoNpcAuthoritySampleRadius,
                             MmoNpcAuthoritySampleMaxPerSweep);
}

void GameSession::HeroStorage::save(Npc& npc) {
  storage.clear();
  Tempest::MemWriter wr{storage};
  Serialize          sr{wr};
  sr.setEntry("hero");

  npc.save(sr,0,"/npc/");
  }

void GameSession::HeroStorage::putToWorld(World& owner, std::string_view wayPoint) const {
  if(storage.size()==0)
    return;
  Tempest::MemReader rd{storage};
  Serialize          sr{rd};
  sr.setEntry("hero");

  if(auto pl = owner.player()) {
    pl->load(sr,0,"/npc/");
    auto pos = owner.findPoint(wayPoint);
    if(pos==nullptr) {
       // freemine.zen
       pos = &owner.startPoint();
      }
    pl->attachToPoint(pos);
    } else {
    auto ptr = std::make_unique<Npc>(owner,-1,wayPoint);
    ptr->load(sr,0,"/npc/");
    owner.insertPlayer(std::move(ptr),wayPoint);
    }

  if(auto pl = owner.player()) {
    if(auto pos = pl->currentWayPoint()) {
      pl->setPosition (pos->position() );
      pl->setDirection(pos->direction());
      }
    if(pl->isInAir()) {
      pl->stopAnim("");
      pl->setAnim(Npc::Anim::Idle);
      }
    pl->clearSpeed();
    pl->updateTransform();
    }
  }


GameSession::GameSession(std::string file) : GameSession(std::move(file), StartupMode::NewGame) {
  }

GameSession::GameSession(std::string file, StartupMode startupMode) {
  const bool dbContinueRequested = startupMode == StartupMode::MmoDbContinue && CommandLine::inst().mmoClientUsesServer();
  const bool mmoServerFreshNewGame = startupMode == StartupMode::MmoServerFreshNewGame && CommandLine::inst().mmoClientUsesServer();
  mmoServerFreshNewGameSession = mmoServerFreshNewGame;
  const ScopedMmoDbContinueVideoSuppression suppressStartupVideos(dbContinueRequested);

  cam.reset(new Camera());

  Gothic::inst().setLoadingProgress(0);
  setupSettings();
  setTime(gtime(8,0));
  if(dbContinueRequested) {
    gtime dbContinueWorldTime;
    if(loadMmoDbContinuePreWorldClock(dbContinueWorldTime))
      setTime(dbContinueWorldTime);
    }

  vm.reset(new GameScript(*this));
  loadMmoClientPresentationCatalog();
  initPerceptions();

  setWorld(std::unique_ptr<World>(new World(*this,std::move(file),true,[&](int v){
    Gothic::inst().setLoadingProgress(int(v*0.55));
    })));

  vm->initDialogs();
  Gothic::inst().setLoadingProgress(70);

  const bool testMode=false;

  std::string_view hero = testMode ? "PC_ROCKEFELLER" : Gothic::inst().defaultPlayer();
  //std::string_view hero = "PC_ROCKEFELLER";
  //std::string_view hero = "PC_HERO";
  //std::string_view hero = "FireGolem";
  //std::string_view hero = "Dragon_Undead";
  //std::string_view hero = "Wolf";
  //std::string_view hero = "Sheep";
  //std::string_view hero = "Giant_Bug";
  //std::string_view hero = "OrcWarrior_Rest";
  //std::string_view hero = "Snapper";
  //std::string_view hero = "Lurker";
  //std::string_view hero = "Scavenger";
  //std::string_view hero = "StoneGolem";
  //std::string_view hero = "Waran";
  //std::string_view hero = "FireWaran";
  //std::string_view hero = "Bloodfly";
  //std::string_view hero = "Gobbo_Skeleton";
  //std::string_view hero = "Swampshark";
  if(!Gothic::inst().isBenchmarkMode())
    wrld->createPlayer(hero);
  wrld->postInit();

  if(!testMode)
    initScripts(true);

#if OPENGOTHIC_MMO_SQLITE_TOOLING
  if(CommandLine::inst().mmoSqliteCapturePreStartExit()) {
    const auto& cmd = CommandLine::inst();
    if(cmd.mmoSqlite().empty()) {
      Log::e("-mmo-sqlite-capture-pre-start-exit requires -mmo-sqlite <path>");
      std::exit(2);
      }

    mmoSqlite.reset(new MmoRuntimeSqlite(std::string(cmd.mmoSqlite()),
                                         cmd.mmoSqliteIntervalMs(),
                                         false,
                                         true,
                                         {}));
    if(!mmoSqlite->open(*this)) {
      Log::e("mmo sqlite pre-start baseline capture failed: ", std::string(cmd.mmoSqlite()));
      std::exit(2);
      }

    mmoSqlite->flush(*this);
    Log::i("mmo sqlite pre-start baseline captured before world start triggers: ", std::string(cmd.mmoSqlite()));
    mmoSqlite.reset();
    std::exit(0);
    }
#endif

  if(!mmoServerFreshNewGame) {
    const bool reuseDbContinueSnapshot = dbContinueRequested && canReuseMmoDbContinuePreWorldSnapshot();
    const char* restoreReason = dbContinueRequested ? "db_continue_baseline_loaded" : "new_game_pre_start_loaded";
    scheduleMmoServerSnapshotRestore(restoreReason, reuseDbContinueSnapshot);
    if(reuseDbContinueSnapshot) {
      Log::i("MMO server snapshot restore reusing pre-world DB continue snapshot");
    } else {
      const char* bootstrapSourceLocation = dbContinueRequested
          ? "game/game/gamesession.cpp:GameSession::GameSession(db-continue)"
          : "game/game/gamesession.cpp:GameSession::GameSession(new/pre-start)";
      Mmo::Hooks::onClientBootstrapRequest(*wrld,
                                           bootstrapSourceLocation,
                                           restoreReason);
    }
    waitForMmoServerSnapshotRestoreDuringLoad();
  } else {
    Log::i("MMO server-bound New Game: starting fresh local baseline without DB bootstrap snapshot");
  }

  if(dbContinueRequested) {
    Log::i("MMO DB continue baseline loaded: running existing-world startup trigger");
    wrld->triggerOnStart(false);
    const auto resumedNpcRoutines = wrld->resumeNpcRoutinesAfterServerRestore();
    Log::i("MMO DB continue startup NPC routines resumed: count=", resumedNpcRoutines);
    if(const auto bootstrap = latestMmoBootstrapSnapshot()) {
      auto snapshot = Mmo::RestoreSnapshot::parseAndValidateBootstrapSnapshot(
          bootstrap->payload, CommandLine::inst().mmoCharacterKey());
      if(snapshot.ok) {
        const auto npcAuthority = applyMmoNpcRoutineAuthorityState(*wrld, snapshot);
        Log::i("MMO DB continue startup NPC authority applied: routine_applied=", npcAuthority.applied,
               " routine_fallback=", npcAuthority.fallback,
               " missing_npc=", npcAuthority.missingNpc,
               " skipped=", npcAuthority.skipped,
               " records=", snapshot.npcRoutineStates.size(),
               " snapshot_source=", snapshot.snapshotSource,
               " snapshot_id=", bootstrap->snapshotId);
      }
    }
  } else {
    wrld->triggerOnStart(true);
  }
  cam->reset(wrld->player());
  Gothic::inst().setLoadingProgress(96);
  ticks = 1;
#if OPENGOTHIC_MMO_SQLITE_TOOLING
  if(!CommandLine::inst().mmoSqlite().empty()) {
    mmoSqlite.reset(new MmoRuntimeSqlite(std::string(CommandLine::inst().mmoSqlite()),
                                         CommandLine::inst().mmoSqliteIntervalMs(),
                                         CommandLine::inst().mmoSqliteRestore(),
                                         CommandLine::inst().mmoSqliteCaptureBaseline(),
                                         {}));
    mmoSqlite->open(*this);
    }
#endif
  // wrld->setDayTime(8,0);
  }

GameSession::GameSession(Serialize &fin, std::string sourceSlot) {
  Gothic::inst().setLoadingProgress(0);
  setupSettings();

  SaveGameHeader hdr;
  fin.setEntry("header");
  fin.read(hdr);
  fin.setGlobalVersion(hdr.version);

  {
  uint16_t wssSize=0;
  fin.read(wssSize);
  visitedWorlds.resize(wssSize);
  for(size_t i=0; i<wssSize; ++i)
    fin.read(visitedWorlds[i].name);
  for(size_t i=0; i<wssSize; ++i)
    visitedWorlds[i].load(fin);
  }

  std::string    wname;
  fin.setEntry("game/session");
  fin.read(ticks,wrldTime,wrldTimePart,wname);

  cam.reset(new Camera());
  vm.reset(new GameScript(*this));
  loadMmoClientPresentationCatalog();
  vm->initDialogs();

  if(true) {
    setWorld(std::unique_ptr<World>(new World(*this,wname,false,[&](int v){
      Gothic::inst().setLoadingProgress(int(v*0.55));
      })));
    wrld->load(fin);
    }

  Gothic::inst().setLoadingProgress(70);

  if(fin.setEntry("game/perc"))
    vm->loadPerc(fin); else
    initPerceptions();

  fin.setEntry("game/quests");
  vm->loadQuests(fin);

  fin.setEntry("game/daedalus");
  vm->loadVar(fin);

  if(auto hero = wrld->player())
    vm->setInstanceNPC("HERO",*hero);

  fin.setEntry("game/camera");
  cam->load(fin,wrld->player());
  Gothic::inst().setLoadingProgress(96);
#if OPENGOTHIC_MMO_SQLITE_TOOLING
  if(!CommandLine::inst().mmoSqlite().empty()) {
    mmoSqlite.reset(new MmoRuntimeSqlite(std::string(CommandLine::inst().mmoSqlite()),
                                         CommandLine::inst().mmoSqliteIntervalMs(),
                                         CommandLine::inst().mmoSqliteRestore(),
                                         CommandLine::inst().mmoSqliteCaptureBaseline(),
                                         std::move(sourceSlot)));
    mmoSqlite->open(*this);
    }
#endif
  scheduleMmoServerSnapshotRestore("save_session_loaded");
  Mmo::Hooks::onClientBootstrapRequest(*wrld,
                                       "game/game/gamesession.cpp:GameSession::GameSession(save)",
                                       "save_session_loaded");
  waitForMmoServerSnapshotRestoreDuringLoad();
  }

GameSession::~GameSession() {
#if OPENGOTHIC_MMO_SQLITE_TOOLING
  if(mmoSqlite!=nullptr)
    mmoSqlite->flush(*this);
#endif
  }

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

void GameSession::save(Serialize &fout, std::string_view name, const Pixmap& screen) {
  SaveGameHeader hdr;
  hdr.version   = Serialize::Version::Current;
  hdr.name      = name;
  hdr.world     = wrld->name();
  {
  time_t now = std::time(nullptr);
  tm*    tp  = std::localtime(&now);
  hdr.pcTime = *tp;
  }
  hdr.wrldTime  = wrldTime;
  hdr.playTime  = ticks;
  hdr.isGothic2 = Gothic::inst().version().game;

  fout.setEntry("header");
  fout.write(hdr);
  {
  uint16_t wssSize = uint16_t(visitedWorlds.size());
  fout.write(wssSize);
  for(auto& i:visitedWorlds)
    fout.write(i.name);
  }

  fout.setEntry("preview.jpg");
  fout.write(std::tie(screen,"jpg"));

  fout.setEntry("game/session");
  fout.write(ticks,wrldTime,wrldTimePart,wrld->name());

  fout.setEntry("game/camera");
  cam->save(fout);
  Gothic::inst().setLoadingProgress(5);

  for(auto& i:visitedWorlds) {
    fout.setEntry("worlds/",i.name);
    i.save(fout);
    }
  Gothic::inst().setLoadingProgress(25);

  wrld->save(fout);
  Gothic::inst().setLoadingProgress(60);

  fout.setEntry("game/perc");
  vm->savePerc(fout);

  fout.setEntry("game/quests");
  vm->saveQuests(fout);

  fout.setEntry("game/daedalus");
  vm->saveVar(fout);
  Gothic::inst().setLoadingProgress(80);

  if(wrld != nullptr && CommandLine::inst().mmoClientUsesServer()) {
    if(auto* hero = wrld->player()) {
      emitMmoNpcAuthoritySamples(*hero, ticks, "native_save_pre_manifest_npc_authority", true,
                                 MmoNpcAuthoritySaveSampleRadius,
                                 MmoNpcAuthoritySaveSampleMaxPerSweep);
      Mmo::Hooks::onCharacterCheckpoint(*hero, "GameSession::save", "native_save_pre_manifest_checkpoint");
      }
    }
  }

void GameSession::recordMmoSaveSlot(std::string_view slotPath, std::string_view displayName) {
#if OPENGOTHIC_MMO_SQLITE_TOOLING
  if(mmoSqlite!=nullptr)
    mmoSqlite->recordSaveSlot(*this, slotPath, displayName);
#endif

  if(wrld != nullptr && CommandLine::inst().mmoClientUsesServer()) {
    if(auto* hero = wrld->player())
      emitMmoNpcAuthoritySamples(*hero, ticks, "native_save_slot_recorded_npc_authority", true,
                                 MmoNpcAuthoritySaveSampleRadius,
                                 MmoNpcAuthoritySaveSampleMaxPerSweep);
    Mmo::Hooks::onSaveCheckpointManifest(*wrld,
                                         slotPath,
                                         displayName,
                                         "GameSession::recordMmoSaveSlot",
                                         "native_save_slot_recorded");
    }
  }

void GameSession::setupSettings() {
  const float soundVolume = Gothic::settingsSoundVolume();
  sound.setGlobalVolume(soundVolume);
  }

void GameSession::releaseMmoServerPresentationBinding(
    const Mmo::ClientPresentation::ServerEntityPresentationBinding& binding) noexcept {
  if(binding.kind == Mmo::ClientPresentation::ServerEntityKind::LocalPlayer) {
    mmoMovementCorrectionBoundary.unbindLocalPlayer();
    return;
  }
  if(wrld == nullptr)
    return;
  auto* npc = findNpcByPresentationIdentity(*wrld, binding.local);
  if(npc == nullptr || npc->persistentId() != binding.local.persistentId ||
     npc->instanceSymbol() != binding.local.instanceSymbol)
    return;
  if(binding.local.materializedByMmo) {
    wrld->removeNpc(*npc);
    return;
  }
  npc->setMmoServerReplica(false);
}

void GameSession::resetMmoServerPresentationProjection() noexcept {
  if(!mmoPresentationRouteKey.empty())
    Gothic::inst().resetTypedServerDialogPresentation();

  ++mmoPresentationWorldGeneration;
  if(mmoPresentationWorldGeneration == 0U)
    mmoPresentationWorldGeneration = 1U;

  auto released =
      mmoServerEntityPresentation.resetRoute(mmoPresentationWorldGeneration);
  for(const auto& binding : released)
    releaseMmoServerPresentationBinding(binding);

  mmoServerEntityInterpolator.resetRoute(mmoPresentationWorldGeneration);
  mmoMovementCorrectionBoundary.resetRoute(mmoPresentationWorldGeneration);
  mmoServerInventoryPresentation_.reset();
  mmoServerWorldObjects.resetRoute({});
  mmoServerEntitySamples.clear();
}

void GameSession::resetMmoServerPresentationWorld() noexcept {
  resetMmoServerPresentationProjection();
  mmoTypedServerPresentation.reset();
  mmoPresentationRouteKey.clear();
}

void GameSession::setWorld(std::unique_ptr<World> &&w) {
  resetMmoServerPresentationWorld();
  if(wrld) {
    if(!isWorldKnown(wrld->name()))
      visitedWorlds.emplace_back(*wrld);
    }
  wrld = std::move(w);
  lastMmoActionCheckpoint = {};
  lastMmoActionMovementProposal = {};
  }

std::unique_ptr<World> GameSession::clearWorld() {
  resetMmoServerPresentationWorld();
  if(wrld) {
    if(!isWorldKnown(wrld->name())) {
      visitedWorlds.emplace_back(*wrld);
      }
    }
  lastMmoActionCheckpoint = {};
  lastMmoActionMovementProposal = {};
  return std::move(wrld);
  }

void GameSession::changeWorld(std::string_view world, std::string_view wayPoint) {
  chWorld.zen = world;
  chWorld.wp  = wayPoint;
  }

void GameSession::exitSession() {
  exitSessionFlg=true;
  }

const VersionInfo& GameSession::version() const {
  return Gothic::inst().version();
  }

WorldView *GameSession::view() const {
  if(wrld)
    return wrld->view();
  return nullptr;
  }

Tempest::SoundEffect GameSession::loadSound(const Tempest::Sound &raw) {
  try {
    return sound.load(raw);
    }
  catch(std::bad_alloc&) {
    Tempest::Log::d("Exceeding OpenAL source limit");
    return Tempest::SoundEffect();
    }
  }

Tempest::SoundEffect GameSession::loadSound(const SoundFx &fx, bool& looped) {
  try {
    return fx.load(sound,looped);
    }
  catch(std::bad_alloc&) {
    Tempest::Log::d("Exceeding OpenAL source limit");
    return Tempest::SoundEffect();
    }
  }

Npc* GameSession::player() {
  if(wrld)
    return wrld->player();
  return nullptr;
  }

void GameSession::scheduleMmoServerSnapshotRestore(std::string_view reason,
                                                   bool reuseExistingSnapshot) noexcept {
  const auto& cmd = CommandLine::inst();
  if(!cmd.mmoClientUsesServer())
    return;

  (void)Mmo::drainServerBootstrapSnapshots();
  const auto latest = Mmo::latestServerBootstrapSnapshot();

  mmoServerSnapshotRestore = {};
  mmoServerSnapshotRestore.requested = true;
  mmoServerSnapshotRestore.requestedAtTick = ticks;
  mmoServerSnapshotRestore.reason = std::string(reason);
  mmoServerSnapshotRestore.minimumSnapshotIdExclusive =
      reuseExistingSnapshot || !latest ? 0U : latest->snapshotId;

  Log::i("MMO server snapshot restore scheduled: transport=client_sandbox_mailbox",
         " strict_db_checkpoint=", cmd.mmoRequireDbSaveCheckpointRestore() ? 1 : 0,
         " reuse_existing_snapshot=", reuseExistingSnapshot ? 1 : 0,
         " minimum_snapshot_id_exclusive=",
         mmoServerSnapshotRestore.minimumSnapshotIdExclusive,
         " reason=", std::string(reason));
}

bool GameSession::tryApplyMmoServerSnapshotRestore(bool forcePoll) noexcept {
  auto& state = mmoServerSnapshotRestore;
  if(!state.requested || state.completed)
    return state.completed;
  if(!forcePoll && ticks < state.lastPollTick + MmoServerSnapshotPollInterval)
    return false;
  state.lastPollTick = ticks;

  const auto& cmd = CommandLine::inst();
  (void)Mmo::drainServerBootstrapSnapshots();
  const auto snapshot = Mmo::latestServerBootstrapSnapshot();
  if(!snapshot || snapshot->snapshotId <= state.minimumSnapshotIdExclusive) {
    if(!state.waitingLogged &&
       ticks >= state.requestedAtTick + MmoServerSnapshotWaitingLogDelay) {
      state.waitingLogged = true;
      Log::i("MMO server snapshot restore waiting for client_sandbox mailbox",
             " minimum_snapshot_id_exclusive=",
             state.minimumSnapshotIdExclusive);
    }
    return false;
  }

  const auto result = Mmo::RestoreSnapshot::parseAndValidateBootstrapSnapshot(
      snapshot->payload, cmd.mmoCharacterKey());
  if(!result.ok) {
    Log::e("MMO server snapshot restore rejected: ", result.message,
           " reason=", state.reason,
           " snapshot_id=", snapshot->snapshotId);
    state.lastAppliedSnapshotId =
        std::max(state.lastAppliedSnapshotId, snapshot->snapshotId);
    state.completed = true;
    return true;
  }

  const bool restoredFromDbSaveCheckpoint = result.snapshotSource == "db_save_checkpoint_v1";
  if(cmd.mmoRequireDbSaveCheckpointRestore() && !restoredFromDbSaveCheckpoint) {
    Log::e("MMO server snapshot restore rejected: strict DB save checkpoint restore required",
           " source=", result.source,
           " snapshot_source=", result.snapshotSource,
           " manifest_uuid=", result.dbSaveCheckpointManifestUuid,
           " reason=", state.reason,
           " snapshot_id=", snapshot->snapshotId);
    state.completed = true;
    return true;
    }

  Log::i("MMO server snapshot restore source: source=", result.source,
         " snapshot_id=", snapshot->snapshotId,
         " snapshot_source=", result.snapshotSource,
         " db_checkpoint=", restoredFromDbSaveCheckpoint ? 1 : 0,
         " manifest_uuid=", result.dbSaveCheckpointManifestUuid);

  if(result.worldClock.present) {
    const auto restoredTime = gtime::fromInt(result.worldClock.currentWorldTimeMs);
    setTime(restoredTime);
    Log::i("MMO server snapshot world clock applied: world_time_ms=", result.worldClock.currentWorldTimeMs,
           " hour=", restoredTime.hour(),
           " minute=", restoredTime.minute(),
           " tick=", result.worldClock.currentTick,
           " world=", result.worldClock.worldName);
    }

  auto* hero = player();
  if(hero == nullptr) {
    Log::e("MMO server snapshot restore failed: player is not available");
    state.completed = true;
    return true;
    }

  Mmo::Hooks::ScopedCaptureSuppression suppressServerMaterializationEcho;

  if(cmd.mmoServerSnapshotApplyStats()) {
    if(result.stats.present) {
      hero->restorePersistentStats(toPersistentStats(result.stats));
      Log::i("MMO server snapshot stats applied: level=", result.stats.level,
             " exp=", result.stats.experience,
             " lp=", result.stats.learningPoints,
             " hp=", result.stats.healthCurrent, "/", result.stats.healthMax,
             " mana=", result.stats.manaCurrent, "/", result.stats.manaMax);
    } else {
      Log::e("MMO server snapshot stats skipped: stats object is missing or invalid");
    }
    }

  if(cmd.mmoServerSnapshotApplyInventory()) {
    std::vector<Npc::PersistentInventoryItem> items;
    items.reserve(result.items.size());
    for(const auto& item : result.items) {
      if(item.symbolIndex == size_t(-1) || item.count == 0)
        continue;
      items.push_back({item.symbolIndex, item.count, item.equipped});
      }
    hero->restorePersistentInventory(items);
    Log::i("MMO server snapshot inventory applied: inventory=", result.inventoryCount,
           " equipment=", result.equipmentCount,
           " restore_items=", items.size(),
           " reason=", state.reason,
           " snapshot_id=", snapshot->snapshotId);
    }

  if(cmd.mmoServerSnapshotApplyPosition()) {
    if(result.position.present) {
      hero->setPosition(static_cast<float>(result.position.x),
                        static_cast<float>(result.position.y),
                        static_cast<float>(result.position.z));
      hero->setDirectionY(static_cast<float>(result.position.yaw));
      hero->clearSpeed();
      hero->updateTransform();
      Log::i("MMO server snapshot position applied: x=", result.position.x,
             " y=", result.position.y,
             " z=", result.position.z,
             " yaw=", result.position.yaw,
             " tick=", result.position.serverTick);
    } else {
      Log::e("MMO server snapshot position skipped: position object is missing or invalid");
    }
    }

  if(cmd.mmoServerSnapshotApplyStory()) {
    if(auto* gameScript = script()) {
      std::vector<QuestLog::Quest> quests;
      quests.reserve(result.quests.size());
      for(const auto& src : result.quests) {
        QuestLog::Quest quest;
        quest.name = src.name;
        quest.section = toQuestSection(src.section);
        quest.status = toQuestStatus(src.status);
        quest.entry = src.entries;
        quests.push_back(std::move(quest));
        }

      std::set<std::pair<size_t, size_t>> dialogs;
      for(const auto& dialog : result.knownDialogs) {
        if(dialog.known && dialog.npcSymbol != size_t(-1) && dialog.infoSymbol != size_t(-1))
          dialogs.emplace(dialog.npcSymbol, dialog.infoSymbol);
        }

      size_t appliedQuests = quests.size();
      size_t appliedDialogs = dialogs.size();
      const bool preserveLocalStory = state.storyDirtySinceRequest;
      if(preserveLocalStory) {
        appliedQuests = gameScript->mergeQuestLogForPersistence(std::move(quests));
        appliedDialogs = gameScript->mergeKnownDialogsForPersistence(dialogs);
        }
      else {
        gameScript->restoreQuestLogForPersistence(std::move(quests));
        gameScript->restoreKnownDialogsForPersistence(std::move(dialogs));
        }

      size_t appliedScriptInts = 0;
      if(!preserveLocalStory) {
        for(const auto& value : result.scriptInts) {
          if(value.symbolIndex == size_t(-1))
            continue;
          if(gameScript->restoreGlobalIntForPersistence(value.symbolIndex, value.valueIndex, value.value))
            ++appliedScriptInts;
          }
        }
      else if(!result.scriptInts.empty()) {
        Log::i("MMO server snapshot script state skipped: local story changed before snapshot arrived",
               " script_ints=", result.scriptIntCount,
               " parsed=", result.scriptInts.size());
        }

      Log::i("MMO server snapshot story applied: mode=", preserveLocalStory ? "merge_preserve_local" : "replace_from_server",
             " quests=", result.questCount,
             " restore_quests=", appliedQuests,
             " known_dialogs=", result.knownDialogCount,
             " restore_known_dialogs=", appliedDialogs,
             " script_ints=", result.scriptIntCount,
             " restore_script_ints=", appliedScriptInts,
             " script_truncated=", result.scriptStateTruncated ? 1 : 0);
    } else {
      Log::e("MMO server snapshot story skipped: script VM is not available");
    }
    }

  if(cmd.mmoServerSnapshotApplyWorldState()) {
    try {
      const auto applied = applyMmoWorldSnapshotState(*wrld, result);
      size_t appliedMovers = 0;
      size_t missingMovers = 0;
      size_t skippedMovers = 0;
      for(const auto& mover : result.moverStates) {
        if(!mover.hasFrameIndex) {
          ++skippedMovers;
          continue;
          }
        const auto targetFrame = mover.hasTargetFrameIndex ? mover.targetFrameIndex : -1;
        if(wrld->restoreMoverState(mover.moverKey, mover.stateAfter, mover.frameIndex, targetFrame))
          ++appliedMovers;
        else
          ++missingMovers;
        }
      Log::i("MMO server snapshot world state applied: world_item_deltas=", result.worldItemDeltaCount,
             " parsed_world_deltas=", result.worldEntityDeltas.size(),
             " removed_world_items=", applied.removedWorldItems,
             " already_absent_world_items=", applied.alreadyAbsentWorldItems,
             " skipped_world_item_deltas=", applied.skippedWorldItemDeltas,
             " active_world_items=", result.activeWorldItems.size(),
             " spawned_world_items=", applied.spawnedWorldItems,
             " updated_world_items=", applied.updatedWorldItems,
             " skipped_active_world_items=", applied.skippedActiveWorldItems,
             " authoritative_window=", result.activeWorldItemWindowPresent ? 1 : 0,
             " authoritative_radius=", result.activeWorldItemRadius,
             " local_items_in_window=", applied.authoritativeWindowLocalItems,
             " preserved_local_items=", applied.authoritativeWindowPreservedItems,
             " removed_local_items_not_in_db=", applied.authoritativeWindowRemovedItems,
             " skipped_authoritative_window=", applied.authoritativeWindowSkipped,
             " nearby_npcs=", result.nearbyNpcCount,
             " parsed_nearby_npcs=", result.nearbyNpcs.size(),
             " nearby_npc_known_dialogs=", result.nearbyNpcKnownDialogCount,
             " parsed_nearby_npc_known_dialogs=", result.nearbyNpcKnownDialogs.size(),
             " nearby_waypoints=", result.nearbyWaypointCount,
             " parsed_nearby_waypoints=", result.nearbyWaypoints.size(),
             " recent_actions=", result.recentActionCount,
             " parsed_recent_actions=", result.recentActions.size(),
             " mover_state=", result.moverStateCount,
             " parsed_mover_state=", result.moverStates.size(),
             " npc_routine_state=", result.npcRoutineStateCount,
             " npc_ai_state=", result.npcAiStateCount,
             " npc_path_state=", result.npcPathStateCount,
             " npc_fight_state=", result.npcFightStateCount,
             " trigger_queue=", result.triggerQueueCount,
             " world_transition_state=", result.worldTransitionStateCount,
             " client_corrections=", result.clientCorrectionCount,
             " parsed_client_corrections=", result.clientCorrections.size(),
             " applied_movers=", appliedMovers,
             " missing_movers=", missingMovers,
             " skipped_movers=", skippedMovers,
             " checkpoint_manifest=", result.serverCheckpointManifestPresent ? 1 : 0,
             " checkpoint_latest_tick=", result.serverCheckpointManifest.latestCheckpointTick,
             " checkpoint_recent_event_seq=", result.serverCheckpointManifest.recentEventSeq,
             " nearby_npc_radius=", result.nearbyNpcRadius,
             " nearby_waypoint_radius=", result.nearbyWaypointRadius,
             " interactive_state=", result.interactiveStateCount,
             " parsed_interactives=", result.interactiveStates.size(),
             " applied_interactives=", applied.appliedInteractives,
             " missing_interactives=", applied.missingInteractives,
             " npc_lifecycle_state=", result.npcLifecycleStateCount,
             " parsed_npc_lifecycle=", result.npcLifecycleStates.size(),
             " applied_npc_lifecycle=", applied.appliedNpcLifecycle,
             " missing_npc_lifecycle=", applied.missingNpcLifecycle,
             " skipped_npc_lifecycle=", applied.skippedNpcLifecycle);
      }
    catch(const std::exception& e) {
      Log::e("MMO server snapshot world state apply failed: ", e.what());
      }
    catch(...) {
      Log::e("MMO server snapshot world state apply failed: unknown exception");
    }
    }

  if(restoredFromDbSaveCheckpoint || state.reason == "db_continue_baseline_loaded") {
    wrld->resetPositionToTA();
    const auto resumedNpcRoutines = wrld->resumeNpcRoutinesAfterServerRestore();
    const auto npcAuthority = applyMmoNpcRoutineAuthorityState(*wrld, result);
    Log::i("MMO server DB restore NPC routines resumed: count=", resumedNpcRoutines,
           " db_checkpoint=", restoredFromDbSaveCheckpoint ? 1 : 0,
           " reason=", state.reason,
           " npc_routine_state=", result.npcRoutineStateCount,
           " npc_ai_state=", result.npcAiStateCount,
           " npc_path_state=", result.npcPathStateCount,
           " npc_fight_state=", result.npcFightStateCount,
           " authority_applied=", npcAuthority.applied,
           " authority_fallback=", npcAuthority.fallback,
           " authority_missing_npc=", npcAuthority.missingNpc,
           " authority_skipped=", npcAuthority.skipped);
    }

  state.lastAppliedSnapshotId =
      std::max(state.lastAppliedSnapshotId, snapshot->snapshotId);
  state.completed = true;
  return true;
}

void GameSession::waitForMmoServerSnapshotRestoreDuringLoad() noexcept {
  const auto& cmd = CommandLine::inst();
  if(!cmd.mmoClientUsesServer())
    return;

  for(unsigned i = 0; i != 260; ++i) {
    if(tryApplyMmoServerSnapshotRestore(true))
      return;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

void GameSession::pollMmoServerSnapshotRestore() noexcept {
  if(mmoServerSnapshotRestore.requested && !mmoServerSnapshotRestore.completed) {
    (void)tryApplyMmoServerSnapshotRestore(false);
    return;
    }
  (void)tryApplyMmoServerWorldSnapshotRefresh();
}

struct GameSession::MmoServerPresentationBatchSink final {
  explicit MmoServerPresentationBatchSink(GameSession& owner) noexcept
      : owner(owner) {}

  void routeApplied(
      const Mmo::ClientPresentation::ServerPresentationRouteIdentity& route,
      const Mmo::ClientPresentation::ServerPresentationRouteReplaceResult&) {
    owner.resetMmoServerPresentationProjection();
    owner.setMmoServerPresentationRoute(route);
    Mmo::recordClientMmoProcessGatePresentation(
        Mmo::ClientMmoProcessGatePresentationEvent::RouteApplied);
    projectionResetForRoute = true;
  }

  void bootstrapApplied(
      const Mmo::ClientPresentation::ServerPresentationBootstrap& bootstrap,
      const Mmo::ClientPresentation::ServerPresentationBootstrapInstallResult&) {
    owner.installMmoServerPresentationBootstrap(
        bootstrap, std::exchange(projectionResetForRoute, false));
  }

  void eventApplied(
      const Mmo::ClientPresentation::ServerPresentationEvent& event,
      const Mmo::ClientPresentation::ServerPresentationApplyResult& result) {
    owner.applyMmoServerPresentationEvent(event, result);
  }

  template<class Record, class Result>
  void rejected(const Record&, const Result& result) const {
    Log::e("MMO typed presentation state rejected record: status=",
           static_cast<unsigned>(result.status));
  }

  void sourceRejected(const std::size_t count) const {
    Log::e("MMO typed presentation facade rejected records: count=", count);
  }

  GameSession& owner;
  bool projectionResetForRoute = false;
};

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

void GameSession::releaseMmoServerEntity(
    const Mmo::ClientPresentation::ServerPresentationEntityRecord& entity) noexcept {
  const auto handle = presentationRegistryHandle(entity.handle);
  if(auto released = mmoServerEntityPresentation.invalidate(
         handle, mmoPresentationWorldGeneration)) {
    releaseMmoServerPresentationBinding(*released);
  }
  static_cast<void>(mmoServerEntityInterpolator.erase(
      handle, mmoPresentationWorldGeneration));
  if(entity.kind ==
     Mmo::ClientPresentation::ServerPresentationEntityKind::LocalPlayer) {
    mmoMovementCorrectionBoundary.unbindLocalPlayer();
  }
}

bool GameSession::trackMmoServerInventoryCommand(
    Mmo::ClientPresentation::ServerInventoryPendingCommand command) {
  return mmoServerInventoryPresentation_.markPending(std::move(command));
}

std::optional<GameSession::MmoServerEntityTarget>
GameSession::mmoServerEntityTarget(const Npc& npc) const noexcept {
  const auto* binding = mmoServerEntityPresentation.findLocal(
      reinterpret_cast<std::uintptr_t>(&npc));
  if(binding == nullptr)
    return std::nullopt;

  const Mmo::ClientPresentation::ServerPresentationEntityHandle entity{
      .id = binding->handle.id,
      .generation = binding->handle.generation,
  };
  const auto* record = mmoTypedServerPresentation.findEntity(entity);
  const auto session = Mmo::clientMmoSessionSnapshot();
  if(record == nullptr || !session.inWorld() || session.worldId == 0U ||
     session.worldGeneration == 0U ||
     binding->worldGeneration != session.worldGeneration) {
    return std::nullopt;
  }

  return MmoServerEntityTarget{
      .handle = {
          .worldId = session.worldId,
          .worldGeneration = session.worldGeneration,
          .id = binding->handle.id,
          .generation = binding->handle.generation,
      },
      .revision = record->entityRevision,
  };
}

std::optional<GameSession::MmoServerEntityTarget>
GameSession::mmoServerEntityTarget(
    const Interactive& interactive) const noexcept {
  using Kind =
      Mmo::ClientPresentation::ServerPresentationWorldObjectKind;
  std::optional<Mmo::ClientPresentation::ServerPresentationEntityHandle> entity;
  for(const auto kind : {Kind::Interactive, Kind::Container, Kind::Mover}) {
    entity = mmoServerWorldObjects.find(
        Mmo::ClientPresentation::LocalVobToken{
            .vobObjectId = interactive.getId(),
            .kind = kind,
        });
    if(entity.has_value())
      break;
  }
  if(!entity.has_value())
    return std::nullopt;

  std::uint64_t revision = 0U;
  if(const auto* state = mmoTypedServerPresentation.findInteractive(*entity);
     state != nullptr) {
    revision = state->stateRevision;
  } else if(const auto* state = mmoTypedServerPresentation.findMover(*entity);
            state != nullptr) {
    revision = state->stateRevision;
  } else {
    return std::nullopt;
  }
  const auto session = Mmo::clientMmoSessionSnapshot();
  if(!session.inWorld() || session.worldId == 0U ||
     session.worldGeneration == 0U || !entity->valid()) {
    return std::nullopt;
  }

  return MmoServerEntityTarget{
      .handle = {
          .worldId = session.worldId,
          .worldGeneration = session.worldGeneration,
          .id = entity->id,
          .generation = entity->generation,
      },
      .revision = revision,
  };
}

Npc* GameSession::resolveMmoServerEntity(
    const Mmo::ClientPresentation::ServerPresentationEntityHandle entity) noexcept {
  if(wrld == nullptr || !entity.valid())
    return nullptr;
  const auto* binding = mmoServerEntityPresentation.find(
      presentationRegistryHandle(entity), mmoPresentationWorldGeneration);
  return binding != nullptr ? resolveMmoPresentationBinding(*wrld, *binding)
                            : nullptr;
}

void GameSession::materializeMmoServerEntity(
    const Mmo::ClientPresentation::ServerPresentationEntityRecord& entity,
    const bool snap) noexcept {
  if(wrld == nullptr || mmoPresentationRouteKey.empty())
    return;

  using namespace Mmo::ClientPresentation;
  const auto handle = presentationRegistryHandle(entity.handle);
  const auto kind = presentationRegistryKind(entity.kind);
  auto stableKey = mmoPresentationEntityKey(entity);
  const ServerEntityTransformObservation observation{
      .route = {
          .worldGeneration = mmoPresentationWorldGeneration,
          .worldInstanceId = mmoPresentationRouteKey,
      },
      .handle = handle,
      .kind = kind,
      // This registry field is an ordering key. Typed V2 entities provide the
      // stricter per-entity revision, which also distinguishes updates emitted
      // within the same authoritative server tick.
      .serverTick = entity.entityRevision,
      .stableEntityKey = stableKey,
      .posX = entity.transform.posX,
      .posY = entity.transform.posY,
      .posZ = entity.transform.posZ,
      .yaw = entity.transform.yaw,
      .active = true,
  };

  auto observed = mmoServerEntityPresentation.observe(observation);
  if(observed.releasedBinding.has_value()) {
    releaseMmoServerPresentationBinding(*observed.releasedBinding);
    static_cast<void>(mmoServerEntityInterpolator.erase(
        observed.releasedBinding->handle,
        observed.releasedBinding->worldGeneration));
  }
  if(observed.status == ServerEntityObservationStatus::Stale ||
     observed.status == ServerEntityObservationStatus::RouteMismatch ||
     observed.status == ServerEntityObservationStatus::IdentityMismatch ||
     observed.status == ServerEntityObservationStatus::Invalid) {
    return;
  }

  const auto* binding = mmoServerEntityPresentation.find(
      handle, mmoPresentationWorldGeneration);
  Npc* npc = binding != nullptr
                 ? resolveMmoPresentationBinding(*wrld, *binding)
                 : nullptr;
  bool materializedByMmo = false;
  bool newlyMaterialized = false;
  if(npc == nullptr) {
    const auto materialized = materializeMmoServerEntityNpc(
        *wrld, *vm, mmoClientPresentationCatalog.get(), entity);
    npc = materialized.npc;
    materializedByMmo = materialized.materializedByMmo;
    const auto local = localMmoPresentationIdentity(
        *wrld, npc, kind, materializedByMmo);
    if(!local || !mmoServerEntityPresentation.bind(observation, *local)) {
      if(materializedByMmo && npc != nullptr)
        wrld->removeNpc(*npc);
      Log::e("MMO typed entity unresolved: entity=", entity.handle.id,
             " generation=", entity.handle.generation,
             " kind=", static_cast<unsigned>(entity.kind),
             " presentation=", entity.presentation.presentationId,
             " archetype=", entity.presentation.archetypeId);
      return;
    }
    newlyMaterialized = true;
  }

  if(newlyMaterialized) {
    auto event = Mmo::ClientMmoProcessGatePresentationEvent::NpcMaterialized;
    if(entity.kind == ServerPresentationEntityKind::LocalPlayer)
      event = Mmo::ClientMmoProcessGatePresentationEvent::LocalPlayerMaterialized;
    else if(entity.kind == ServerPresentationEntityKind::RemotePlayer)
      event = Mmo::ClientMmoProcessGatePresentationEvent::RemotePlayerMaterialized;
    Mmo::recordClientMmoProcessGatePresentation(event);
  }
  applyMmoServerEntityTransform(entity, snap);
}

void GameSession::applyMmoServerEntityTransform(
    const Mmo::ClientPresentation::ServerPresentationEntityRecord& entity,
    const bool snap) noexcept {
  if(wrld == nullptr || mmoPresentationRouteKey.empty())
    return;

  using namespace Mmo::ClientPresentation;
  const auto handle = presentationRegistryHandle(entity.handle);
  const auto kind = presentationRegistryKind(entity.kind);
  const auto* binding = mmoServerEntityPresentation.find(
      handle, mmoPresentationWorldGeneration);
  if(binding == nullptr) {
    materializeMmoServerEntity(entity, snap);
    return;
  }
  auto* npc = resolveMmoPresentationBinding(*wrld, *binding);
  if(npc == nullptr) {
    if(auto released = mmoServerEntityPresentation.invalidate(
           handle, mmoPresentationWorldGeneration)) {
      releaseMmoServerPresentationBinding(*released);
    }
    static_cast<void>(mmoServerEntityInterpolator.erase(
        handle, mmoPresentationWorldGeneration));
    materializeMmoServerEntity(entity, snap);
    return;
  }

  const ServerEntityTransformObservation observation{
      .route = {
          .worldGeneration = mmoPresentationWorldGeneration,
          .worldInstanceId = mmoPresentationRouteKey,
      },
      .handle = handle,
      .kind = kind,
      // See materialization above: per-entity revision is the monotonic order
      // key; receive time remains the interpolator's temporal input.
      .serverTick = entity.entityRevision,
      .stableEntityKey = binding->stableEntityKey,
      .posX = entity.transform.posX,
      .posY = entity.transform.posY,
      .posZ = entity.transform.posZ,
      .yaw = entity.transform.yaw,
      .active = true,
  };

  if(kind == ServerEntityKind::LocalPlayer) {
    if(!mmoMovementCorrectionBoundary.bindLocalPlayer(handle, observation.route))
      return;
    if(snap || entity.transform.teleport) {
      if(npc->setPosition(static_cast<float>(entity.transform.posX),
                          static_cast<float>(entity.transform.posY),
                          static_cast<float>(entity.transform.posZ))) {
        npc->setDirection(static_cast<float>(entity.transform.yaw));
        npc->clearSpeed();
      }
    }
    mmoServerEntityPresentation.touch(observation);
    return;
  }

  npc->setMmoServerReplica(true);
  const bool hardSnap = snap || entity.transform.teleport;
  if(hardSnap) {
    static_cast<void>(mmoServerEntityInterpolator.erase(
        handle, mmoPresentationWorldGeneration));
    if(npc->setPosition(static_cast<float>(entity.transform.posX),
                        static_cast<float>(entity.transform.posY),
                        static_cast<float>(entity.transform.posZ))) {
      npc->setDirection(static_cast<float>(entity.transform.yaw));
      npc->clearSpeed();
      mmoServerEntityPresentation.touch(observation);
    }
    return;
  }

  if(mmoServerEntityInterpolator.ingest(observation, ticks) ==
     ServerEntityInterpolationIngestStatus::Accepted) {
    mmoServerEntityPresentation.touch(observation);
  }
}

void GameSession::applyMmoServerNpcState(
    const Mmo::ClientPresentation::ServerPresentationNpcStateRecord& state) noexcept {
  if(wrld == nullptr)
    return;
  using namespace Mmo::ClientPresentation;
  const auto* binding = mmoServerEntityPresentation.find(
      presentationRegistryHandle(state.entity), mmoPresentationWorldGeneration);
  if(binding == nullptr || binding->kind != ServerEntityKind::Npc)
    return;
  auto* npc = resolveMmoPresentationBinding(*wrld, *binding);
  if(npc == nullptr)
    return;

  Npc::PersistentStats stats;
  stats.healthCurrent = state.health;
  stats.healthMax = state.maximumHealth;
  stats.manaCurrent = state.mana;
  stats.manaMax = state.maximumMana;
  npc->restorePersistentStats(stats);
  npc->setMmoServerReplica(true);

  Npc::MmoPresentationLifeState lifeState =
      Npc::MmoPresentationLifeState::Alive;
  switch(state.lifeState) {
    case ServerPresentationNpcLifeState::Alive:
      lifeState = Npc::MmoPresentationLifeState::Alive;
      break;
    case ServerPresentationNpcLifeState::Unconscious:
      lifeState = Npc::MmoPresentationLifeState::Unconscious;
      break;
    case ServerPresentationNpcLifeState::Dead:
      lifeState = Npc::MmoPresentationLifeState::Dead;
      break;
  }
  npc->applyMmoServerPresentationLifecycle(
      state.health, state.maximumHealth, lifeState);

  if(lifeState == Npc::MmoPresentationLifeState::Alive) {
    switch(state.activityState) {
      case ServerPresentationNpcActivityState::Idle:
      case ServerPresentationNpcActivityState::Routine:
      case ServerPresentationNpcActivityState::Dialog:
        static_cast<void>(npc->setAnim(Npc::Anim::Idle));
        break;
      case ServerPresentationNpcActivityState::Traversal:
        static_cast<void>(npc->setAnim(Npc::Anim::Move));
        break;
      case ServerPresentationNpcActivityState::Interaction:
      case ServerPresentationNpcActivityState::Combat:
        break;
    }
  }

  Npc* target = nullptr;
  if((state.flags & ServerPresentationNpcTargetPresent) != 0U)
    target = resolveMmoServerEntity(state.target);
  npc->setTarget(target);

  if(state.activityState == ServerPresentationNpcActivityState::Dialog ||
     state.activityState == ServerPresentationNpcActivityState::Interaction) {
    npc->setAiOutputBarrier(250U, true);
  }
  Mmo::recordClientMmoProcessGatePresentation(
      Mmo::ClientMmoProcessGatePresentationEvent::NpcStateApplied);
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
      ClientEquipmentSlot::MeleeWeapon,
      ClientEquipmentSlot::RangedWeapon,
      ClientEquipmentSlot::Armor,
      ClientEquipmentSlot::Amulet,
      ClientEquipmentSlot::RingLeft,
      ClientEquipmentSlot::RingRight,
      ClientEquipmentSlot::Belt,
      ClientEquipmentSlot::Spell,
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

void GameSession::applyMmoServerCombatAction(
    const Mmo::ClientPresentation::ServerPresentationCombatActionRecord& action) noexcept {
  auto* actor = resolveMmoServerEntity(action.entity);
  if(actor == nullptr)
    return;
  actor->setMmoServerReplica(true);
  actor->setTarget(action.target.empty()
                       ? nullptr
                       : resolveMmoServerEntity(action.target));

  const bool predictedLocally =
      (action.flags &
       Mmo::ClientPresentation::ServerPresentationCombatActionPredictedLocally) != 0U;
  if(predictedLocally && wrld != nullptr && actor == wrld->player())
    return;

  actor->applyMmoServerPresentationCombatAction(
      presentationCombatAction(action.kind), action.comboIndex,
      (action.flags &
       Mmo::ClientPresentation::ServerPresentationCombatActionLeftSide) != 0U,
      (action.flags &
       Mmo::ClientPresentation::ServerPresentationCombatActionRightSide) != 0U);
}

void GameSession::resolveMmoServerCombatAction(
    const Mmo::ClientPresentation::ServerPresentationCombatActionResolution& resolution) noexcept {
  using namespace Mmo::ClientPresentation;
  if(resolution.result == ServerPresentationCombatActionResult::Completed)
    return;
  auto* actor = resolveMmoServerEntity(resolution.entity);
  if(actor == nullptr)
    return;

  const auto* melee = mmoTypedServerPresentation.findEquipment(
      resolution.entity, ServerPresentationEquipmentSlot::MeleeWeapon);
  const auto* ranged = mmoTypedServerPresentation.findEquipment(
      resolution.entity, ServerPresentationEquipmentSlot::RangedWeapon);
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
  auto authoritativeMode = resolution.authoritativeWeaponMode;
  if(const auto* mode =
         mmoTypedServerPresentation.findWeaponMode(resolution.entity);
     mode != nullptr) {
    authoritativeMode = mode->mode;
  }
  actor->correctMmoServerPresentationCombat(
      presentationWeaponState(authoritativeMode, shape),
      shape.meleeTwoHanded, shape.rangedCrossbow);
}

void GameSession::applyMmoServerDamage(
    const Mmo::ClientPresentation::ServerPresentationDamageRecord& damage) noexcept {
  auto* target = resolveMmoServerEntity(damage.target);
  if(target == nullptr)
    return;
  Npc::PersistentStats stats;
  stats.healthCurrent = damage.health;
  stats.healthMax = damage.maximumHealth >= 0
                        ? damage.maximumHealth
                        : Npc::PersistentStats::Missing;
  target->restorePersistentStats(stats);
  target->setMmoServerReplica(true);
}

void GameSession::applyMmoServerHitReaction(
    const Mmo::ClientPresentation::ServerPresentationHitReactionRecord& reaction) noexcept {
  if(wrld == nullptr)
    return;
  auto* target = resolveMmoServerEntity(reaction.target);
  if(target == nullptr)
    return;
  target->setMmoServerReplica(true);
  target->applyMmoServerPresentationHitReaction(
      presentationHitReaction(reaction.kind));

  const Tempest::Vec3 knockback{
      reaction.knockbackX, reaction.knockbackY, reaction.knockbackZ};
  if(knockback.x != 0.f || knockback.y != 0.f || knockback.z != 0.f)
    static_cast<void>(target->setPosition(target->position()+knockback));

  const auto effectFlags =
      Mmo::ClientPresentation::ServerPresentationHitReactionVfx |
      Mmo::ClientPresentation::ServerPresentationHitReactionSfx;
  if(auto* source = reaction.source.empty()
                        ? nullptr
                        : resolveMmoServerEntity(reaction.source);
     source != nullptr && (reaction.flags & effectFlags) != 0U) {
    const bool spawnVfx =
        (reaction.flags &
         Mmo::ClientPresentation::ServerPresentationHitReactionVfx) != 0U;
    auto effect =
        wrld->addWeaponHitEffect(*source, nullptr, *target, spawnVfx);
    if((reaction.flags &
        Mmo::ClientPresentation::ServerPresentationHitReactionSfx) != 0U) {
      effect.play();
    }
  }

  if(target == wrld->player() &&
     (reaction.flags &
      Mmo::ClientPresentation::ServerPresentationHitReactionCameraShake) != 0U) {
    camera().addPresentationShake(reaction.cameraShakeStrength);
  }
}

void GameSession::applyMmoServerLifeState(
    const Mmo::ClientPresentation::ServerPresentationLifeStateRecord& state) noexcept {
  auto* npc = resolveMmoServerEntity(state.entity);
  if(npc == nullptr)
    return;
  npc->setMmoServerReplica(true);
  npc->applyMmoServerPresentationLifecycle(
      state.health, state.maximumHealth,
      presentationLifeState(state.lifeState));
}

void GameSession::applyMmoServerInteractiveState(
    const Mmo::ClientPresentation::ServerPresentationInteractiveStateRecord& state) noexcept {
  using namespace Mmo::ClientPresentation;
  if(wrld == nullptr ||
     state.stateId > static_cast<std::uint64_t>(
                         std::numeric_limits<std::int32_t>::max())) {
    return;
  }

  const auto signature = mmoInteractiveStateSignature(state);
  const auto status = mmoServerWorldObjects.inspectState(
      state.entity, state.stateRevision, signature);
  if(status == ServerWorldObjectStateStatus::Duplicate ||
     status == ServerWorldObjectStateStatus::Stale)
    return;
  if(status == ServerWorldObjectStateStatus::Unchanged) {
    mmoServerWorldObjects.commitState(
        state.entity, state.stateRevision, signature);
    return;
  }

  const auto* local = mmoServerWorldObjects.find(state.entity);
  if(local == nullptr ||
     (local->kind != ServerPresentationWorldObjectKind::Interactive &&
      local->kind != ServerPresentationWorldObjectKind::Container)) {
    if(mmoServerWorldObjects.shouldLogUnresolved(
           state.entity, state.stateRevision)) {
      Log::e("MMO typed interactive unresolved: entity=", state.entity.id,
             " generation=", state.entity.generation,
             " revision=", state.stateRevision);
    }
    return;
  }

  auto* interactive = wrld->interactiveByVobId(local->vobObjectId);
  if(interactive == nullptr) {
    if(mmoServerWorldObjects.shouldLogUnresolved(
           state.entity, state.stateRevision)) {
      Log::e("MMO typed interactive local VOB unresolved: entity=",
             state.entity.id,
             " vob=", local->vobObjectId,
             " revision=", state.stateRevision);
    }
    return;
  }
  const bool locked =
      (state.flags & ServerPresentationInteractiveLocked) != 0U;
  interactive->restorePersistentState(
      static_cast<std::int32_t>(state.stateId), locked, false);
  mmoServerWorldObjects.commitState(
      state.entity, state.stateRevision, signature);
  Mmo::recordClientMmoProcessGatePresentation(
      Mmo::ClientMmoProcessGatePresentationEvent::InteractiveApplied);
}

void GameSession::applyMmoServerMoverState(
    const Mmo::ClientPresentation::ServerPresentationMoverStateRecord& state) noexcept {
  using namespace Mmo::ClientPresentation;
  if(wrld == nullptr)
    return;

  const auto signature = mmoMoverStateSignature(state);
  const auto status = mmoServerWorldObjects.inspectState(
      state.entity, state.stateRevision, signature);
  if(status == ServerWorldObjectStateStatus::Duplicate ||
     status == ServerWorldObjectStateStatus::Stale)
    return;
  if(status == ServerWorldObjectStateStatus::Unchanged) {
    mmoServerWorldObjects.commitState(
        state.entity, state.stateRevision, signature);
    return;
  }

  const auto* local = mmoServerWorldObjects.find(state.entity);
  if(local == nullptr || local->kind != ServerPresentationWorldObjectKind::Mover) {
    if(mmoServerWorldObjects.shouldLogUnresolved(
           state.entity, state.stateRevision)) {
      Log::e("MMO typed mover unresolved: entity=", state.entity.id,
             " generation=", state.entity.generation,
             " revision=", state.stateRevision);
    }
    return;
  }

  std::int32_t moverState = 0;
  switch(state.phase) {
    case ServerPresentationMoverPhase::AtStart: moverState = 0; break;
    case ServerPresentationMoverPhase::Opening: moverState = 2; break;
    case ServerPresentationMoverPhase::AtEnd: moverState = 0; break;
    case ServerPresentationMoverPhase::Closing: moverState = 4; break;
    case ServerPresentationMoverPhase::Paused: moverState = 0; break;
  }

  const auto frame = static_cast<std::int32_t>(state.keyframe);
  if(!wrld->restoreMoverState(local->vobObjectId, moverState, frame, -1)) {
    if(mmoServerWorldObjects.shouldLogUnresolved(
           state.entity, state.stateRevision)) {
      Log::e("MMO typed mover local VOB unresolved: entity=", state.entity.id,
             " vob=", local->vobObjectId,
             " phase=", static_cast<unsigned>(state.phase),
             " keyframe=", state.keyframe,
             " revision=", state.stateRevision);
    }
    return;
  }
  mmoServerWorldObjects.commitState(
      state.entity, state.stateRevision, signature);
  Mmo::recordClientMmoProcessGatePresentation(
      Mmo::ClientMmoProcessGatePresentationEvent::MoverApplied);
}

void GameSession::applyMmoServerMovementCorrection() noexcept {
  if(wrld == nullptr || mmoPresentationRouteKey.empty())
    return;
  const auto typed = mmoTypedServerPresentation.takePendingCorrection();
  if(!typed.has_value())
    return;

  using namespace Mmo::ClientPresentation;
  const bool hardSnap = typed->transform.teleport ||
                        typed->reason == ServerMovementCorrectionReason::Teleport ||
                        typed->reason == ServerMovementCorrectionReason::Resync;
  const ServerMovementCorrection correction{
      .route = {
          .worldGeneration = mmoPresentationWorldGeneration,
          .worldInstanceId = mmoPresentationRouteKey,
      },
      .handle = presentationRegistryHandle(typed->entity),
      .serverTick = typed->header.serverTick,
      .posX = typed->transform.posX,
      .posY = typed->transform.posY,
      .posZ = typed->transform.posZ,
      .yaw = typed->transform.yaw,
      .hardSnap = hardSnap,
  };
  if(mmoMovementCorrectionBoundary.observe(correction) !=
     ServerMovementCorrectionStatus::Accepted) {
    return;
  }
  const auto accepted = mmoMovementCorrectionBoundary.takePending();
  auto* hero = wrld->player();
  if(!accepted.has_value() || hero == nullptr)
    return;

  if(hero->setPosition(static_cast<float>(accepted->posX),
                       static_cast<float>(accepted->posY),
                       static_cast<float>(accepted->posZ))) {
    hero->setDirection(static_cast<float>(accepted->yaw));
    if(accepted->hardSnap)
      hero->clearSpeed();
    Mmo::recordClientMmoProcessGatePresentation(
        Mmo::ClientMmoProcessGatePresentationEvent::MovementCorrectionApplied);
  }
}

void GameSession::sampleMmoServerEntityTransforms() noexcept {
  if(wrld == nullptr || mmoPresentationWorldGeneration == 0U)
    return;
  mmoServerEntityInterpolator.sample(ticks, mmoServerEntitySamples);
  for(const auto& sampled : mmoServerEntitySamples) {
    const auto* binding = mmoServerEntityPresentation.find(
        sampled.handle, sampled.worldGeneration);
    if(binding == nullptr || binding->kind != sampled.kind)
      continue;
    auto* npc = resolveMmoPresentationBinding(*wrld, *binding);
    if(npc == nullptr) {
      if(auto released = mmoServerEntityPresentation.invalidate(
             sampled.handle, sampled.worldGeneration)) {
        releaseMmoServerPresentationBinding(*released);
      }
      static_cast<void>(mmoServerEntityInterpolator.erase(
          sampled.handle, sampled.worldGeneration));
      continue;
    }
    if(npc->setPosition(static_cast<float>(sampled.posX),
                        static_cast<float>(sampled.posY),
                        static_cast<float>(sampled.posZ))) {
      npc->setDirection(static_cast<float>(sampled.yaw));
    }
  }
}

void GameSession::applyMmoServerPresentationEvent(
    const Mmo::ClientPresentation::ServerPresentationEvent& event,
    const Mmo::ClientPresentation::ServerPresentationApplyResult& result) noexcept {
  using namespace Mmo::ClientPresentation;
  Mmo::Hooks::ScopedCaptureSuppression suppressCapture;
  std::visit(
      [this, &event, &result](const auto& value) {
        using Event = std::decay_t<decltype(value)>;
        if constexpr(std::is_same_v<Event, ServerEntitySpawnEvent>) {
          if(result.releasedEntity.has_value())
            releaseMmoServerEntity(*result.releasedEntity);
          materializeMmoServerEntity(value.entity, true);
        } else if constexpr(std::is_same_v<Event, ServerEntityDespawnEvent>) {
          if(result.releasedEntity.has_value()) {
            releaseMmoServerEntity(*result.releasedEntity);
            Mmo::recordClientMmoProcessGatePresentation(
                Mmo::ClientMmoProcessGatePresentationEvent::EntityDespawnApplied);
          }
        } else if constexpr(std::is_same_v<Event, ServerEntityTransformEvent>) {
          const auto* entity = mmoTypedServerPresentation.findEntity(value.entity);
          if(entity != nullptr) {
            applyMmoServerEntityTransform(*entity,
                                          value.transform.teleport);
            Mmo::recordClientMmoProcessGatePresentation(
                Mmo::ClientMmoProcessGatePresentationEvent::TransformApplied);
          }
        } else if constexpr(std::is_same_v<Event, ServerMovementCorrectionEvent>) {
          applyMmoServerMovementCorrection();
        } else if constexpr(std::is_same_v<Event, ServerNpcStateEvent>) {
          applyMmoServerNpcState(value.state);
        } else if constexpr(std::is_same_v<Event, ServerInventorySnapshotEvent>) {
          applyMmoServerInventorySnapshot(value);
        } else if constexpr(std::is_same_v<Event, ServerInventoryDeltaEvent>) {
          applyMmoServerInventoryDelta(value);
        } else if constexpr(std::is_same_v<Event, ServerEquipmentSnapshotEvent>) {
          applyMmoServerEquipmentSnapshot(value);
        } else if constexpr(std::is_same_v<Event, ServerEquipmentBindingChangedEvent>) {
          applyMmoServerEquipmentBinding(value);
        } else if constexpr(std::is_same_v<Event, ServerEquipmentSlotChangedEvent>) {
          applyMmoServerEquipmentSlot(value.state);
        } else if constexpr(std::is_same_v<Event, ServerWeaponModeChangedEvent>) {
          applyMmoServerWeaponMode(value.state, true);
        } else if constexpr(std::is_same_v<Event, ServerCombatActionStartedEvent>) {
          applyMmoServerCombatAction(value.action);
        } else if constexpr(std::is_same_v<Event, ServerCombatActionResolvedEvent>) {
          resolveMmoServerCombatAction(value.resolution);
        } else if constexpr(std::is_same_v<Event, ServerDamageAppliedEvent>) {
          applyMmoServerDamage(value.damage);
        } else if constexpr(std::is_same_v<Event, ServerHitReactionEvent>) {
          applyMmoServerHitReaction(value.reaction);
        } else if constexpr(std::is_same_v<Event, ServerCharacterDeathStateChangedEvent>) {
          applyMmoServerLifeState(value.state);
        } else if constexpr(std::is_same_v<Event, ServerInteractiveStateEvent>) {
          applyMmoServerInteractiveState(value.state);
        } else if constexpr(std::is_same_v<Event, ServerMoverStateEvent>) {
          applyMmoServerMoverState(value.state);
        } else if constexpr(std::is_same_v<Event, ServerDialogStartEvent>) {
          auto* player = resolveMmoServerEntity(value.player);
          auto* npc = resolveMmoServerEntity(value.npc);
          Gothic::inst().presentTypedServerDialog(player, npc, nullptr, event);
          Mmo::recordClientMmoProcessGatePresentation(
              Mmo::ClientMmoProcessGatePresentationEvent::DialogApplied);
        } else if constexpr(std::is_same_v<Event, ServerDialogUpdateEvent>) {
          const auto& dialog = mmoTypedServerPresentation.dialog();
          auto* player = resolveMmoServerEntity(dialog.player);
          auto* npc = resolveMmoServerEntity(dialog.npc);
          auto* speaker = resolveMmoServerEntity(value.speaker);
          Gothic::inst().presentTypedServerDialog(player, npc, speaker, event);
          Mmo::recordClientMmoProcessGatePresentation(
              Mmo::ClientMmoProcessGatePresentationEvent::DialogApplied);
        } else if constexpr(std::is_same_v<Event, ServerDialogEndEvent>) {
          Gothic::inst().presentTypedServerDialog(nullptr, nullptr, nullptr, event);
          Mmo::recordClientMmoProcessGatePresentation(
              Mmo::ClientMmoProcessGatePresentationEvent::DialogApplied);
        } else if constexpr(std::is_same_v<Event, ServerDialogBusyEvent>) {
          auto* npc = resolveMmoServerEntity(value.npc);
          Gothic::inst().presentTypedServerDialog(nullptr, npc, nullptr, event);
          Mmo::recordClientMmoProcessGatePresentation(
              Mmo::ClientMmoProcessGatePresentationEvent::DialogApplied);
        } else if constexpr(std::is_same_v<Event, ServerWorldDescriptorEvent>) {
          Log::i("MMO typed world descriptor updated: revision=",
                 value.descriptor.descriptorRevision,
                 " content=", value.descriptor.contentFingerprint,
                 " story=", value.descriptor.storyProjectionFingerprint);
        }
      },
      event);
}

void GameSession::pollMmoServerPresentationMailbox() noexcept {
  const auto& cmd = CommandLine::inst();
  if(!cmd.mmoClientUsesServer() || wrld == nullptr)
    return;

  for(const auto& completion : Mmo::drainClientMmoCommandCompletions())
    mmoServerInventoryPresentation_.complete(completion);

  auto batch = Mmo::drainTypedServerPresentationMailbox();
  if(!batch.empty() || batch.rejectedRecords != 0U) {
    MmoServerPresentationBatchSink sink{*this};
    const auto stats = Mmo::ClientPresentation::consumeServerPresentationBatch(
        mmoTypedServerPresentation, batch, sink);
    if(stats.routesApplied != 0U || stats.bootstrapsApplied != 0U ||
       stats.eventsApplied != 0U || stats.stateRejected != 0U ||
       stats.sourceRejected != 0U) {
      Log::i("MMO typed presentation batch consumed",
             " routes=", stats.routesApplied,
             " bootstraps=", stats.bootstrapsApplied,
             " events=", stats.eventsApplied,
             " state_rejected=", stats.stateRejected,
             " source_rejected=", stats.sourceRejected,
             " bindings=", mmoServerEntityPresentation.size(),
             " interpolation_tracks=", mmoServerEntityInterpolator.size());
    }
  }
  sampleMmoServerEntityTransforms();
}

bool GameSession::tryApplyMmoServerWorldSnapshotRefresh() noexcept {
  const auto& cmd = CommandLine::inst();
  if(!cmd.mmoClientUsesServer() || !cmd.mmoServerSnapshotApplyWorldState())
    return false;
  if(mmoServerFreshNewGameSession)
    return false;

  auto& state = mmoServerSnapshotRestore;
  if(state.requested && !state.completed)
    return false;
  if(ticks < state.lastLiveRefreshPollTick + MmoServerLiveSnapshotPollInterval)
    return false;
  state.lastLiveRefreshPollTick = ticks;

  (void)Mmo::drainServerBootstrapSnapshots();
  const auto snapshot = Mmo::latestServerBootstrapSnapshot();
  if(!snapshot || snapshot->snapshotId <= state.lastAppliedSnapshotId)
    return false;
  const auto snapshotId = snapshot->snapshotId;

  const auto result = Mmo::RestoreSnapshot::parseAndValidateBootstrapSnapshot(
      snapshot->payload, cmd.mmoCharacterKey());
  if(!result.ok) {
    Log::e("MMO server live world snapshot rejected: snapshot_id=", snapshotId,
           " message=", result.message);
    state.lastAppliedSnapshotId = snapshotId;
    return true;
  }

  if(wrld == nullptr)
    return false;

  try {
    Mmo::Hooks::ScopedCaptureSuppression suppressServerMaterializationEcho;
    if(result.worldClock.present) {
      const auto restoredTime = gtime::fromInt(result.worldClock.currentWorldTimeMs);
      setTime(restoredTime);
      Log::i("MMO server live world clock applied: snapshot_id=", snapshotId,
             " world_time_ms=", result.worldClock.currentWorldTimeMs,
             " hour=", restoredTime.hour(),
             " minute=", restoredTime.minute(),
             " tick=", result.worldClock.currentTick,
             " world=", result.worldClock.worldName);
      }
    size_t appliedCorrections = 0;
    for(const auto& correction : result.clientCorrections) {
      if(correction.acknowledged || !correction.hasAuthoritativePosition)
        continue;
      auto* hero = player();
      if(hero == nullptr)
        break;
      hero->setPosition(static_cast<float>(correction.x),
                        static_cast<float>(correction.y),
                        static_cast<float>(correction.z));
      hero->setDirectionY(static_cast<float>(correction.yaw));
      hero->clearSpeed();
      hero->updateTransform();
      ++appliedCorrections;
      Log::i("MMO server correction applied: snapshot_id=", snapshotId,
             " action=", correction.actionKind,
             " reason=", correction.reason,
             " local_sequence=", correction.clientLocalSequence,
             " x=", correction.x,
             " y=", correction.y,
             " z=", correction.z,
             " yaw=", correction.yaw,
             " server_tick=", correction.authoritativeServerTick);
      }
    const auto applied = applyMmoWorldSnapshotState(*wrld, result);
    size_t appliedMovers = 0;
    size_t missingMovers = 0;
    size_t skippedMovers = 0;
    for(const auto& mover : result.moverStates) {
      if(!mover.hasFrameIndex) {
        ++skippedMovers;
        continue;
        }
      const auto targetFrame = mover.hasTargetFrameIndex ? mover.targetFrameIndex : -1;
      if(wrld->restoreMoverState(mover.moverKey, mover.stateAfter, mover.frameIndex, targetFrame))
        ++appliedMovers;
      else
        ++missingMovers;
      }
    state.lastAppliedSnapshotId = snapshotId;
    Log::i("MMO server live world snapshot applied: snapshot_id=", snapshotId,
           " source=", result.source,
           " snapshot_source=", result.snapshotSource,
           " active_world_items=", result.activeWorldItems.size(),
           " spawned_world_items=", applied.spawnedWorldItems,
           " updated_world_items=", applied.updatedWorldItems,
           " removed_local_items_not_in_db=", applied.authoritativeWindowRemovedItems,
           " authoritative_window=", result.activeWorldItemWindowPresent ? 1 : 0,
           " authoritative_radius=", result.activeWorldItemRadius,
           " nearby_npcs=", result.nearbyNpcs.size(),
           " nearby_npc_known_dialogs=", result.nearbyNpcKnownDialogs.size(),
           " nearby_waypoints=", result.nearbyWaypoints.size(),
           " recent_actions=", result.recentActions.size(),
           " mover_state=", result.moverStates.size(),
           " npc_routine_state=", result.npcRoutineStateCount,
           " npc_ai_state=", result.npcAiStateCount,
           " npc_path_state=", result.npcPathStateCount,
           " npc_fight_state=", result.npcFightStateCount,
           " trigger_queue=", result.triggerQueueCount,
           " world_transition_state=", result.worldTransitionStateCount,
           " client_corrections=", result.clientCorrections.size(),
           " applied_client_corrections=", appliedCorrections,
           " applied_movers=", appliedMovers,
           " missing_movers=", missingMovers,
           " skipped_movers=", skippedMovers,
           " checkpoint_manifest=", result.serverCheckpointManifestPresent ? 1 : 0,
           " checkpoint_latest_tick=", result.serverCheckpointManifest.latestCheckpointTick,
           " checkpoint_recent_event_seq=", result.serverCheckpointManifest.recentEventSeq,
           " nearby_npc_radius=", result.nearbyNpcRadius,
           " nearby_waypoint_radius=", result.nearbyWaypointRadius,
           " interactive_state=", result.interactiveStates.size(),
           " npc_lifecycle_state=", result.npcLifecycleStates.size());
    return true;
    }
  catch(const std::exception& e) {
    Log::e("MMO server live world snapshot apply failed: snapshot_id=", snapshotId,
           " error=", e.what());
    state.lastAppliedSnapshotId = snapshotId;
    return true;
    }
  catch(...) {
    Log::e("MMO server live world snapshot apply failed: snapshot_id=", snapshotId,
           " error=unknown exception");
    state.lastAppliedSnapshotId = snapshotId;
    return true;
    }
}

void GameSession::markMmoServerSnapshotStoryDirty() noexcept {
  auto& state = mmoServerSnapshotRestore;
  if(state.requested && !state.completed)
    state.storyDirtySinceRequest = true;
}

void GameSession::updateListenerPos(const Camera::ListenerPos& lpos) {
  sound.setListenerPosition (lpos.pos);
  sound.setListenerDirection(lpos.front, lpos.up);
  }

void GameSession::setTime(gtime t) {
  wrldTime = t;
  }

void GameSession::tick(uint64_t dt) {
  wrld->scaleTime(dt);

  // apply ztime multiplyer
  dt = dt*timeMul + timeMulFract;
  timeMulFract = dt%1000;
  dt /= 1000;

  ticks+=dt;

  uint64_t add = dt*multTime + wrldTimePart;
  wrldTimePart = add%divTime;
  wrldTime.addMilis(add/divTime);

  vm->tick(dt);
  wrld->tick(dt);
  pollMmoServerSnapshotRestore();
  pollMmoServerPresentationMailbox();

  if(auto* pl = wrld->player()) {
    tickMmoMovementProposal(*pl, ticks);
    tickMmoNpcAuthoritySamples(*pl, ticks);
    if(const char* reason = mmoActionCheckpointReason(*pl, ticks)) {
      Mmo::Hooks::onCharacterCheckpoint(*pl, "GameSession::tick", reason);
      recordMmoActionCheckpointState(*pl, ticks);
      }
    }

#if OPENGOTHIC_MMO_SQLITE_TOOLING
  if(mmoSqlite!=nullptr)
    mmoSqlite->tick(*this, dt);
#endif
  // std::this_thread::sleep_for(std::chrono::milliseconds(60));

  if(exitSessionFlg) {
    exitSessionFlg = false;
    Gothic::inst().clearGame();
    Gothic::inst().onSessionExit();
    return;
    }

  if(!chWorld.zen.empty()) {
    for(auto& c:chWorld.zen)
      c = char(std::tolower(c));
    size_t beg = chWorld.zen.rfind('\\');
    size_t end = chWorld.zen.rfind('.');

    std::string wname;
    if(beg!=std::string::npos && end!=std::string::npos)
      wname = chWorld.zen.substr(beg+1,end-beg-1);
    else if(end!=std::string::npos)
      wname = chWorld.zen.substr(0,end); else
      wname = chWorld.zen;

    const char *w = (beg!=std::string::npos) ? (chWorld.zen.c_str()+beg+1) : chWorld.zen.c_str();

    if(Resources::hasFile(w)) {
      string_frm name("LOADING_",wname,".TGA"); // format load-screen name, like "LOADING_OLDWORLD.TGA"

      Gothic::inst().startLoad(name,[this](std::unique_ptr<GameSession>&& game){
        auto ret = implChangeWorld(std::move(game),chWorld.zen,chWorld.wp);
        chWorld.zen.clear();
        return ret;
        });
      }
    }
  }

void GameSession::setTimeMultiplyer(float t) {
  timeMul = uint64_t(t*1000);
  }

auto GameSession::implChangeWorld(std::unique_ptr<GameSession>&& game,
                                  std::string_view world, std::string_view wayPoint) -> std::unique_ptr<GameSession> {
  size_t           cut = world.rfind('\\');
  std::string_view w   = world;
  if(cut!=std::string::npos)
    w = world.substr(cut+1);

  if(!Resources::hasFile(w)) {
    Log::i("World not found[",world,"]");
    return std::move(game);
    }

  HeroStorage hdata;
  if(auto hero = wrld->player())
    hdata.save(*hero);
  clearWorld();

  vm->resetVarPointers();

  const WorldStateStorage& wss = findStorage(w);
  // Update world name for non-empty wss in case we have a mixed-case world name - otherwise wss is empty for already visited world
  if(!wss.isEmpty())
    w = wss.name;

  auto loadProgress = [](int v) {
    Gothic::inst().setLoadingProgress(v);
    };

  initPerceptions();
  std::unique_ptr<World> ret = std::unique_ptr<World>(new World(*this,w,wss.isEmpty(),loadProgress));
  setWorld(std::move(ret));

  if(!wss.isEmpty()) {
    Tempest::MemReader rd {wss.storage.data(),wss.storage.size()};
    Serialize          fin{rd};
    wrld->load(fin);
    }

  if(1) {
    // put hero to world
    hdata.putToWorld(*game->wrld,wayPoint);
    }
  if(auto hero = wrld->player())
    vm->setInstanceNPC("HERO",*hero);

  initScripts(wss.isEmpty());
  wrld->triggerOnStart(wss.isEmpty());

  for(auto& i:visitedWorlds)
    if(i.compareName(wrld->name())){
      i = std::move(visitedWorlds.back());
      visitedWorlds.pop_back();
      break;
      }

  cam->reset(wrld->player());
  Log::i("Done loading world[",world,"]");
  return std::move(game);
  }

const WorldStateStorage& GameSession::findStorage(std::string_view name) {
  for(auto& i:visitedWorlds)
    if(i.compareName(name))
      return i;
  static WorldStateStorage wss;
  return wss;
  }

void GameSession::updateAnimation(uint64_t dt) {
  if(wrld)
    wrld->updateAnimation(dt);
  }

std::vector<GameScript::DlgChoice> GameSession::updateDialog(const GameScript::DlgChoice &dlg, Npc& player, Npc& npc) {
  auto ret = vm->updateDialog(dlg,player,npc);
#if OPENGOTHIC_MMO_SQLITE_TOOLING
  if(mmoSqlite!=nullptr) {
    mmoSqlite->recordDialogSelection(*this, player, npc, dlg, "update");
    mmoSqlite->recordDialogChoices(*this, player, npc, ret, "subchoices", false);
    }
#endif
  return ret;
  }

void GameSession::dialogExec(const GameScript::DlgChoice &dlg, Npc& player, Npc& npc) {
  markMmoServerSnapshotStoryDirty();
#if OPENGOTHIC_MMO_SQLITE_TOOLING
  if(mmoSqlite!=nullptr)
    mmoSqlite->recordDialogSelection(*this, player, npc, dlg, "exec");
#endif
  return vm->exec(dlg,player,npc);
  }

void GameSession::recordDialogChoices(Npc& player, Npc& npc,
                                      const std::vector<GameScript::DlgChoice>& choices,
                                      std::string_view phase, bool includeImportant) {
#if OPENGOTHIC_MMO_SQLITE_TOOLING
  if(mmoSqlite!=nullptr)
    mmoSqlite->recordDialogChoices(*this, player, npc, choices, phase, includeImportant);
#endif
  }

void GameSession::recordMmoChapterIntro(std::string_view title, std::string_view subtitle,
                                        std::string_view image, std::string_view sound, int time) {
#if OPENGOTHIC_MMO_SQLITE_TOOLING
  if(mmoSqlite!=nullptr)
    mmoSqlite->recordChapterIntro(*this, title, subtitle, image, sound, time);
#endif
  }

std::string_view GameSession::messageFromSvm(std::string_view id, int voice) const {
  if(!wrld)
    return "";
  return vm->messageFromSvm(id,voice);
  }

std::string_view GameSession::messageByName(std::string_view id) const {
  if(!wrld)
    return "";
  return vm->messageByName(id);
  }

uint32_t GameSession::messageTime(std::string_view id) const {
  if(!wrld)
    return 0;
  return vm->messageTime(id);
  }

AiOuputPipe *GameSession::openDlgOuput(Npc &player, Npc &npc) {
  AiOuputPipe* ret=nullptr;
  Gothic::inst().openDialogPipe(player, npc, ret);
  return ret;
  }

bool GameSession::isNpcInDialog(const Npc& npc) const {
  return Gothic::inst().isNpcInDialog(npc);
  }

bool GameSession::isInDialog() const {
  return Gothic::inst().isInDialog();
  }

bool GameSession::isWorldKnown(std::string_view name) const {
  for(auto& i:visitedWorlds)
    if(i.name==name)
      return true;
  return false;
  }

void GameSession::initPerceptions() {
  // NOTE: world is null at this point and most scrip-api will be prone to crash
  if(vm->hasSymbolName("initPerceptions"))
    vm->getVm().call_function("initPerceptions");
  }

void GameSession::initScripts(bool firstTime) {
  auto wname = wrld->name();
  auto dot   = wname.rfind('.');
  auto name  = (dot==std::string::npos ? wname : wname.substr(0,dot));

  if(firstTime) {
    if(vm->hasSymbolName("startup_global"))
      vm->getVm().call_function("startup_global");

    string_frm startup("startup_", name);
    if(vm->hasSymbolName(startup))
      vm->getVm().call_function(startup);
    }

  if(vm->hasSymbolName("init_global"))
    vm->getVm().call_function("init_global");

  string_frm init("init_",name);
  if(vm->hasSymbolName(init))
    vm->getVm().call_function(init);

  wrld->resetPositionToTA();
  }
