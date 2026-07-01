#include "gamesession.h"
#include "savegameheader.h"
#include "mmoruntimesqlite.h"
#include "mmosemantichooks.h"
#include "mmorestoresnapshot.h"

#include <Tempest/Log>
#include <Tempest/MemReader>
#include <Tempest/MemWriter>
#include <algorithm>
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

#include "utils/string_frm.h"
#include "worldstatestorage.h"
#include "world/objects/npc.h"
#include "world/objects/item.h"
#include "world/objects/interactive.h"
#include "world/world.h"
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

std::filesystem::path snapshotTmpPath(std::string_view path) {
  auto out = std::filesystem::path(std::string(path));
  out += ".tmp";
  return out;
}

std::uint32_t readMmoSnapshotManifestId() noexcept {
  try {
    std::ifstream in("runtime/mmo_server_bootstrap_snapshot_manifest.json", std::ios::in | std::ios::binary);
    if(!in.is_open())
      return 0;
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    constexpr std::string_view Key = "\"snapshot_id\"";
    auto pos = text.find(Key);
    if(pos == std::string::npos)
      return 0;
    pos = text.find(':', pos + Key.size());
    if(pos == std::string::npos)
      return 0;
    ++pos;
    while(pos < text.size() && static_cast<unsigned char>(text[pos]) <= ' ')
      ++pos;
    std::uint32_t out = 0;
    const auto* begin = text.data() + pos;
    const auto* end = text.data() + text.size();
    const auto r = std::from_chars(begin, end, out);
    if(r.ec != std::errc{})
      return 0;
    return out;
    }
  catch(...) {
    return 0;
    }
}

bool canReuseMmoDbContinuePreWorldSnapshot() noexcept {
  const auto& cmd = CommandLine::inst();
  if(!cmd.mmoClientUsesServer() || !cmd.mmoDbContinueWithoutNativeSave())
    return false;

  const auto pathView = cmd.mmoServerSnapshotJson();
  if(pathView.empty())
    return false;

  const std::filesystem::path path{std::string(pathView)};
  std::error_code ec;
  if(!std::filesystem::is_regular_file(path, ec))
    return false;

  const auto result = Mmo::RestoreSnapshot::loadAndValidateBootstrapSnapshot(pathView, "PC_HERO");
  if(!result.ok) {
    Log::e("MMO DB continue pre-world snapshot reuse rejected: ", result.message,
           " path=", std::string(pathView));
    return false;
  }

  if(result.snapshotSource != "db_save_checkpoint_v1") {
    Log::e("MMO DB continue pre-world snapshot reuse rejected: snapshot_source=",
           result.snapshotSource,
           " path=", std::string(pathView));
    return false;
  }

  Log::i("MMO DB continue pre-world snapshot reuse enabled",
         " world=", result.worldName,
         " manifest=", result.dbSaveCheckpointManifestUuid,
         " path=", std::string(pathView));
  return true;
}

bool loadMmoDbContinuePreWorldClock(gtime& out) noexcept {
  const auto& cmd = CommandLine::inst();
  if(!cmd.mmoClientUsesServer() || !cmd.mmoDbContinueWithoutNativeSave())
    return false;

  const auto pathView = cmd.mmoServerSnapshotJson();
  if(pathView.empty())
    return false;

  const auto result = Mmo::RestoreSnapshot::loadAndValidateBootstrapSnapshot(pathView, "PC_HERO");
  if(!result.ok) {
    Log::e("MMO DB continue pre-world clock rejected: ", result.message,
           " path=", std::string(pathView));
    return false;
    }

  if(!result.worldClock.present)
    return false;

  out = gtime::fromInt(result.worldClock.currentWorldTimeMs);
  Log::i("MMO DB continue pre-world clock selected: world_time_ms=", result.worldClock.currentWorldTimeMs,
         " hour=", out.hour(),
         " minute=", out.minute(),
         " world=", result.worldClock.worldName,
         " snapshot_source=", result.snapshotSource);
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
  if(cmd.mmoActionMovementProposalIntervalMs() == 0)
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

  if(dbContinueRequested) {
    Log::i("MMO DB continue baseline loaded: running existing-world startup trigger");
    wrld->triggerOnStart(false);
  } else {
    wrld->triggerOnStart(true);
  }
  cam->reset(wrld->player());
  Gothic::inst().setLoadingProgress(96);
  ticks = 1;
  if(!CommandLine::inst().mmoSqlite().empty()) {
    mmoSqlite.reset(new MmoRuntimeSqlite(std::string(CommandLine::inst().mmoSqlite()),
                                         CommandLine::inst().mmoSqliteIntervalMs(),
                                         CommandLine::inst().mmoSqliteRestore(),
                                         CommandLine::inst().mmoSqliteCaptureBaseline(),
                                         {}));
    mmoSqlite->open(*this);
    }
  consumeMmoRestoreSnapshot(dbContinueRequested ? "db_continue_session_loaded" : "new_game_session_loaded");
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
  if(!CommandLine::inst().mmoSqlite().empty()) {
    mmoSqlite.reset(new MmoRuntimeSqlite(std::string(CommandLine::inst().mmoSqlite()),
                                         CommandLine::inst().mmoSqliteIntervalMs(),
                                         CommandLine::inst().mmoSqliteRestore(),
                                         CommandLine::inst().mmoSqliteCaptureBaseline(),
                                         std::move(sourceSlot)));
    mmoSqlite->open(*this);
    }
  scheduleMmoServerSnapshotRestore("save_session_loaded");
  Mmo::Hooks::onClientBootstrapRequest(*wrld,
                                       "game/game/gamesession.cpp:GameSession::GameSession(save)",
                                       "save_session_loaded");
  waitForMmoServerSnapshotRestoreDuringLoad();
  consumeMmoRestoreSnapshot("save_session_loaded");
  }

