#include "gamesession.h"
#include "savegameheader.h"
#include "mmoruntimesqlite.h"
#include "mmosemantichooks.h"

#include <Tempest/Log>
#include <Tempest/MemReader>
#include <Tempest/MemWriter>
#include <cctype>
#include <cmath>

#include "utils/string_frm.h"
#include "worldstatestorage.h"
#include "world/objects/npc.h"
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


GameSession::GameSession(std::string file) {
  cam.reset(new Camera());

  Gothic::inst().setLoadingProgress(0);
  setupSettings();
  setTime(gtime(8,0));

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
  wrld->triggerOnStart(true);
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
  }

void GameSession::recordMmoSaveSlot(std::string_view slotPath, std::string_view displayName) {
  if(mmoSqlite!=nullptr)
    mmoSqlite->recordSaveSlot(*this, slotPath, displayName);
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



