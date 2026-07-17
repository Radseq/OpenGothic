#include "gamescript.h"

#include <cstdint>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <Tempest/Log>
#include <Tempest/SoundEffect>

#include <cctype>

#include "game/compatibility/directmemory.h"
#include "game/definitions/spelldefinitions.h"
#include "game/serialize.h"
#include "utils/string_frm.h"
#include "world/objects/npc.h"
#include "world/objects/item.h"
#include "world/objects/interactive.h"
#include "world/triggers/abstracttrigger.h"
#include "graphics/visualfx.h"
#include "utils/fileutil.h"
#include "commandline.h"
#include "gothic.h"
#include "mmosemantichooks.h"

using namespace Tempest;

template <typename T>
struct ScopeVar final {
  ScopeVar(zenkit::DaedalusSymbol& sym, const std::shared_ptr<T>& h) : prev(sym.get_instance()), sym(sym) {
    sym.set_instance(h);
    }

  ScopeVar(const ScopeVar&)=delete;
  ~ScopeVar(){
    sym.set_instance(prev);
    }

  std::shared_ptr<zenkit::DaedalusInstance> prev;
  zenkit::DaedalusSymbol&                   sym;
  };


void GameScript::initializeInstanceNpc(const std::shared_ptr<zenkit::INpc>& npc, size_t instance) {
  auto sym = vm.find_symbol_by_index(uint32_t(instance));

  if(sym == nullptr) {
    Tempest::Log::e("Cannot initialize NPC ", instance, ": Symbol not found.");
    return;
    }

  vm.init_instance(npc, sym);

  if(npc->daily_routine!=0) {
    ScopeVar self(*vm.global_self(), npc);
    auto* daily_routine = vm.find_symbol_by_index(uint32_t(npc->daily_routine));

    if(daily_routine != nullptr) {
      vm.call_function(daily_routine);
      }
    }
  }

bool GameScript::isDead(const Npc &pl) {
  return pl.isInState(ZS_Dead);
  }

bool GameScript::isUnconscious(const Npc &pl) {
  return pl.isInState(ZS_Unconscious);
  }

bool GameScript::isTalk(const Npc &pl) {
  return pl.isInState(ZS_Talk);
  }

bool GameScript::isAttack(const Npc& pl) const {
  return pl.isInState(ZS_Attack) || pl.isInState(ZS_MM_Attack);
  }

Attitude GameScript::guildAttitude(const Npc &p0, const Npc &p1) const {
  auto selfG = std::min<size_t>(gilCount-1,p0.guild());
  auto npcG  = std::min<size_t>(gilCount-1,p1.guild());
  auto ret   = gilAttitudes[selfG*gilCount+npcG];
  return Attitude(ret);
  }

Attitude GameScript::personAttitude(const Npc &p0, const Npc &p1) const {
  if(!p0.isPlayer() && !p1.isPlayer())
    return guildAttitude(p0,p1);

  Attitude att=ATT_NULL;
  const Npc& npc = p0.isPlayer() ? p1 : p0;
  att = npc.attitude();
  if(att!=ATT_NULL)
    return att;
  att = guildAttitude(p0,p1);
  return att;
  }

bool GameScript::isFriendlyFire(const Npc& src, const Npc& dst) const {
  const int AIV_PARTYMEMBER = (owner.version().game==2) ? 15 : 36;
  if(src.isPlayer())
    return false;
  if(personAttitude(src, dst)==ATT_FRIENDLY)
    return true;
  if(src.handlePtr()->aivar[AIV_PARTYMEMBER]!=0 && dst.isPlayer())
    return true;
  return false;
  }

BodyState GameScript::schemeToBodystate(std::string_view sc) {
  if(searchScheme(sc,"MOB_SIT"))
    return BS_SIT;
  if(searchScheme(sc,"MOB_LIE"))
    return BS_LIE;
  if(searchScheme(sc,"MOB_CLIMB"))
    return BS_CLIMB;
  if(searchScheme(sc,"MOB_NOTINTERRUPTABLE"))
    return BS_MOBINTERACT;
  return BS_MOBINTERACT_INTERRUPT;
  }

