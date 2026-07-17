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

float checkpointYawDelta(float a, float b) noexcept {
  float d = std::fabs(a - b);
  if(d > 360.f)
    d = std::fmod(d, 360.f);
  if(d > 180.f)
    d = 360.f - d;
  return d;
}


constexpr uint64_t MmoNpcAuthoritySampleInterval = 2500;
constexpr uint64_t MmoNpcAuthoritySampleStaleInterval = 10000;
constexpr uint64_t MmoNpcAuthoritySamplePruneInterval = 60000;
constexpr float    MmoNpcAuthoritySampleRadius = 12000.f;
constexpr size_t   MmoNpcAuthoritySampleMaxPerSweep = 8;

void hashCombine(std::uint64_t& seed, std::uint64_t value) noexcept {
  seed ^= value + 0x9E3779B97F4A7C15ull + (seed << 6) + (seed >> 2);
}

std::uint64_t hashString(std::string_view value) noexcept {
  return static_cast<std::uint64_t>(std::hash<std::string_view>{}(value));
}

std::uint64_t quantizedFloatHash(float value, float scale) noexcept {
  return static_cast<std::uint64_t>(static_cast<std::int64_t>(std::lround(value / scale)));
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


void GameSession::updateAnimation(uint64_t dt) {
  if(wrld)
    wrld->updateAnimation(dt);
  }
