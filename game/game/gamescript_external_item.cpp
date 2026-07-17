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


void GameScript::initializeInstanceItem(const std::shared_ptr<zenkit::IItem>& item, size_t instance) {
  auto sym = vm.find_symbol_by_index(uint32_t(instance));

  if(sym == nullptr) {
    Tempest::Log::e("Cannot initialize item ", instance, ": Symbol not found.");
    return;
    }

  vm.init_instance(item, sym);
  }

void GameScript::storeItem(Item *itm) {
  auto* s = vm.global_item();
  if(itm!=nullptr) {
    s->set_instance(itm->handlePtr());
    } else {
    s->set_instance(nullptr);
    }
  }

const zenkit::ISpell& GameScript::spellDesc(int32_t splId) {
  auto& tag = spellFxInstanceNames->get_string(uint16_t(splId));
  return spells->find(tag);
  }

const VisualFx* GameScript::spellVfx(int32_t splId) {
  auto& tag = spellFxInstanceNames->get_string(uint16_t(splId));
  string_frm name("spellFX_",tag);
  return Gothic::inst().loadVisualFx(name);
  }

CollideMask GameScript::canNpcCollideWithSpell(Npc& npc, Npc* shooter, int32_t spellId) {
  if(owner.version().game==1) {
    auto& spl = spellDesc(spellId);
    if(npc.isTargetableBySpell(TargetType(spl.target_collect_type)))
      return COLL_DOEVERYTHING; else
      return COLL_DONOTHING;
    }

  auto fn   = vm.find_symbol_by_name("C_CanNpcCollideWithSpell");
  if(fn==nullptr)
    return COLL_DOEVERYTHING;

  ScopeVar self (*vm.global_self(),  npc.handlePtr());
  ScopeVar other(*vm.global_other(), shooter->handlePtr());
  return CollideMask(vm.call_function<int>(fn, spellId));
  }

// Gothic 1 only differentiates between the two worldmap types with and
// without the orc addition and does this inside the code, not the script
int GameScript::playerHotKeyScreenMap_G1(Npc& pl) {
  size_t map = findSymbolIndex("itwrworldmap_orc");
  if(map==size_t(-1) || pl.itemCount(map)<1)
    map = findSymbolIndex("itwrworldmap");

  if(map==size_t(-1) || pl.itemCount(map)<1)
    return -1;

  pl.useItem(map);

  return int(map);
  }

int GameScript::playerHotKeyScreenMap(Npc& pl) {
  auto fn   = vm.find_symbol_by_name("player_hotkey_screen_map");
  if(fn==nullptr) {
    if(owner.version().game==1)
      return playerHotKeyScreenMap_G1(pl);
    return -1;
    }

  ScopeVar self(*vm.global_self(), pl.handlePtr());
  int map = vm.call_function<int>(fn);
  if(map>=0)
    pl.useItem(size_t(map));
  return map;
  }

void GameScript::playerHotLamePotion(Npc& pl) {
  auto opt = Gothic::inst().settingsGetI("GAME", "usePotionKeys");
  if(opt==0)
    return;

  auto fn   = vm.find_symbol_by_name("player_hotkey_lame_potion");
  if(fn==nullptr)
    return;

  ScopeVar self(*vm.global_self(), pl.handlePtr());
  vm.call_function<void>(fn);
  }

void GameScript::playerHotLameHeal(Npc& pl) {
  auto opt = Gothic::inst().settingsGetI("GAME", "usePotionKeys");
  if(opt==0)
    return;

  auto fn   = vm.find_symbol_by_name("player_hotkey_lame_heal");
  if(fn==nullptr)
    return;

  ScopeVar self(*vm.global_self(), pl.handlePtr());
  vm.call_function<void>(fn);
  }

std::string_view GameScript::spellCastAnim(Npc&, Item &it) {
  if(spellFxAniLetters==nullptr)
    return "FIB";
  return spellFxAniLetters->get_string(uint16_t(it.spellId()));
  }

void GameScript::onWldItemRemoved(const Item& itm) {
  onWldInstanceRemoved(&itm.handle());
  }

void GameScript::makeCurrent(Item* w) {
  if(w==nullptr)
    return;
  auto* s = vm.find_symbol_by_index(uint32_t(w->clsId()));
  if(s != nullptr)
    s->set_instance(w->handlePtr());
  }

Item *GameScript::findItem(zenkit::IItem* handle) {
  if(handle==nullptr)
    return nullptr;
  auto& itData = *handle;
  assert(itData.user_ptr); // engine bug, if null
  return reinterpret_cast<Item*>(itData.user_ptr);
  }

Item *GameScript::findItemById(size_t id) {
  auto* handle = vm.find_symbol_by_index(uint32_t(id));
  if(handle==nullptr||!handle->is_instance_of<zenkit::IItem>())
    return nullptr;
  auto hitm = reinterpret_cast<zenkit::IItem*>(handle->get_instance().get());
  return findItem(hitm);
  }

Item* GameScript::findItemById(const std::shared_ptr<zenkit::DaedalusInstance>& handle) {
  if(handle==nullptr)
    return nullptr;
  if(auto itm = dynamic_cast<const zenkit::IItem*>(handle.get())) {
    assert(itm->user_ptr); // engine bug, if null
    return reinterpret_cast<Item*>(itm->user_ptr);
    }
  return findItemById(handle->symbol_index());
  }

void GameScript::removeItem(Item &it) {
  world().removeItem(it);
  }

void GameScript::setInstanceItem(Npc &holder, size_t itemId) {
  storeItem(holder.getItem(itemId));
  }

uint32_t GameScript::lockPickId() const {
  return ItKE_lockpick!=nullptr ? ItKE_lockpick->index() : 0;
  }

bool GameScript::npc_ownedbynpc(std::shared_ptr<zenkit::IItem> itmRef, std::shared_ptr<zenkit::INpc> npcRef) {
  auto npc = findNpc(npcRef);
  auto itm = findItem(itmRef.get());
  if(itm==nullptr || npc==nullptr) {
    return false;
    }

  auto* sym = vm.find_symbol_by_index(uint32_t(itm->handle().owner));
  return sym != nullptr && npc->handlePtr()==sym->get_instance();
  }

int GameScript::npc_getdisttoitem(std::shared_ptr<zenkit::INpc> npcRef, std::shared_ptr<zenkit::IItem> itmRef) {
  auto itm = findItem(itmRef.get());
  auto npc = findNpc(npcRef);
  if(itm==nullptr || npc==nullptr) {
    return std::numeric_limits<int32_t>::max();
    }
  auto dp = itm->position()-npc->position();
  return int32_t(dp.length());
  }

int GameScript::npc_getheighttoitem(std::shared_ptr<zenkit::INpc> npcRef, std::shared_ptr<zenkit::IItem> itmRef) {
  auto itm = findItem(itmRef.get());
  auto npc = findNpc(npcRef);
  if(itm==nullptr || npc==nullptr) {
    return std::numeric_limits<int32_t>::max();
    }
  auto dp = int32_t(itm->position().y-npc->position().y);
  return std::abs(dp);
  }

bool GameScript::hlp_isitem(std::shared_ptr<zenkit::IItem> itemRef, int instanceSymbol) {
  auto item = findItem(itemRef.get());
  if(item!=nullptr){
    auto& v = item->handle();
    return int(v.symbol_index()) == instanceSymbol;
    } else {
      return false;
    }
  }

bool GameScript::hlp_isvaliditem(std::shared_ptr<zenkit::IItem> itemRef) {
  auto item = findItem(itemRef.get());
  return item!=nullptr;
  }