Npc* GameScript::findNpc(zenkit::DaedalusSymbol* s) {
  if(s->is_instance_of<zenkit::INpc>()) {
    auto cNpc = reinterpret_cast<zenkit::INpc*>(s->get_instance().get());
    return findNpc(cNpc);
    }
  return nullptr;
  }

Npc* GameScript::findNpc(zenkit::INpc *handle) {
  if(handle==nullptr)
    return nullptr;
  assert(handle->user_ptr); // engine bug, if null
  auto a = reinterpret_cast<Npc*>(handle->user_ptr);
  //auto v = a->position();
  //auto x = a->displayName();
  //append_unique("logs/npcxxx.txt", x, a->handle().id, v.x, v.y, v.z);
  return a;
}

Npc* GameScript::findNpc(const std::shared_ptr<zenkit::INpc>& handle) {
  if(handle==nullptr)
    return nullptr;
  assert(handle->user_ptr); // engine bug, if null

  auto a = reinterpret_cast<Npc*>(handle->user_ptr);

  //auto v = a->position();
  //auto x = a->displayName();
  //append_unique("logs/npcxxx.txt", x, a->handle().id, v.x, v.y, v.z);
  return a;
  }

Npc* GameScript::findNpcById(const std::shared_ptr<zenkit::DaedalusInstance>& handle) {
  if(handle==nullptr)
    return nullptr;
  if(auto npc = dynamic_cast<const zenkit::INpc*>(handle.get())) {
    assert(npc->user_ptr); // engine bug, if null
    return reinterpret_cast<Npc*>(npc->user_ptr);
    }
  return findNpcById(handle->symbol_index());
  }

Npc* GameScript::findNpcById(size_t id) {
  auto* handle = vm.find_symbol_by_index(uint32_t(id));
  if(handle==nullptr || !handle->is_instance_of<zenkit::INpc>())
    return nullptr;

  auto hnpc = reinterpret_cast<zenkit::INpc*>(handle->get_instance().get());
  if(hnpc==nullptr) {
    auto obj = world().findNpcByInstance(id);
    handle->set_instance(obj ? obj->handlePtr() : nullptr);
    hnpc = reinterpret_cast<zenkit::INpc*>(handle->get_instance().get());
    }
  return findNpc(hnpc);
  }

void GameScript::setInstanceNPC(std::string_view name, Npc &npc) {
  auto sym = vm.find_symbol_by_name(name);
  if(sym == nullptr) {
    Tempest::Log::e("Cannot set NPC instance ", name, ": Symbol not found.");
    return;
    }
  sym->set_instance(npc.handlePtr());
  }

ScriptFn GameScript::playerPercAssessMagic() {
  auto id = vm.find_symbol_by_name("PLAYER_PERC_ASSESSMAGIC");
  if(id==nullptr)
    return ScriptFn();

  if(id->count()>0)
    return ScriptFn(uint32_t(id->get_int()));
  return ScriptFn();
  }

int GameScript::npcDamDiveTime() {
  auto id = vm.find_symbol_by_name("NPC_DAM_DIVE_TIME");
  if(id==nullptr)
    return 0;
  return id->get_int();
  }

int32_t GameScript::criticalDamageMultiplyer() const {
  return damCriticalMultiplier;
  }

void GameScript::npc_settofightmode(std::shared_ptr<zenkit::INpc> npcRef, int weaponSymbol) {
  if(npcRef!=nullptr && weaponSymbol>=0)
    findNpc(npcRef.get())->setToFightMode(size_t(weaponSymbol));
  }

void GameScript::npc_settofistmode(std::shared_ptr<zenkit::INpc> npcRef) {
  auto npc = findNpc(npcRef);
  if(npc!=nullptr)
    npc->setToFistMode();
  }

bool GameScript::npc_isinstate(std::shared_ptr<zenkit::INpc> npcRef, int stateFn) {
  auto npc = findNpc(npcRef);
  if(npc!=nullptr)
    return npc->isInState(uint32_t(stateFn));
  return false;
  }

bool GameScript::npc_isinroutine(std::shared_ptr<zenkit::INpc> npcRef, int stateFn) {
  auto npc = findNpc(npcRef);
  if(npc!=nullptr)
    return npc->isInRoutine(uint32_t(stateFn));
  return false;
  }

