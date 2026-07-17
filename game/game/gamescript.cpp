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


GameScript::PerDist::PerDist() {
  for(auto& i:range)
    i = -1;
  }

int GameScript::PerDist::at(PercType perc, int r) const {
  if(perc>=PERC_Count)
    return r;
  auto rr = range[perc];
  if(rr>0)
    return rr;
  return r;
  }


GameScript::GameScript(GameSession &owner)
    :owner(owner), vm(createVm(Gothic::inst())) {
  if (vm.global_self() == nullptr || vm.global_other() == nullptr || vm.global_item() == nullptr ||
      vm.global_victim() == nullptr || vm.global_hero() == nullptr)
    throw std::runtime_error("Cannot find script symbol SELF, OTHER, ITEM, VICTIM, or HERO! Cannot proceed!");

  vmLang = Gothic::inst().settingsGetI("GAME", "language");
  vm.register_exception_handler(zenkit::lenient_vm_exception_handler);
  Gothic::inst().setupCommonScriptClasses(vm);
  Gothic::inst().setupVmCommonApi(vm);
  aiDefaultPipe.reset(new GlobalOutput(*this));
  initCommon();
  initSettings();
  Gothic::inst().onSettingsChanged.bind(this,&GameScript::initSettings);
  }

GameScript::~GameScript() {
  Gothic::inst().onSettingsChanged.ubind(this,&GameScript::initSettings);
  }

void GameScript::initSettings() {
  auto lang = Gothic::inst().settingsGetI("GAME", "language");
  if(vmLang!=lang) {
    vmLang = lang;
    //vm     = createVm(Gothic::inst());
    initDialogs();
    }
  }

void GameScript::saveVar(Serialize &fout) {
  auto& dat = vm.symbols();
  fout.write(uint32_t(dat.size()));
  for(unsigned i = 0; i < dat.size(); ++i){
    auto* sym = vm.find_symbol_by_index(i); // never returns nullptr
    saveSym(fout,*sym);
    }
  }

void GameScript::loadVar(Serialize &fin) {
  std::string name;
  uint32_t sz=0;
  fin.read(sz);
  for(size_t i=0;i<sz;++i){
    auto t = uint32_t(zenkit::DaedalusDataType::VOID);
    fin.read(t);
    switch(zenkit::DaedalusDataType(t)) {
      case zenkit::DaedalusDataType::INT:{
        fin.read(name);
        auto* s = findSymbol(name);

        uint32_t size;
        fin.read(size);

        int v = 0;
        for(unsigned j = 0; j < size; ++j) {
          fin.read(v);
          if (s != nullptr && !s->is_member() && !s->is_const()) {
            s->set_int(v, uint16_t(j));
            }
          }

        break;
        }
      case zenkit::DaedalusDataType::FLOAT:{
        fin.read(name);
        auto* s = findSymbol(name);

        uint32_t size;
        fin.read(size);

        float v = 0;
        for (unsigned j = 0; j < size; ++j) {
          fin.read(v);
          if (s != nullptr && !s->is_member() && !s->is_const()) {
            s->set_float(v, uint16_t(j));
            }
          }

        break;
        }
      case zenkit::DaedalusDataType::STRING:{
        fin.read(name);
        auto* s = findSymbol(name);

        uint32_t size;
        fin.read(size);

        std::string v;
        for (unsigned j = 0; j < size; ++j) {
          fin.read(v);
          if (s != nullptr && !s->is_member() && !s->is_const()) {
            s->set_string(v, uint16_t(j));
            }
          }

        break;
        }
      case zenkit::DaedalusDataType::INSTANCE:{
        uint8_t dataClass=0;
        fin.read(dataClass);
        if(dataClass>0){
          uint32_t id=0;
          fin.read(name,id);
          auto* s = findSymbol(name);
          if (s == nullptr)
            break;
          if(dataClass==1) {
            auto npc = world().npcById(id);
            s->set_instance(npc ? npc->handlePtr() : nullptr);
            }
          else if(dataClass==2) {
            auto itm = world().itmById(id);
            s->set_instance(itm != nullptr ? itm->handlePtr() : nullptr);
            }
          else if(dataClass==3) {
            uint32_t itmClass=0;
            fin.read(itmClass);
            if(auto npc = world().npcById(id)) {
              auto itm = npc->getItem(itmClass);
              s->set_instance(itm ? itm->handlePtr() : nullptr);
              }
            }
          }
        break;
        }
      default:
        break;
      }
    }
  }

