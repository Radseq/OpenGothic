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
#include "gamesession_mmo_restore_detail.h"

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
constexpr uint64_t MmoServerSnapshotPollInterval = 250;
constexpr uint64_t MmoServerSnapshotWaitingLogDelay = 10000;
constexpr uint64_t MmoServerLiveSnapshotPollInterval = 500;

struct MmoNpcIdentity final {
  bool        valid = false;
  std::string world;
  std::size_t persistentId = std::size_t(-1);
  std::size_t symbolIndex = std::size_t(-1);
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


} // namespace

namespace GameSessionMmoRestoreDetail {

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

} // namespace GameSessionMmoRestoreDetail

namespace {

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

using GameSessionMmoRestoreDetail::applyMmoNpcRoutineAuthorityState;

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