bool GameScript::npc_wasinstate(std::shared_ptr<zenkit::INpc> npcRef, int stateFn) {
  auto npc = findNpc(npcRef);
  if(npc!=nullptr)
    return npc->wasInState(uint32_t(stateFn));
  return false;
  }

int GameScript::npc_getdisttowp(std::shared_ptr<zenkit::INpc> npcRef, std::string_view wpname) {
  auto  npc = findNpc(npcRef);
  //NOTE: in CoM, some way-point and free-points share same name - need to be precise
  auto* wp  = world().findWayPoint(wpname);

  if(npc!=nullptr && wp!=nullptr){
    float ret = std::sqrt(npc->qDistTo(wp));
    if(ret<float(std::numeric_limits<int32_t>::max()))
      return int32_t(ret); else
      return std::numeric_limits<int32_t>::max();
    } else {
    return std::numeric_limits<int32_t>::max();
    }
  }

void GameScript::npc_exchangeroutine(std::shared_ptr<zenkit::INpc> npcRef, std::string_view rname) {
  auto npc = findNpc(npcRef);
  if(npc!=nullptr) {
    auto& v = npc->handle();
    string_frm name("Rtn_",rname,'_',v.id);

    auto* sym = vm.find_symbol_by_name(name);
    size_t d = sym != nullptr ? sym->index() : 0;
    if(d>0)
      npc->excRoutine(d);
    }
  }

bool GameScript::npc_isdead(std::shared_ptr<zenkit::INpc> npcRef) {
  auto npc = findNpc(npcRef);
  return npc==nullptr || isDead(*npc);
  }

void GameScript::npc_settalentskill(std::shared_ptr<zenkit::INpc> npcRef, int t, int lvl) {
  auto npc = findNpc(npcRef);
  if(npc!=nullptr)
    npc->setTalentSkill(Talent(t),lvl);
  }

int GameScript::npc_gettalentskill(std::shared_ptr<zenkit::INpc> npcRef, int skillId) {
  auto npc = findNpc(npcRef);
  return npc==nullptr ? 0 : npc->talentSkill(Talent(skillId));
  }

void GameScript::npc_settalentvalue(std::shared_ptr<zenkit::INpc> npcRef, int t, int lvl) {
  auto npc = findNpc(npcRef);
  if(npc!=nullptr)
    npc->setTalentValue(Talent(t),lvl);
  }

int GameScript::npc_gettalentvalue(std::shared_ptr<zenkit::INpc> npcRef, int skillId) {
  auto npc = findNpc(npcRef);
  return npc==nullptr ? 0 : npc->talentValue(Talent(skillId));
  }

void GameScript::npc_setrefusetalk(std::shared_ptr<zenkit::INpc> npcRef, int timeSec) {
  auto npc = findNpc(npcRef);
  if(npc)
    npc->setRefuseTalk(uint64_t(std::max(timeSec*1000,0)));
  }

bool GameScript::npc_refusetalk(std::shared_ptr<zenkit::INpc> npcRef) {
  auto npc = findNpc(npcRef);
  return npc && npc->isRefuseTalk();
  }

int GameScript::npc_getbodystate(std::shared_ptr<zenkit::INpc> npcRef) {
  auto npc = findNpc(npcRef);

  if(npc!=nullptr)
    return int32_t(npc->bodyState());
  return int32_t(0);
  }

std::shared_ptr<zenkit::INpc> GameScript::npc_getlookattarget(std::shared_ptr<zenkit::INpc> npcRef) {
  auto npc = findNpc(npcRef);
  return npc && npc->lookAtTarget() ? npc->lookAtTarget()->handlePtr() : nullptr;
  }

int GameScript::npc_getdisttonpc(std::shared_ptr<zenkit::INpc> aRef, std::shared_ptr<zenkit::INpc> bRef) {
  auto a = findNpc(aRef);
  auto b = findNpc(bRef);

  if(a==nullptr || b==nullptr)
    return std::numeric_limits<int32_t>::max();

  float ret = std::sqrt(a->qDistTo(*b));
  if(ret>float(std::numeric_limits<int32_t>::max()))
    return std::numeric_limits<int32_t>::max();
  return int(ret);
  }

