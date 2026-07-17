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
constexpr float  MmoNpcAuthoritySaveSampleRadius = 60000.f;
constexpr size_t MmoNpcAuthoritySaveSampleMaxPerSweep = 512;
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
                                       "game/game/gamesession_persistence.cpp:GameSession::GameSession(save)",
                                       "save_session_loaded");
  waitForMmoServerSnapshotRestoreDuringLoad();
  }

GameSession::~GameSession() {
#if OPENGOTHIC_MMO_SQLITE_TOOLING
  if(mmoSqlite!=nullptr)
    mmoSqlite->flush(*this);
#endif
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
