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

} // namespace

using GameSessionMmoRestoreDetail::applyMmoNpcRoutineAuthorityState;
using GameSessionMmoRestoreDetail::canReuseMmoDbContinuePreWorldSnapshot;
using GameSessionMmoRestoreDetail::latestMmoBootstrapSnapshot;
using GameSessionMmoRestoreDetail::loadMmoDbContinuePreWorldClock;

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
          ? "game/game/gamesession_startup.cpp:GameSession::GameSession(db-continue)"
          : "game/game/gamesession_startup.cpp:GameSession::GameSession(new/pre-start)";
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


void GameSession::setupSettings() {
  const float soundVolume = Gothic::settingsSoundVolume();
  sound.setGlobalVolume(soundVolume);
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