void GameScript::npc_setperctime(std::shared_ptr<zenkit::INpc> npcRef, float sec) {
  auto npc = findNpc(npcRef);
  if(npc)
    npc->setPerceptionTime(uint64_t(sec*1000));
  }

void GameScript::npc_percenable(std::shared_ptr<zenkit::INpc> npcRef, int pr, int fn) {
  auto npc = findNpc(npcRef);
  if(npc && fn>=0)
    npc->setPerceptionEnable(PercType(pr),size_t(fn));
  }

void GameScript::npc_percdisable(std::shared_ptr<zenkit::INpc> npcRef, int pr) {
  auto npc = findNpc(npcRef);
  if(npc)
    npc->setPerceptionDisable(PercType(pr));
  }

std::string GameScript::npc_getnearestwp(std::shared_ptr<zenkit::INpc> npcRef) {
  auto npc = findNpc(npcRef);
  auto wp  = npc ? world().findWayPoint(npc->position()) : nullptr;
  if(wp)
    return wp->name;
  return "";
  }

std::string GameScript::npc_getnextwp(std::shared_ptr<zenkit::INpc> npcRef) {
  auto npc = findNpc(npcRef);
  auto wp  = npc ? world().findNextWayPoint(*npc) : nullptr;
  if(wp)
    return wp->name;
  return "";
  }

void GameScript::npc_clearaiqueue(std::shared_ptr<zenkit::INpc> npcRef) {
  auto npc = findNpc(npcRef);
  if(npc)
    npc->clearAiQueue();
  }

bool GameScript::npc_isplayer(std::shared_ptr<zenkit::INpc> npcRef) {
  auto npc = findNpc(npcRef);
  return npc && npc->isPlayer();
  }

int GameScript::npc_getstatetime(std::shared_ptr<zenkit::INpc> npcRef) {
  auto npc = findNpc(npcRef);
  if(npc)
    return int32_t(npc->stateTime()/1000);
  return 0;
  }

void GameScript::npc_setstatetime(std::shared_ptr<zenkit::INpc> npcRef, int val) {
  auto npc = findNpc(npcRef);
  if(npc)
    npc->setStateTime(val*1000);
  }

void GameScript::npc_changeattribute(std::shared_ptr<zenkit::INpc> npcRef, int atr, int val) {
  auto npc = findNpc(npcRef);
  if(npc!=nullptr && atr>=0)
    npc->changeAttribute(Attribute(atr),val,false);
  }

bool GameScript::npc_isonfp(std::shared_ptr<zenkit::INpc> npcRef, std::string_view val) {
  auto npc = findNpc(npcRef);
  if(npc==nullptr)
    return false;

  auto w = npc->currentWayPoint();
  if(w==nullptr || !MoveAlgo::isClose(*npc,*w,MAX_AI_USE_DISTANCE) || !w->checkName(val))
    return false;
  return w->isFreePoint();
  }

int GameScript::npc_getheighttonpc(std::shared_ptr<zenkit::INpc> aRef, std::shared_ptr<zenkit::INpc> bRef) {
  auto a = findNpc(aRef);
  auto b = findNpc(bRef);
  float ret = 0;
  if(a!=nullptr && b!=nullptr)
    ret = std::abs(a->position().y - b->position().y);
  return int32_t(ret);
  }

bool GameScript::npc_canseenpc(std::shared_ptr<zenkit::INpc> npcRef, std::shared_ptr<zenkit::INpc> otherRef) {
  // 'see' functions are intended as ray-cast, ignoring hnpc->senses mask
  // https://discord.com/channels/989316194148433950/989333514543587339/1226664463697182760
  auto other = findNpc(otherRef);
  auto npc   = findNpc(npcRef);

  if(npc!=nullptr && other!=nullptr){
    return npc->canSeeNpc(*other,false);
    }
  return false;
  }

