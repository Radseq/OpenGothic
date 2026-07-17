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


int GameScript::npc_hasitems(std::shared_ptr<zenkit::INpc> npcRef, int itemId) {
  auto npc = findNpc(npcRef);
  return npc!=nullptr ? int(npc->itemCount(uint32_t(itemId))) : 0;
  }

bool GameScript::npc_hasspell(std::shared_ptr<zenkit::INpc> npcRef, int splId) {
  auto npc = findNpc(npcRef);
  return npc!=nullptr && npc->inventory().hasSpell(splId);
  }

int GameScript::npc_getinvitem(std::shared_ptr<zenkit::INpc> npcRef, int itemId) {
  auto npc = findNpc(npcRef);
  auto itm = npc==nullptr ? nullptr : npc->getItem(uint32_t(itemId));
  storeItem(itm);
  if(itm!=nullptr) {
    return int(itm->handle().symbol_index());
    }
  return -1;
  }

// This seems specific to Gothic 1, where the inventory was grouped into categories.
// In the shared inventory, these categories are just following one after the other.
// So we should transform this to iterate over the different categories in the
// shared inventory
int GameScript::npc_getinvitembyslot(std::shared_ptr<zenkit::INpc> npcRef, int cat, int slotnr) {
  auto npc = findNpc(npcRef);
  if(npc==nullptr) {
    storeItem(nullptr);
    return 0;
    }

  // The category flag names were global, but for the scripts, only npc_getinvitembyslot
  // ever used them, so as long as nobody implements the Gothic 1 inventory, they can
  // be limited to the comments below.
  ItmFlags f = ITM_CAT_NONE;
  switch(cat) {
    case 1: // INV_WEAPON
      f = ItmFlags(ITM_CAT_NF|ITM_CAT_FF|ITM_CAT_MUN);
      break;
    case 2: // INV_ARMOR
      f = ITM_CAT_ARMOR;
      break;
    case 3: // INV_RUNE
      f = ITM_CAT_RUNE;
      break;
    case 4: // INV_MAGIC
      f = ITM_CAT_MAGIC;
      break;
    case 5: // INV_FOOD
      f = ITM_CAT_FOOD;
      break;
    case 6: // INV_POTION
      f = ITM_CAT_POTION;
      break;
    case 7: // INV_DOC
      f = ITM_CAT_DOCS;
      break;
    case 8: // INV_MISC
      f = ItmFlags(ITM_CAT_LIGHT|ITM_CAT_NONE);
      break;
    default:
      Log::e("Unknown item category ", cat);
      storeItem(nullptr);
      return 0;
    }

  auto itm = npc==nullptr ? nullptr : npc->inventory().findByFlags(f, uint32_t(slotnr));
  // Store the found item in the global item var
  storeItem(itm);

  return itm!=nullptr ? int(itm->count()) : 0;
  }

int GameScript::npc_removeinvitem(std::shared_ptr<zenkit::INpc> npcRef, int itemId) {
  auto npc = findNpc(npcRef);
  if(npc!=nullptr)
    npc->delItem(uint32_t(itemId),1);
  return 0;
  }

int GameScript::npc_removeinvitems(std::shared_ptr<zenkit::INpc> npcRef, int itemId, int amount) {
  auto npc = findNpc(npcRef);

  if(npc!=nullptr && amount>0)
    npc->delItem(uint32_t(itemId),uint32_t(amount));

  return 0;
  }

bool GameScript::npc_hasequippedarmor(std::shared_ptr<zenkit::INpc> npcRef) {
  auto npc = findNpc(npcRef);
  return npc!=nullptr && npc->currentArmor()!=nullptr;
  }

std::shared_ptr<zenkit::IItem> GameScript::npc_getequippedmeleeweapon(std::shared_ptr<zenkit::INpc> npcRef) {
  auto npc = findNpc(npcRef);
  if(npc!=nullptr && npc->currentMeleeWeapon() != nullptr) {
    return npc->currentMeleeWeapon()->handlePtr();
    }
  return nullptr;
  }

std::shared_ptr<zenkit::IItem> GameScript::npc_getequippedrangedweapon(std::shared_ptr<zenkit::INpc> npcRef) {
  auto npc = findNpc(npcRef);
  if(npc!=nullptr && npc->currentRangedWeapon() != nullptr) {
    return npc->currentRangedWeapon()->handlePtr();
    }
  return nullptr;
  }

std::shared_ptr<zenkit::IItem> GameScript::npc_getequippedarmor(std::shared_ptr<zenkit::INpc> npcRef) {
  auto npc = findNpc(npcRef);
  if(npc!=nullptr && npc->currentArmor()!=nullptr) {
    return npc->currentArmor()->handlePtr();
    }
  return nullptr;
  }

bool GameScript::npc_hasequippedweapon(std::shared_ptr<zenkit::INpc> npcRef) {
  auto npc = findNpc(npcRef);
  return (npc!=nullptr &&
     (npc->currentMeleeWeapon()!=nullptr ||
      npc->currentRangedWeapon()!=nullptr));
  }

bool GameScript::npc_hasequippedmeleeweapon(std::shared_ptr<zenkit::INpc> npcRef) {
  auto npc = findNpc(npcRef);
  return npc!=nullptr && npc->currentMeleeWeapon()!=nullptr;
  }

bool GameScript::npc_hasequippedrangedweapon(std::shared_ptr<zenkit::INpc> npcRef) {
  auto npc = findNpc(npcRef);
  return npc!=nullptr && npc->currentRangedWeapon()!=nullptr;
  }

