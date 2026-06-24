#include "worldstateexporter.h"

#include <Tempest/Log>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>
#include <string_view>

#include "game/gamesession.h"
#include "game/gamescript.h"
#include "game/inventory.h"
#include "game/questlog.h"
#include "utils/versioninfo.h"
#include "world/objects/interactive.h"
#include "world/objects/item.h"
#include "world/objects/npc.h"
#include "world/waypoint.h"
#include "world/world.h"

namespace {

namespace fs = std::filesystem;

std::string jsonEscape(std::string_view src) {
  std::string ret;
  ret.reserve(src.size() + 8);
  for(char c:src) {
    switch(c) {
      case '\\': ret += "\\\\"; break;
      case '"':  ret += "\\\""; break;
      case '\b': ret += "\\b";  break;
      case '\f': ret += "\\f";  break;
      case '\n': ret += "\\n";  break;
      case '\r': ret += "\\r";  break;
      case '\t': ret += "\\t";  break;
      default:
        if(static_cast<unsigned char>(c)<0x20) {
          char buf[7] = {};
          std::snprintf(buf, sizeof(buf), "\\u%04x", unsigned(c));
          ret += buf;
          } else {
          ret += c;
          }
      }
    }
  return ret;
  }

void writeJsonString(std::ostream& out, std::string_view src) {
  out << '"' << jsonEscape(src) << '"';
  }

void writeVec3(std::ostream& out, const Tempest::Vec3& v) {
  out << '[' << v.x << ',' << v.y << ',' << v.z << ']';
  }

template<class T>
void writeIntArray(std::ostream& out, const T* data, size_t count) {
  out << '[';
  for(size_t i=0; i<count; ++i) {
    if(i!=0)
      out << ',';
    out << int64_t(data[i]);
    }
  out << ']';
  }

std::string targetName(const VersionInfo& v) {
  if(v.game==1)
    return "g1";
  if(v.game==2 && v.patch>=5)
    return "g2notr";
  if(v.game==2)
    return "g2";
  return "unknown";
  }

uint64_t fnv1a(std::string_view src) {
  uint64_t ret = 14695981039346656037ull;
  for(char c:src) {
    ret ^= static_cast<unsigned char>(c);
    ret *= 1099511628211ull;
    }
  return ret;
  }

std::string hex64(uint64_t v) {
  std::ostringstream out;
  out << std::hex << std::setw(16) << std::setfill('0') << v;
  return out.str();
  }

int32_t rounded(float v) {
  return int32_t(v >= 0.f ? v + 0.5f : v - 0.5f);
  }

std::string npcStableKey(const World& world, uint32_t slotId, const Npc& npc) {
  const auto& h  = *npc.handlePtr();
  std::ostringstream src;
  src.imbue(std::locale::classic());
  src << "npc|" << world.name()
      << '|' << npc.persistentId()
      << '|' << h.symbol_index()
      << '|' << h.id;
  return hex64(fnv1a(src.str()));
  }

std::string itemStableKey(const World& world, uint32_t /*slotId*/, const Item& item) {
  std::ostringstream src;
  src.imbue(std::locale::classic());
  src << "item|" << world.name()
      << '|' << item.persistentId()
      << '|' << item.handle().symbol_index()
      << '|' << item.displayName();
  return hex64(fnv1a(src.str()));
  }

std::string mobsiStableKey(const World& world, uint32_t slotId, const Interactive& mobsi) {
  const auto pos = mobsi.position();
  std::ostringstream src;
  src.imbue(std::locale::classic());
  src << "mobsi|" << world.name()
      << '|' << slotId
      << '|' << mobsi.getId()
      << '|' << mobsi.tag()
      << '|' << mobsi.focusName()
      << '|' << mobsi.schemeName()
      << '|' << rounded(pos.x)
      << '|' << rounded(pos.y)
      << '|' << rounded(pos.z);
  return hex64(fnv1a(src.str()));
  }

std::string inventoryStableKey(const World& world, uint32_t /*ownerSlotId*/, const Npc& owner, const Item& item, bool equipped, uint8_t slot) {
  const auto& h = *owner.handlePtr();
  std::ostringstream src;
  src.imbue(std::locale::classic());
  src << "npc_inventory|" << world.name()
      << '|' << owner.persistentId()
      << '|' << h.symbol_index()
      << '|' << h.id
      << '|' << item.persistentId()
      << '|' << item.handle().symbol_index()
      << '|' << uint32_t(equipped ? 1 : 0)
      << '|' << uint32_t(slot);
  return hex64(fnv1a(src.str()));
  }

std::string mobsiInventoryStableKey(const World& world, uint32_t ownerSlotId, const Interactive& owner, const Item& item, bool equipped, uint8_t slot) {
  std::ostringstream src;
  src.imbue(std::locale::classic());
  src << "mobsi_inventory|" << world.name()
      << '|' << ownerSlotId
      << '|' << owner.getId()
      << '|' << item.persistentId()
      << '|' << item.handle().symbol_index()
      << '|' << uint32_t(equipped ? 1 : 0)
      << '|' << uint32_t(slot);
  return hex64(fnv1a(src.str()));
  }

std::string questStableKey(std::string_view name) {
  std::ostringstream src;
  src.imbue(std::locale::classic());
  src << "quest|" << name;
  return hex64(fnv1a(src.str()));
  }

std::string knownDialogStableKey(uint32_t npcSymbol, uint32_t infoSymbol) {
  std::ostringstream src;
  src.imbue(std::locale::classic());
  src << "known_dialog|" << npcSymbol << '|' << infoSymbol;
  return hex64(fnv1a(src.str()));
  }

std::string scriptGlobalStableKey(uint32_t symbolIndex, std::string_view name) {
  std::ostringstream src;
  src.imbue(std::locale::classic());
  src << "script_global|" << symbolIndex << '|' << name;
  return hex64(fnv1a(src.str()));
  }

std::string scriptGlobalTypeName(zenkit::DaedalusDataType type) {
  switch(type) {
    case zenkit::DaedalusDataType::INT:
      return "int";
    case zenkit::DaedalusDataType::FLOAT:
      return "float";
    case zenkit::DaedalusDataType::STRING:
      return "string";
    default:
      return "unknown";
    }
  }

std::string scriptGlobalCategory(std::string_view name) {
  auto startsWith = [](std::string_view s, std::string_view prefix) {
    return s.size()>=prefix.size() && s.substr(0, prefix.size())==prefix;
    };

  if(startsWith(name, "DIA_") || startsWith(name, "INFO_"))
    return "dialog";
  if(startsWith(name, "MIS_") || startsWith(name, "LOG_"))
    return "quest";
  if(name.find("BOOK")!=std::string_view::npos || name.find("BUCH")!=std::string_view::npos ||
     name.find("READ")!=std::string_view::npos || name.find("LEARN")!=std::string_view::npos)
    return "knowledge";
  if(name.find("XP")!=std::string_view::npos || name.find("EXP")!=std::string_view::npos ||
     name.find("BONUS")!=std::string_view::npos)
    return "reward";
  return "script";
  }

std::string_view symbolName(GameScript& script, size_t symbolIndex) {
  auto* sym = script.findSymbol(symbolIndex);
  if(sym==nullptr)
    return {};
  return sym->name();
  }

void writeItemFields(std::ostream& out, const Item& item) {
  const auto& h = item.handle();
  out << "\"persistent_id\":" << item.persistentId()
      << ",\"symbol_index\":" << h.symbol_index()
      << ",\"script_id\":" << h.id
      << ",\"name\":";
  writeJsonString(out, h.name);
  out << ",\"display_name\":";
  writeJsonString(out, item.displayName());
  out << ",\"amount\":" << item.count()
      << ",\"main_flag\":" << h.main_flag
      << ",\"flags\":" << uint32_t(h.flags)
      << ",\"value\":" << h.value
      << ",\"material\":" << uint32_t(h.material)
      << ",\"visual\":";
  writeJsonString(out, h.visual);
  }

bool exportNpcs(GameSession& game, const fs::path& path) {
  auto* world = game.world();
  if(world==nullptr)
    return false;

  std::ofstream out(path, std::ios::binary);
  if(!out)
    return false;
  out.imbue(std::locale::classic());
  out << std::fixed << std::setprecision(3);

  for(uint32_t i=0; i<world->npcCount(); ++i) {
    auto* npc = world->npcById(i);
    if(npc==nullptr)
      continue;

    const auto& h = npc->handle();
    const auto  pos = npc->position();
    out << "{\"type\":\"npc\""
        << ",\"world\":";
    writeJsonString(out, world->name());
    out << ",\"slot_id\":" << i
        << ",\"persistent_id\":" << npc->persistentId()
        << ",\"stable_key\":";
    writeJsonString(out, npcStableKey(*world, i, *npc));
    out << ",\"symbol_index\":" << h.symbol_index()
        << ",\"script_id\":" << h.id
        << ",\"display_name\":";
    writeJsonString(out, npc->displayName());
    out << ",\"pos\":";
    writeVec3(out, pos);
    out << ",\"rotation\":" << npc->rotation()
        << ",\"guild\":" << npc->guild()
        << ",\"true_guild\":" << npc->trueGuild()
        << ",\"hp\":" << npc->attribute(ATR_HITPOINTS)
        << ",\"hp_max\":" << npc->attribute(ATR_HITPOINTSMAX)
        << ",\"mana\":" << npc->attribute(ATR_MANA)
        << ",\"mana_max\":" << npc->attribute(ATR_MANAMAX)
        << ",\"level\":" << npc->level()
        << ",\"dead\":" << (npc->isDead() ? "true" : "false")
        << ",\"player\":" << (npc->isPlayer() ? "true" : "false")
        << ",\"waypoint\":";
    if(auto* wp = npc->currentWayPoint())
      writeJsonString(out, wp->name);
    else
      out << "null";
    out << "}\n";
    }
  return bool(out);
  }

bool exportNpcStats(GameSession& game, const fs::path& path) {
  auto* world = game.world();
  if(world==nullptr)
    return false;

  std::ofstream out(path, std::ios::binary);
  if(!out)
    return false;
  out.imbue(std::locale::classic());

  for(uint32_t i=0; i<world->npcCount(); ++i) {
    auto* npc = world->npcById(i);
    if(npc==nullptr)
      continue;

    const auto& h = npc->handle();
    out << "{\"type\":\"npc_stats\""
        << ",\"world\":";
    writeJsonString(out, world->name());
    out << ",\"owner_slot_id\":" << i
        << ",\"owner_persistent_id\":" << npc->persistentId()
        << ",\"stable_key\":";
    writeJsonString(out, npcStableKey(*world, i, *npc));
    out << ",\"owner_symbol_index\":" << h.symbol_index()
        << ",\"owner_display_name\":";
    writeJsonString(out, npc->displayName());
    out << ",\"player\":" << (npc->isPlayer() ? "true" : "false")
        << ",\"level\":" << npc->level()
        << ",\"experience\":" << npc->experience()
        << ",\"experience_next\":" << npc->experienceNext()
        << ",\"learning_points\":" << npc->learningPoints()
        << ",\"attributes\":";
    writeIntArray(out, h.attribute, ATR_MAX);
    out << ",\"protection\":";
    writeIntArray(out, h.protection, PROT_MAX);
    out << ",\"damage\":";
    writeIntArray(out, h.damage, PROT_MAX);
    out << ",\"hit_chance\":";
    writeIntArray(out, h.hitchance, zenkit::INpc::hitchance_count);
    out << ",\"talent_skill\":[";
    for(uint32_t j=0; j<TALENT_MAX_G2; ++j) {
      if(j!=0)
        out << ',';
      out << npc->talentSkill(Talent(j));
      }
    out << "],\"talent_value\":[";
    for(uint32_t j=0; j<TALENT_MAX_G2; ++j) {
      if(j!=0)
        out << ',';
      out << npc->talentValue(Talent(j));
      }
    out << "],\"mission\":";
    writeIntArray(out, h.mission, zenkit::INpc::mission_count);
    out << ",\"aivar\":";
    writeIntArray(out, h.aivar, zenkit::INpc::aivar_count);
    out << "}\n";
    }
  return bool(out);
  }

bool exportItems(GameSession& game, const fs::path& path) {
  auto* world = game.world();
  if(world==nullptr)
    return false;

  std::ofstream out(path, std::ios::binary);
  if(!out)
    return false;
  out.imbue(std::locale::classic());
  out << std::fixed << std::setprecision(3);

  for(uint32_t i=0;; ++i) {
    auto* item = world->itmById(i);
    if(item==nullptr)
      break;

    out << "{\"type\":\"item\""
        << ",\"world\":";
    writeJsonString(out, world->name());
    out << ",\"slot_id\":" << i
        << ",\"stable_key\":";
    writeJsonString(out, itemStableKey(*world, i, *item));
    out << ',';
    writeItemFields(out, *item);
    out << ",\"pos\":";
    writeVec3(out, item->position());
    out << "}\n";
    }
  return bool(out);
  }

bool exportNpcInventory(GameSession& game, const fs::path& path) {
  auto* world = game.world();
  if(world==nullptr)
    return false;

  std::ofstream out(path, std::ios::binary);
  if(!out)
    return false;
  out.imbue(std::locale::classic());
  out << std::fixed << std::setprecision(3);

  for(uint32_t i=0; i<world->npcCount(); ++i) {
    auto* npc = world->npcById(i);
    if(npc==nullptr)
      continue;

    auto it = npc->inventory().iterator(Inventory::T_Inventory);
    for(; it.isValid(); ++it) {
      const Item& item = *it;
      out << "{\"type\":\"npc_inventory\""
          << ",\"world\":";
      writeJsonString(out, world->name());
      out << ",\"owner_slot_id\":" << i
          << ",\"owner_persistent_id\":" << npc->persistentId()
          << ",\"stable_key\":";
      writeJsonString(out, inventoryStableKey(*world, i, *npc, item, it.isEquipped(), it.slot()));
      out << ",\"owner_stable_key\":";
      writeJsonString(out, npcStableKey(*world, i, *npc));
      out << ",\"owner_symbol_index\":" << npc->handle().symbol_index()
          << ",\"owner_display_name\":";
      writeJsonString(out, npc->displayName());
      out << ',';
      writeItemFields(out, item);
      out << ",\"iterator_count\":" << it.count()
          << ",\"equipped\":" << (it.isEquipped() ? "true" : "false")
          << ",\"slot\":" << uint32_t(it.slot())
          << "}\n";
      }
    }
  return bool(out);
  }

bool exportMobsi(GameSession& game, const fs::path& path) {
  auto* world = game.world();
  if(world==nullptr)
    return false;

  std::ofstream out(path, std::ios::binary);
  if(!out)
    return false;
  out.imbue(std::locale::classic());
  out << std::fixed << std::setprecision(3);

  for(uint32_t i=0;; ++i) {
    auto* mobsi = world->mobsiById(i);
    if(mobsi==nullptr)
      break;

    out << "{\"type\":\"mobsi\""
        << ",\"world\":";
    writeJsonString(out, world->name());
    out << ",\"slot_id\":" << i
        << ",\"stable_key\":";
    writeJsonString(out, mobsiStableKey(*world, i, *mobsi));
    out << ",\"vob_id\":" << mobsi->getId()
        << ",\"tag\":";
    writeJsonString(out, mobsi->tag());
    out << ",\"focus_name\":";
    writeJsonString(out, mobsi->focusName());
    out << ",\"display_name\":";
    writeJsonString(out, mobsi->displayName());
    out << ",\"owner\":";
    writeJsonString(out, mobsi->ownerName());
    out << ",\"scheme\":";
    writeJsonString(out, mobsi->schemeName());
    out << ",\"pos_scheme\":";
    writeJsonString(out, mobsi->posSchemeName());
    out << ",\"pos\":";
    writeVec3(out, mobsi->position());
    out << ",\"display_pos\":";
    writeVec3(out, mobsi->displayPosition());
    out << ",\"state\":" << mobsi->stateId()
        << ",\"state_count\":" << mobsi->stateCount()
        << ",\"state_mask\":" << mobsi->stateMask()
        << ",\"container\":" << (mobsi->isContainer() ? "true" : "false")
        << ",\"door\":" << (mobsi->isDoor() ? "true" : "false")
        << ",\"ladder\":" << (mobsi->isLadder() ? "true" : "false")
        << ",\"locked\":" << (mobsi->isLocked() ? "true" : "false")
        << ",\"cracked\":" << (mobsi->isCracked() ? "true" : "false")
        << ",\"key_instance\":";
    writeJsonString(out, mobsi->keyInstanceName());
    out << ",\"pick_lock_code\":";
    writeJsonString(out, mobsi->pickLockCode());
    out << "}\n";
    }
  return bool(out);
  }

bool exportMobsiInventory(GameSession& game, const fs::path& path) {
  auto* world = game.world();
  if(world==nullptr)
    return false;

  std::ofstream out(path, std::ios::binary);
  if(!out)
    return false;
  out.imbue(std::locale::classic());
  out << std::fixed << std::setprecision(3);

  for(uint32_t i=0;; ++i) {
    auto* mobsi = world->mobsiById(i);
    if(mobsi==nullptr)
      break;

    auto it = mobsi->inventory().iterator(Inventory::T_Inventory);
    for(; it.isValid(); ++it) {
      const Item& item = *it;
      out << "{\"type\":\"mobsi_inventory\""
          << ",\"world\":";
      writeJsonString(out, world->name());
      out << ",\"owner_slot_id\":" << i
          << ",\"stable_key\":";
      writeJsonString(out, mobsiInventoryStableKey(*world, i, *mobsi, item, it.isEquipped(), it.slot()));
      out << ",\"owner_stable_key\":";
      writeJsonString(out, mobsiStableKey(*world, i, *mobsi));
      out << ",\"owner_vob_id\":" << mobsi->getId()
          << ",\"owner_tag\":";
      writeJsonString(out, mobsi->tag());
      out << ",\"owner_focus_name\":";
      writeJsonString(out, mobsi->focusName());
      out << ',';
      writeItemFields(out, item);
      out << ",\"iterator_count\":" << it.count()
          << ",\"equipped\":" << (it.isEquipped() ? "true" : "false")
          << ",\"slot\":" << uint32_t(it.slot())
          << "}\n";
      }
    }
  return bool(out);
  }

bool exportQuests(GameSession& game, const fs::path& path) {
  auto* script = game.script();
  if(script==nullptr)
    return false;

  const auto& quests = script->questLog();
  std::ofstream out(path, std::ios::binary);
  if(!out)
    return false;
  out.imbue(std::locale::classic());

  for(size_t i=0; i<quests.questCount(); ++i) {
    const auto& quest = quests.quest(i);
    out << "{\"type\":\"quest\""
        << ",\"stable_key\":";
    writeJsonString(out, questStableKey(quest.name));
    out << ",\"name\":";
    writeJsonString(out, quest.name);
    out << ",\"section\":" << uint32_t(quest.section)
        << ",\"status\":" << uint32_t(quest.status)
        << ",\"entry_count\":" << quest.entry.size()
        << ",\"entries\":[";
    for(size_t j=0; j<quest.entry.size(); ++j) {
      if(j!=0)
        out << ',';
      writeJsonString(out, quest.entry[j]);
      }
    out << "]}\n";
    }
  return bool(out);
  }

bool exportKnownDialogs(GameSession& game, const fs::path& path) {
  auto* script = game.script();
  if(script==nullptr)
    return false;

  std::ofstream out(path, std::ios::binary);
  if(!out)
    return false;
  out.imbue(std::locale::classic());

  for(auto& i:script->knownDialogInfos()) {
    const auto npcSymbol  = uint32_t(i.first);
    const auto infoSymbol = uint32_t(i.second);
    out << "{\"type\":\"known_dialog\""
        << ",\"stable_key\":";
    writeJsonString(out, knownDialogStableKey(npcSymbol, infoSymbol));
    out << ",\"npc_symbol_index\":" << npcSymbol
        << ",\"npc_symbol_name\":";
    writeJsonString(out, symbolName(*script, npcSymbol));
    out << ",\"info_symbol_index\":" << infoSymbol
        << ",\"info_symbol_name\":";
    writeJsonString(out, symbolName(*script, infoSymbol));
    out << "}\n";
    }
  return bool(out);
  }

bool exportScriptGlobals(GameSession& game, const fs::path& path) {
  auto* script = game.script();
  if(script==nullptr)
    return false;

  std::ofstream out(path, std::ios::binary);
  if(!out)
    return false;
  out.imbue(std::locale::classic());

  for(uint32_t i=0; i<script->symbolsCount(); ++i) {
    auto* sym = script->findSymbol(i);
    if(sym==nullptr || sym->is_member() || sym->is_const() || sym->count()==0)
      continue;

    const auto type = sym->type();
    if(type!=zenkit::DaedalusDataType::INT &&
       type!=zenkit::DaedalusDataType::FLOAT &&
       type!=zenkit::DaedalusDataType::STRING)
      continue;

    out << "{\"type\":\"script_global\""
        << ",\"stable_key\":";
    writeJsonString(out, scriptGlobalStableKey(i, sym->name()));
    out << ",\"symbol_index\":" << i
        << ",\"symbol_name\":";
    writeJsonString(out, sym->name());
    out << ",\"value_type\":";
    writeJsonString(out, scriptGlobalTypeName(type));
    out << ",\"category\":";
    writeJsonString(out, scriptGlobalCategory(sym->name()));
    out << ",\"value_count\":" << sym->count()
        << ",\"values\":[";

    for(uint32_t j=0; j<sym->count(); ++j) {
      if(j!=0)
        out << ',';
      switch(type) {
        case zenkit::DaedalusDataType::INT:
          out << sym->get_int(uint16_t(j));
          break;
        case zenkit::DaedalusDataType::FLOAT:
          out << sym->get_float(uint16_t(j));
          break;
        case zenkit::DaedalusDataType::STRING:
          writeJsonString(out, sym->get_string(uint16_t(j)));
          break;
        default:
          out << "null";
          break;
        }
      }
    out << "]}\n";
    }
  return bool(out);
  }

uint32_t itemCount(World& world) {
  uint32_t ret = 0;
  while(world.itmById(ret)!=nullptr)
    ++ret;
  return ret;
  }

uint32_t mobsiCount(World& world) {
  uint32_t ret = 0;
  while(world.mobsiById(ret)!=nullptr)
    ++ret;
  return ret;
  }

uint32_t inventoryRowCount(World& world) {
  uint32_t ret = 0;
  for(uint32_t i=0; i<world.npcCount(); ++i) {
    auto* npc = world.npcById(i);
    if(npc==nullptr)
      continue;
    auto it = npc->inventory().iterator(Inventory::T_Inventory);
    for(; it.isValid(); ++it)
      ++ret;
    }
  return ret;
  }

uint32_t mobsiInventoryRowCount(World& world) {
  uint32_t ret = 0;
  for(uint32_t i=0;; ++i) {
    auto* mobsi = world.mobsiById(i);
    if(mobsi==nullptr)
      break;
    auto it = mobsi->inventory().iterator(Inventory::T_Inventory);
    for(; it.isValid(); ++it)
      ++ret;
    }
  return ret;
  }

uint32_t questCount(GameSession& game) {
  auto* script = game.script();
  if(script==nullptr)
    return 0;
  return uint32_t(script->questLog().questCount());
  }

uint32_t knownDialogCount(GameSession& game) {
  auto* script = game.script();
  if(script==nullptr)
    return 0;
  return uint32_t(script->knownDialogInfos().size());
  }

uint32_t scriptGlobalCount(GameSession& game) {
  auto* script = game.script();
  if(script==nullptr)
    return 0;

  uint32_t ret = 0;
  for(uint32_t i=0; i<script->symbolsCount(); ++i) {
    auto* sym = script->findSymbol(i);
    if(sym==nullptr || sym->is_member() || sym->is_const() || sym->count()==0)
      continue;
    const auto type = sym->type();
    if(type==zenkit::DaedalusDataType::INT ||
       type==zenkit::DaedalusDataType::FLOAT ||
       type==zenkit::DaedalusDataType::STRING)
      ++ret;
    }
  return ret;
  }

bool exportManifest(GameSession& game, const fs::path& path, std::string_view kind) {
  auto* world = game.world();
  if(world==nullptr)
    return false;

  std::ofstream out(path, std::ios::binary);
  if(!out)
    return false;

  out.imbue(std::locale::classic());
  out << "{\n"
      << "  \"schema\": 6,\n"
      << "  \"kind\": ";
  writeJsonString(out, kind);
  out << ",\n"
      << "  \"target\": ";
  writeJsonString(out, targetName(game.version()));
  out << ",\n"
      << "  \"world\": ";
  writeJsonString(out, world->name());
  out << ",\n"
      << "  \"game\": " << int(game.version().game) << ",\n"
      << "  \"patch\": " << game.version().patch << ",\n"
      << "  \"tick_count\": " << game.tickCount() << ",\n"
      << "  \"time_day_millis\": " << game.time().timeInDay().toInt() << ",\n"
      << "  \"npc_count\": " << world->npcCount() << ",\n"
      << "  \"npc_stats_rows\": " << world->npcCount() << ",\n"
      << "  \"item_count\": " << itemCount(*world) << ",\n"
      << "  \"npc_inventory_rows\": " << inventoryRowCount(*world) << ",\n"
      << "  \"mobsi_count\": " << mobsiCount(*world) << ",\n"
      << "  \"mobsi_inventory_rows\": " << mobsiInventoryRowCount(*world) << ",\n"
      << "  \"quest_count\": " << questCount(game) << ",\n"
      << "  \"known_dialog_count\": " << knownDialogCount(game) << ",\n"
      << "  \"script_global_count\": " << scriptGlobalCount(game) << "\n"
      << "}\n";

  return bool(out);
  }

bool exportState(GameSession& game, const fs::path& dir, std::string_view kind) {
  auto* world = game.world();
  if(world==nullptr)
    return false;

  std::error_code ec;
  fs::create_directories(dir, ec);
  if(ec) {
    Tempest::Log::e("unable to create world export directory: ", dir.string(), ", reason: ", ec.message());
    return false;
    }

  const bool ok = exportManifest(game, dir / "manifest.json", kind) &&
                  exportNpcs(game, dir / "npcs.jsonl") &&
                  exportNpcStats(game, dir / "npc_stats.jsonl") &&
                  exportItems(game, dir / "items.jsonl") &&
                  exportNpcInventory(game, dir / "npc_inventory.jsonl") &&
                  exportMobsi(game, dir / "mobsi.jsonl") &&
                  exportMobsiInventory(game, dir / "mobsi_inventory.jsonl") &&
                  exportQuests(game, dir / "quests.jsonl") &&
                  exportKnownDialogs(game, dir / "known_dialogs.jsonl") &&
                  exportScriptGlobals(game, dir / "script_globals.jsonl");

  if(ok)
    Tempest::Log::i("world export written to: ", dir.string());
  else
    Tempest::Log::e("world export failed at: ", dir.string());

  return ok;
  }

}

bool WorldStateExporter::exportInitialState(GameSession& game, std::string_view directory) {
  auto* world = game.world();
  if(world==nullptr || directory.empty())
    return false;

  fs::path dir = fs::path(directory) / targetName(game.version()) / std::string(world->name());
  return exportState(game, dir, "initial_world_export");
  }

bool WorldStateExporter::exportSaveState(GameSession& game, std::string_view directory) {
  auto* world = game.world();
  if(world==nullptr || directory.empty())
    return false;

  char tick[64] = {};
  std::snprintf(tick, sizeof(tick), "tick_%llu", static_cast<unsigned long long>(game.tickCount()));

  fs::path dir = fs::path(directory) / targetName(game.version()) / std::string(world->name()) / "snapshots" / tick;
  return exportState(game, dir, "save_world_export");
  }