bool GameScript::npc_canseenpcfreelos(std::shared_ptr<zenkit::INpc> npcRef, std::shared_ptr<zenkit::INpc> otherRef) {
  auto npc = findNpc(npcRef);
  auto oth = findNpc(otherRef);

  if(npc!=nullptr && oth!=nullptr){
    return npc->canSeeNpc(*oth,true);
    }
  return false;
  }

bool GameScript::npc_canseeitem(std::shared_ptr<zenkit::INpc> npcRef, std::shared_ptr<zenkit::IItem> itemRef) {
  auto npc = findNpc(npcRef);
  auto itm = findItem(itemRef.get());

  if(npc!=nullptr && itm!=nullptr){
    return npc->canSeeItem(*itm,false);
    }
  return false;
  }

bool GameScript::npc_isinfightmode(std::shared_ptr<zenkit::INpc> npcRef, int modeI) {
  auto npc  = findNpc(npcRef);
  auto mode = FightMode(modeI);

  if(npc==nullptr){
    return false;
    }

  auto st  = npc->weaponState();
  bool ret = false;
  if(mode==FightMode::FMODE_NONE){
    ret = (st==WeaponState::NoWeapon);
    }
  else if(mode==FightMode::FMODE_FIST){
    ret = (st==WeaponState::Fist);
    }
  else if(mode==FightMode::FMODE_MELEE){
    ret = (st==WeaponState::W1H || st==WeaponState::W2H);
    }
  else if(mode==FightMode::FMODE_FAR){
    ret = (st==WeaponState::Bow || st==WeaponState::CBow);
    }
  else if(mode==FightMode::FMODE_MAGIC){
    ret = (st==WeaponState::Mage);
    }
  return ret;
  }

void GameScript::npc_settarget(std::shared_ptr<zenkit::INpc> npcRef, std::shared_ptr<zenkit::INpc> otherRef) {
  auto oth = findNpc(otherRef);
  auto npc = findNpc(npcRef);
  if(npc)
    npc->setTarget(oth);
  }

/**
 * @brief WorldScript::npc_gettarget
 * Fill 'other' with the current npc target.
 * set by Npc_SetTarget () or Npc_GetNextTarget ().
 * - return: current target saved -> TRUE
 * no target saved -> FALSE
 */
bool GameScript::npc_gettarget(std::shared_ptr<zenkit::INpc> npcRef) {
  auto npc = findNpc(npcRef);
  auto s   = vm.global_other();

  if(npc!=nullptr && npc->target()) {
    s->set_instance(npc->target()->handlePtr());
    return true;
    }

  s->set_instance(nullptr);
  return false;
  }

bool GameScript::npc_getnexttarget(std::shared_ptr<zenkit::INpc> npcRef) {
  auto npc = findNpc(npcRef);
  Npc* ret = nullptr;

  if(npc!=nullptr){
    float dist = float(npc->handle().senses_range);
    dist*=dist;

    world().detectNpc(npc->position(),float(npc->handle().senses_range),[&,npc](Npc& oth){
      if(&oth!=npc && !oth.isDown() && oth.isEnemy(*npc) && npc->canSenseNpc(oth,true)!=SensesBit::SENSE_NONE){
        float qd = oth.qDistTo(*npc);
        if(qd<dist){
          dist=qd;
          ret = &oth;
          }
        }
      return false;
      });
    if(ret!=nullptr)
      npc->setTarget(ret);
    }

  auto s = vm.global_other();
  if(ret!=nullptr) {
    s->set_instance(ret->handlePtr());
    return true;
    } else {
    s->set_instance(nullptr);
    return false;
    }
  }

void GameScript::npc_sendpassiveperc(std::shared_ptr<zenkit::INpc> npcRef, int id, std::shared_ptr<zenkit::INpc> victimRef, std::shared_ptr<zenkit::INpc> otherRef) {
  auto npc    = findNpc(npcRef);
  auto other  = findNpc(otherRef);
  auto victim = findNpc(victimRef);

  if(npc && other && victim)
    world().sendPassivePerc(*npc,*other,*victim,id);
  else if(npc && other)
    world().sendPassivePerc(*npc,*other,id);
  }