int GameScript::npc_getactivespell(std::shared_ptr<zenkit::INpc> npcRef) {
  auto npc = findNpc(npcRef);
  if(npc==nullptr)
    return -1;

  Item* w = npc->activeWeapon();
  if(w==nullptr || !w->isSpellOrRune())
    return -1;

  makeCurrent(w);
  return w->spellId();
  }

bool GameScript::npc_getactivespellisscroll(std::shared_ptr<zenkit::INpc> npcRef) {
  auto npc = findNpc(npcRef);
  if(npc==nullptr)
    return false;

  Item* w = npc->activeWeapon();
  if(w==nullptr || !w->isSpell())
    return false;

  return true;
  }

std::shared_ptr<zenkit::IItem> GameScript::npc_getreadiedweapon(std::shared_ptr<zenkit::INpc> npcRef) {
  auto npc = findNpc(npcRef);
  if(npc==nullptr) {
    return 0;
    }

  auto ret = npc->activeWeapon();
  if(ret!=nullptr) {
    makeCurrent(ret);
    return ret->handlePtr();
    } else {
    return nullptr;
    }
  }

bool GameScript::npc_hasreadiedweapon(std::shared_ptr<zenkit::INpc> npcRef) {
  auto npc = findNpc(npcRef);
  if(npc==nullptr)
    return false;
  auto ws = npc->weaponState();
  return (ws==WeaponState::W1H || ws==WeaponState::W2H ||
          ws==WeaponState::Bow || ws==WeaponState::CBow);
  }

bool GameScript::npc_hasreadiedmeleeweapon(std::shared_ptr<zenkit::INpc> npcRef) {
  auto npc = findNpc(npcRef);
  if(npc==nullptr) {
    return false;
    }
  auto ws = npc->weaponState();
  return ws==WeaponState::W1H || ws==WeaponState::W2H;
  }

bool GameScript::npc_hasreadiedrangedweapon(std::shared_ptr<zenkit::INpc> npcRef) {
  auto npc = findNpc(npcRef);
  if(npc==nullptr)
    return false;
  auto ws = npc->weaponState();
  return ws==WeaponState::Bow || ws==WeaponState::CBow;
  }

bool GameScript::npc_hasrangedweaponwithammo(std::shared_ptr<zenkit::INpc> npcRef) {
  auto npc = findNpc(npcRef);
  return npc!=nullptr && npc->inventory().hasRangedWeaponWithAmmo();
  }

int GameScript::npc_isdrawingspell(std::shared_ptr<zenkit::INpc> npcRef) {
  auto npc = findNpc(npcRef);
  if(npc==nullptr)
    return 0;

  auto ret = npc->activeWeapon();
  if(ret==nullptr || !ret->isSpell())
    return 0;

  makeCurrent(ret);
  return int32_t(ret->clsId());
  }

int GameScript::npc_isdrawingweapon(std::shared_ptr<zenkit::INpc> npcRef) {
  auto npc = findNpc(npcRef);
  if(npc==nullptr)
    return 0;

  auto ret = npc->activeWeapon();
  if(ret==nullptr || !ret->isSpell())
    return 0;

  makeCurrent(ret);
  return int32_t(ret->clsId());
  }

void GameScript::npc_clearinventory(std::shared_ptr<zenkit::INpc> npcRef) {
  auto npc = findNpc(npcRef);
  if(npc!=nullptr)
    npc->clearInventory();
  }

int GameScript::npc_getactivespellcat(std::shared_ptr<zenkit::INpc> npcRef) {
  auto npc = findNpc(npcRef);
  if(npc==nullptr)
    return SPELL_GOOD;

  const Item* w = npc->activeWeapon();
  if(w==nullptr || !w->isSpellOrRune())
    return SPELL_GOOD;

  const int id    = w->spellId();
  auto&     spell = spellDesc(id);
  return spell.spell_type;
  }

int GameScript::npc_setactivespellinfo(std::shared_ptr<zenkit::INpc> npcRef, int v) {
  auto npc = findNpc(npcRef);
  if(npc!=nullptr)
    npc->setActiveSpellInfo(v);
  return 0;
  }

int GameScript::npc_getactivespelllevel(std::shared_ptr<zenkit::INpc> npcRef) {
  int  v   = 0;
  auto npc = findNpc(npcRef);
  if(npc!=nullptr)
    v = npc->activeSpellLevel();
  return v;
  }

int GameScript::mob_hasitems(std::string_view tag, int item) {
  return int(world().hasItems(tag,uint32_t(item)));
  }

void GameScript::equipitem(std::shared_ptr<zenkit::INpc> npcRef, int cls) {
  auto self = findNpc(npcRef);
  if(self!=nullptr) {
    if(self->itemCount(uint32_t(cls))==0)
      self->addItem(uint32_t(cls),1);
    self->useItem(uint32_t(cls),Item::NSLOT,true);
    }
  }

void GameScript::createinvitem(std::shared_ptr<zenkit::INpc> npcRef, int itemInstance) {
  auto self = findNpc(npcRef);
  if(self!=nullptr) {
    Item* itm = self->addItem(uint32_t(itemInstance),1);
    storeItem(itm);
    }
  }

void GameScript::createinvitems(std::shared_ptr<zenkit::INpc> npcRef, int itemInstance, int amount) {
  auto self = findNpc(npcRef);
  if(self!=nullptr && amount>0) {
    Item* itm = self->addItem(uint32_t(itemInstance),size_t(amount));
    storeItem(itm);
    }
  }