void GameScript::savePerc(Serialize& fout) {
  fout.write(uint32_t(PERC_Count));
  for(size_t i=0; i<PERC_Count; ++i) {
    fout.write(perceptionRanges.range[i]);
    }
  }

void GameScript::loadPerc(Serialize& fin) {
  uint32_t count = 0;
  fin.read(count);
  if(count!=PERC_Count) {
    if(hasSymbolName("initPerceptions"))
      vm.call_function("initPerceptions");
    return;
    }
  for(size_t i=0; i<PERC_Count; ++i)
    fin.read(perceptionRanges.range[i]);
  }

void GameScript::resetVarPointers() {
  for(uint32_t i=0;i<vm.symbols().size();++i){
    auto* s = vm.find_symbol_by_index(i); // never returns nullptr
    if(s->is_instance_of<zenkit::INpc>() || s->is_instance_of<zenkit::IItem>()){
      s->set_instance(nullptr);
      }
    }
  }

bool GameScript::restoreGlobalIntForPersistence(size_t symbolIndex, uint16_t valueIndex, int32_t value) {
  auto* symbol = findSymbol(symbolIndex);
  if(symbol==nullptr || symbol->is_member() || symbol->is_const() ||
     symbol->type()!=zenkit::DaedalusDataType::INT || valueIndex>=symbol->count())
    return false;
  symbol->set_int(value, valueIndex);
  return true;
  }

bool GameScript::restoreGlobalFloatForPersistence(size_t symbolIndex, uint16_t valueIndex, float value) {
  auto* symbol = findSymbol(symbolIndex);
  if(symbol==nullptr || symbol->is_member() || symbol->is_const() ||
     symbol->type()!=zenkit::DaedalusDataType::FLOAT || valueIndex>=symbol->count())
    return false;
  symbol->set_float(value, valueIndex);
  return true;
  }

bool GameScript::restoreGlobalStringForPersistence(size_t symbolIndex, uint16_t valueIndex, std::string_view value) {
  auto* symbol = findSymbol(symbolIndex);
  if(symbol==nullptr || symbol->is_member() || symbol->is_const() ||
     symbol->type()!=zenkit::DaedalusDataType::STRING || valueIndex>=symbol->count())
    return false;
  symbol->set_string(value, valueIndex);
  return true;
  }

size_t GameScript::guildCountForPersistence() const {
  return gilCount;
  }

int32_t GameScript::guildAttitudeForPersistence(size_t fromGuild, size_t toGuild) const {
  if(fromGuild>=gilCount || toGuild>=gilCount)
    return ATT_HOSTILE;
  return gilAttitudes[fromGuild*gilCount + toGuild];
  }

bool GameScript::restoreGuildAttitudeForPersistence(size_t fromGuild, size_t toGuild, int32_t value) {
  if(fromGuild>=gilCount || toGuild>=gilCount)
    return false;
  gilAttitudes[fromGuild*gilCount + toGuild] = value;
  return true;
  }