void GameScript::npc_sendsingleperc(std::shared_ptr<zenkit::INpc> npcRef, std::shared_ptr<zenkit::INpc> otherRef, int id) {
  auto other  = findNpc(otherRef);
  auto npc    = findNpc(npcRef);

  if(npc && other)
    other->perceptionProcess(*npc,nullptr,0,PercType(id));
  }

bool GameScript::npc_checkinfo(std::shared_ptr<zenkit::INpc> npcRef, int imp) {
  auto n    = findNpc(npcRef);
  auto hero = findNpc(vm.global_other());
  if(n==nullptr || hero==nullptr)
    return false;

  auto& pl  = hero->handle();
  auto& npc = n->handle();
  for(auto& info:dialogsInfo) {
    if(info->npc!=int32_t(npc.symbol_index()) || info->important!=imp)
      continue;
    bool npcKnowsInfo = doesNpcKnowInfo(pl,info->symbol_index());
    if(npcKnowsInfo && !info->permanent)
      continue;
    bool valid=false;
    if(info->condition) {
      auto* conditionSymbol = vm.find_symbol_by_index(uint32_t(info->condition));
      if (conditionSymbol != nullptr)
        valid = vm.call_function<int>(conditionSymbol)!=0;
      }
    if(valid) {
      return true;
      }
    }
  return false;
  }

int GameScript::npc_getportalguild(std::shared_ptr<zenkit::INpc> npcRef) {
  int32_t g   = GIL_NONE;
  auto    npc = findNpc(npcRef);
  if(npc!=nullptr)
    g = world().guildOfRoom(npc->portalName());
  return g;
  }

bool GameScript::npc_isinplayersroom(std::shared_ptr<zenkit::INpc> npcRef) {
  auto npc = findNpc(npcRef);
  auto pl  = world().player();

  if(npc!=nullptr && pl!=nullptr) {
    auto g1 = pl ->portalName();
    auto g2 = npc->portalName();
    if(g1==g2)
      return true;
    }
  return false;
  }

void GameScript::npc_perceiveall(std::shared_ptr<zenkit::INpc> npcRef) {
  (void)npcRef; // nop
  }

void GameScript::npc_stopani(std::shared_ptr<zenkit::INpc> npcRef, std::string_view name) {
  auto npc = findNpc(npcRef);
  if(npc!=nullptr)
    npc->stopAnim(name);
  }

int GameScript::npc_settrueguild(std::shared_ptr<zenkit::INpc> npcRef, int gil) {
  auto npc = findNpc(npcRef);
  if(npc!=nullptr)
    npc->setTrueGuild(gil);
  return 0;
  }

int GameScript::npc_gettrueguild(std::shared_ptr<zenkit::INpc> npcRef) {
  auto npc = findNpc(npcRef);
  if(npc!=nullptr)
    return npc->trueGuild();
  return int32_t(GIL_NONE);
  }

int GameScript::npc_getattitude(std::shared_ptr<zenkit::INpc> aRef, std::shared_ptr<zenkit::INpc> bRef) {
  auto a = findNpc(aRef);
  auto b = findNpc(bRef);

  if(a!=nullptr && b!=nullptr){
    auto att=personAttitude(*a,*b);
    return att; //TODO: temp attitudes
    }
  return ATT_NEUTRAL;
  }

int GameScript::npc_getpermattitude(std::shared_ptr<zenkit::INpc> aRef, std::shared_ptr<zenkit::INpc> bRef) {
  auto a = findNpc(aRef);
  auto b = findNpc(bRef);

  if(a!=nullptr && b!=nullptr){
    auto att=personAttitude(*a,*b);
    return att;
    }
  return ATT_NEUTRAL;
  }

void GameScript::npc_setattitude(std::shared_ptr<zenkit::INpc> npcRef, int att) {
  auto npc = findNpc(npcRef);
  if(npc!=nullptr)
    npc->setAttitude(Attitude(att));
  }

void GameScript::npc_settempattitude(std::shared_ptr<zenkit::INpc> npcRef, int att) {
  auto npc = findNpc(npcRef);
  if(npc!=nullptr)
    npc->setTempAttitude(Attitude(att));
  }