GameSession::~GameSession() {
  if(mmoSqlite!=nullptr)
    mmoSqlite->flush(*this);
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
    if(auto* hero = wrld->player())
      Mmo::Hooks::onCharacterCheckpoint(*hero, "GameSession::save", "native_save_pre_manifest_checkpoint");
    }
  }

void GameSession::recordMmoSaveSlot(std::string_view slotPath, std::string_view displayName) {
  if(mmoSqlite!=nullptr)
    mmoSqlite->recordSaveSlot(*this, slotPath, displayName);

  if(wrld != nullptr && CommandLine::inst().mmoClientUsesServer())
    Mmo::Hooks::onSaveCheckpointManifest(*wrld,
                                         slotPath,
                                         displayName,
                                         "GameSession::recordMmoSaveSlot",
                                         "native_save_slot_recorded");
  }

void GameSession::setupSettings() {
  const float soundVolume = Gothic::settingsSoundVolume();
  sound.setGlobalVolume(soundVolume);
  }

void GameSession::setWorld(std::unique_ptr<World> &&w) {
  if(wrld) {
    if(!isWorldKnown(wrld->name()))
      visitedWorlds.emplace_back(*wrld);
    }
  wrld = std::move(w);
  lastMmoActionCheckpoint = {};
  lastMmoActionMovementProposal = {};
  }