void GameScript::saveSym(Serialize &fout, zenkit::DaedalusSymbol& i) {
  auto& w = world();
  switch(i.type()) {
    case zenkit::DaedalusDataType::INT:
      if(i.count()>0 && !i.is_member() && !i.is_const()){
        fout.write(i.type(), i.name(), i.count());

        for (unsigned j = 0; j < i.count(); ++j)
          fout.write(i.get_int(uint16_t(j)));
        return;
        }
      break;
    case zenkit::DaedalusDataType::FLOAT:
      if(i.count()>0 && !i.is_member() && !i.is_const()){
        fout.write(i.type(), i.name(), i.count());

        for (unsigned j = 0; j < i.count(); ++j)
          fout.write(i.get_float(uint16_t(j)));
        return;
        }
      break;
    case zenkit::DaedalusDataType::STRING:
      if(i.count()>0 && !i.is_member() && !i.is_const()){
        fout.write(i.type(), i.name(), i.count());

        for (unsigned j = 0; j < i.count(); ++j)
          fout.write(i.get_string(uint16_t(j)));
        return;
        }
      break;
    case zenkit::DaedalusDataType::INSTANCE:
      fout.write(i.type());

      if(i.is_instance_of<zenkit::INpc>()){
        auto hnpc = reinterpret_cast<const zenkit::INpc*>(i.get_instance().get());
        auto npc  = reinterpret_cast<const Npc*>(hnpc==nullptr ? nullptr : hnpc->user_ptr);
        fout.write(uint8_t(1),i.name(),world().npcId(npc));
        }
      else if(i.is_instance_of<zenkit::IItem>()){
        auto     item = reinterpret_cast<const zenkit::IItem*>(i.get_instance().get());
        uint32_t id   = w.itmId(item);
        if(id!=uint32_t(-1) || item==nullptr) {
          fout.write(uint8_t(2),i.name(),id);
          } else {
          uint32_t idNpc = uint32_t(-1);
          for(uint32_t r=0; r<w.npcCount(); ++r) {
            auto& n = *w.npcById(r);
            if(n.itemCount(item->symbol_index())>0) {
              idNpc = r;
              fout.write(uint8_t(3),i.name(),idNpc,uint32_t(item->symbol_index()));
              break;
              }
            }
          if(idNpc==uint32_t(-1))
            fout.write(uint8_t(2),i.name(),uint32_t(-1));
          }
        }
      else if(i.is_instance_of<zenkit::IFocus>() ||
              i.is_instance_of<zenkit::IGuildValues>() ||
              i.is_instance_of<zenkit::IInfo>()) {
        fout.write(uint8_t(0));
        }
      else {
        fout.write(uint8_t(0));
        }
      return;
    default:
      break;
    }
  fout.write(uint32_t(zenkit::DaedalusDataType::VOID));
  }

zenkit::IFocus GameScript::findFocus(std::string_view name) {
  auto id = vm.find_symbol_by_name(name);
  if(id==nullptr)
    return {};
  try {
    return *vm.init_instance<zenkit::IFocus>(id);
    }
  catch(const zenkit::DaedalusScriptError&) {
    return {};
    }
  }

zenkit::DaedalusSymbol* GameScript::findSymbol(std::string_view s) {
  return vm.find_symbol_by_name(s);
  }

zenkit::DaedalusSymbol* GameScript::findSymbol(const size_t s) {
  return vm.find_symbol_by_index(uint32_t(s));
  }

size_t GameScript::findSymbolIndex(std::string_view name) {
  auto sym = vm.find_symbol_by_name(name);
  return sym == nullptr ? size_t(-1) : sym->index();
  }

size_t GameScript::symbolsCount() const {
  return vm.symbols().size();
  }

zenkit::DaedalusVm GameScript::createVm(Gothic& gothic) {
  auto lang   = gothic.settingsGetI("GAME", "language");
  auto script = gothic.loadScript(gothic.defaultGameDatFile(), ScriptLang(lang));
  auto exef   = zenkit::DaedalusVmExecutionFlag::ALLOW_NULL_INSTANCE_ACCESS;
  if(DirectMemory::isRequired(script)) {
    exef |= zenkit::DaedalusVmExecutionFlag::IGNORE_CONST_SPECIFIER;
    }
  return zenkit::DaedalusVm(std::move(script), exef);
  }

bool GameScript::hasSymbolName(std::string_view name) {
  return vm.find_symbol_by_name(name)!=nullptr;
  }

uint32_t GameScript::rand(uint32_t max) {
  return uint32_t(randGen())%max;
  }

std::string_view GameScript::menuMain() const {
  if(dma!=nullptr)
    return dma->menuMain();
  return ::menuMain;
  }

bool GameScript::game_initgerman() {
  return true;
  }

bool GameScript::game_initenglish() {
  return true;
  }

int GameScript::hlp_getinstanceid(std::shared_ptr<zenkit::DaedalusInstance> instance) {
  // Log::d("hlp_getinstanceid: name \"",handle.name,"\" not found");
  return instance == nullptr ? -1 : int(instance->symbol_index());
  }

int GameScript::hlp_random(int bound) {
  uint32_t mod = uint32_t(std::max(1,bound));
  return int32_t(randGen() % mod);
  }

void GameScript::exitsession() {
  owner.exitSession();
  }