bool GameScript::npc_hasbodyflag(std::shared_ptr<zenkit::INpc> npcRef, int bodyflag) {
  auto npc = findNpc(npcRef);
  if(npc==nullptr)
    return false;
  return npc->hasStateFlag(BodyState(bodyflag));
  }

int GameScript::npc_getlasthitspellid(std::shared_ptr<zenkit::INpc> npcRef) {
  auto npc = findNpc(npcRef);
  if(npc==nullptr){
    return 0;
    }
  return npc->lastHitSpellId();
  }

int GameScript::npc_getlasthitspellcat(std::shared_ptr<zenkit::INpc> npcRef) {
  auto npc = findNpc(npcRef);
  if(npc==nullptr)
    return SPELL_GOOD;

  const int id    = npc->lastHitSpellId();
  auto&     spell = spellDesc(id);
  return spell.spell_type;
  }

void GameScript::npc_playani(std::shared_ptr<zenkit::INpc> npcRef, std::string_view name) {
  auto npc = findNpc(npcRef);
  if(npc!=nullptr)
    npc->playAnimByName(name,BS_NONE);
  }

bool GameScript::npc_isdetectedmobownedbynpc(std::shared_ptr<zenkit::INpc> usrRef, std::shared_ptr<zenkit::INpc> npcRef) {
  auto npc = findNpc(npcRef);
  auto usr = findNpc(usrRef);

  if(npc!=nullptr && usr!=nullptr && usr->interactive()!=nullptr){
    auto* inst = vm.find_symbol_by_index(npc->instanceSymbol());
    auto  ow   = usr->interactive()->ownerName();
    return inst->name() == ow;
    }
  return false;
  }

bool GameScript::npc_isdetectedmobownedbyguild(std::shared_ptr<zenkit::INpc> npcRef, int guild) {
  static bool first=true;
  if(first){
    Log::e("not implemented call [npc_isdetectedmobownedbyguild]");
    first=false;
    }

  auto npc = findNpc(npcRef);
  (void)guild;

  if(npc!=nullptr && npc->detectedMob()!=nullptr) {
    auto  ow   = npc->detectedMob()->ownerName();
    (void)ow;
    //vm.setReturn(inst.name==ow ? 1 : 0);
    return false;
    }
  return false;
  }

std::string GameScript::npc_getdetectedmob(std::shared_ptr<zenkit::INpc> npcRef) {
  auto usr = findNpc(npcRef);
  if(usr!=nullptr && usr->detectedMob()!=nullptr){
    auto i = usr->detectedMob();
    return std::string(i->schemeName());
    }
  return "";
  }

bool GameScript::npc_canseesource(std::shared_ptr<zenkit::INpc> npcRef) {
  auto self = findNpc(npcRef);
  if(!self)
    return false;
  return self->canSeeSource();
  }

// Used (only?) in Gothic 1 in B_AssessEnemy, to prevent attacks during cutscenes.
bool GameScript::npc_isincutscene(std::shared_ptr<zenkit::INpc> npcRef) {
  auto npc = findNpc(npcRef);
  auto w = Gothic::inst().world();
  if(w==nullptr)
    return false;

  if(npc!=nullptr && owner.isNpcInDialog(*npc))
    return true;

  return false;
  }

int GameScript::npc_getdisttoplayer(std::shared_ptr<zenkit::INpc> npcRef) {
  auto pl  = world().player();
  auto npc = findNpc(npcRef);
  if(pl==nullptr || npc==nullptr) {
    return std::numeric_limits<int32_t>::max();
    }
  auto dp = pl->position()-npc->position();
  auto l  = dp.length();
  if(l>float(std::numeric_limits<int32_t>::max())) {
    return std::numeric_limits<int32_t>::max();
    }
  return int32_t(l);
  }

bool GameScript::hlp_isvalidnpc(std::shared_ptr<zenkit::INpc> npcRef) {
  auto self = findNpc(npcRef);
  return self != nullptr;
  }

std::shared_ptr<zenkit::INpc> GameScript::hlp_getnpc(int instanceSymbol) {
  auto npc = findNpcById(uint32_t(instanceSymbol));
  if(npc != nullptr)
    return npc->handlePtr();
  else
    return nullptr;
  }