std::unique_ptr<World> GameSession::clearWorld() {
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

void GameSession::scheduleMmoServerSnapshotRestore(std::string_view reason, bool reuseExistingSnapshot) noexcept {
  const auto& cmd = CommandLine::inst();
  if(!cmd.mmoClientUsesServer())
    return;

  mmoServerSnapshotRestore = {};
  mmoServerSnapshotRestore.requested = true;
  mmoServerSnapshotRestore.requestedAtTick = ticks;
  mmoServerSnapshotRestore.reason = std::string(reason);

  if(!reuseExistingSnapshot) {
    std::error_code ec;
    const auto path = std::filesystem::path(std::string(cmd.mmoServerSnapshotJson()));
    std::filesystem::remove(path, ec);
    ec.clear();
    std::filesystem::remove(snapshotTmpPath(cmd.mmoServerSnapshotJson()), ec);
    ec.clear();
    std::filesystem::remove("runtime/mmo_server_bootstrap_snapshot_manifest.json", ec);
    ec.clear();
    std::filesystem::remove("runtime/mmo_server_bootstrap_snapshot_manifest.json.tmp", ec);
  }

  Log::i("MMO server snapshot restore scheduled: server_bound=1 inventory=1 position=1 stats=1 story=1 world=1",
         " strict_db_checkpoint=", cmd.mmoRequireDbSaveCheckpointRestore() ? 1 : 0,
         " reuse_existing_snapshot=", reuseExistingSnapshot ? 1 : 0,
         " reason=", std::string(reason),
         " path=", std::string(cmd.mmoServerSnapshotJson()));
}

bool GameSession::tryApplyMmoServerSnapshotRestore(bool forcePoll) noexcept {
  auto& state = mmoServerSnapshotRestore;
  if(!state.requested || state.completed)
    return state.completed;
  if(!forcePoll && ticks < state.lastPollTick + MmoServerSnapshotPollInterval)
    return false;
  state.lastPollTick = ticks;

  const auto& cmd = CommandLine::inst();
  const auto pathView = cmd.mmoServerSnapshotJson();
  if(pathView.empty()) {
    Log::e("MMO server snapshot restore failed: empty snapshot path");
    state.completed = true;
    return true;
    }

  const auto path = std::filesystem::path(std::string(pathView));
  std::error_code ec;
  if(!std::filesystem::exists(path, ec)) {
    if(!state.waitingLogged && ticks >= state.requestedAtTick + MmoServerSnapshotWaitingLogDelay) {
      state.waitingLogged = true;
      Log::i("MMO server snapshot restore waiting for downloaded snapshot: path=", std::string(pathView));
      }
    return false;
    }

  const auto result = Mmo::RestoreSnapshot::loadAndValidateBootstrapSnapshot(pathView, "PC_HERO");
  if(!result.ok) {
    Log::e("MMO server snapshot restore rejected: ", result.message,
           " reason=", state.reason,
           " path=", std::string(pathView));
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
           " path=", std::string(pathView));
    state.completed = true;
    return true;
    }

  Log::i("MMO server snapshot restore source: source=", result.source,
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
           " path=", std::string(pathView));
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

  if(const auto snapshotId = readMmoSnapshotManifestId())
    state.lastAppliedSnapshotId = std::max(state.lastAppliedSnapshotId, snapshotId);
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

  auto& state = mmoServerSnapshotRestore;
  if(state.requested && !state.completed)
    return false;
  if(ticks < state.lastLiveRefreshPollTick + MmoServerLiveSnapshotPollInterval)
    return false;
  state.lastLiveRefreshPollTick = ticks;

  const auto snapshotId = readMmoSnapshotManifestId();
  if(snapshotId == 0 || snapshotId <= state.lastAppliedSnapshotId)
    return false;

  const auto pathView = cmd.mmoServerSnapshotJson();
  const auto result = Mmo::RestoreSnapshot::loadAndValidateBootstrapSnapshot(pathView, "PC_HERO");
  if(!result.ok) {
    Log::e("MMO server live world snapshot rejected: snapshot_id=", snapshotId,
           " message=", result.message,
           " path=", std::string(pathView));
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

  if(auto* pl = wrld->player()) {
    tickMmoMovementProposal(*pl, ticks);
    if(const char* reason = mmoActionCheckpointReason(*pl, ticks)) {
      Mmo::Hooks::onCharacterCheckpoint(*pl, "GameSession::tick", reason);
      recordMmoActionCheckpointState(*pl, ticks);
      }
    }

  if(mmoSqlite!=nullptr)
    mmoSqlite->tick(*this, dt);
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
  if(mmoSqlite!=nullptr) {
    mmoSqlite->recordDialogSelection(*this, player, npc, dlg, "update");
    mmoSqlite->recordDialogChoices(*this, player, npc, ret, "subchoices", false);
    }
  return ret;
  }

void GameSession::dialogExec(const GameScript::DlgChoice &dlg, Npc& player, Npc& npc) {
  markMmoServerSnapshotStoryDirty();
  if(mmoSqlite!=nullptr)
    mmoSqlite->recordDialogSelection(*this, player, npc, dlg, "exec");
  return vm->exec(dlg,player,npc);
  }

void GameSession::recordDialogChoices(Npc& player, Npc& npc,
                                      const std::vector<GameScript::DlgChoice>& choices,
                                      std::string_view phase, bool includeImportant) {
  if(mmoSqlite!=nullptr)
    mmoSqlite->recordDialogChoices(*this, player, npc, choices, phase, includeImportant);
  }

void GameSession::recordMmoChapterIntro(std::string_view title, std::string_view subtitle,
                                        std::string_view image, std::string_view sound, int time) {
  if(mmoSqlite!=nullptr)
    mmoSqlite->recordChapterIntro(*this, title, subtitle, image, sound, time);
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

void GameSession::consumeMmoRestoreSnapshot(std::string_view reason) noexcept {
  const auto& cmd = CommandLine::inst();
  const auto path = cmd.mmoRestoreSnapshotJson();
  if(path.empty())
    return;

  if(!cmd.mmoClientUsesServer()) {
    Log::i("MMO restore snapshot ignored: -mmo-restore-snapshot-json requires -mmo-client-server");
    return;
    }

  const auto result = Mmo::RestoreSnapshot::loadAndValidate(path, "PC_HERO", cmd.mmoActionSessionKey());
  if(!result.ok) {
    Log::e("MMO restore snapshot rejected: ", result.message,
           " reason=", reason,
           " path=", std::string(path));
    return;
    }

  Log::i("MMO restore snapshot validated: inventory=", result.inventoryCount,
         " equipment=", result.equipmentCount,
         " restore_items=", result.items.size(),
         " reason=", reason,
         " path=", std::string(path));

  if(!cmd.mmoRestoreSnapshotApply())
    return;

  auto* hero = player();
  if(hero==nullptr) {
    Log::e("MMO restore snapshot apply failed: player is not available");
    return;
    }

  std::vector<Npc::PersistentInventoryItem> items;
  items.reserve(result.items.size());
  for(const auto& item : result.items) {
    if(item.symbolIndex==size_t(-1) || item.count==0)
      continue;
    items.push_back({item.symbolIndex, item.count, item.equipped});
    }

  hero->restorePersistentInventory(items);
  Log::i("MMO restore snapshot applied: restore_items=", items.size(),
         " reason=", reason,
         " path=", std::string(path));
  }



