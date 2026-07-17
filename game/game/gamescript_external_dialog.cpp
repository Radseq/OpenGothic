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


bool GameScript::GlobalOutput::output(Npc& npc, std::string_view text) {
  return owner.aiOutput(npc,text,false);
  }

bool GameScript::GlobalOutput::outputSvm(Npc &npc, std::string_view text) {
  return owner.aiOutputSvm(npc,text,false);
  }

bool GameScript::GlobalOutput::outputOv(Npc &npc, std::string_view text) {
  return owner.aiOutputSvm(npc,text,true);
  }

bool GameScript::GlobalOutput::printScr(Npc& npc, int time, std::string_view msg, int x, int y, std::string_view font) {
  auto& f = Resources::font(font, Resources::FontType::Normal, 1.0);
  Gothic::inst().onPrintScreen(msg,x,y,time,f);
  return true;
  }

bool GameScript::GlobalOutput::isFinished() {
  return true;
  }

void GameScript::initDialogs() {
  loadDialogOU();

  dialogsInfo.clear();
  vm.enumerate_instances_by_class_name("C_INFO", [this](zenkit::DaedalusSymbol& sym){
    dialogsInfo.push_back(vm.init_instance<zenkit::IInfo>(&sym));
    });
  }

void GameScript::loadDialogOU() {
  // Based on https://github.com/auronen/Gothic-2-localization mod,
  // at least in G2 .BIN should have a priority
  const std::string prefix = std::string(Gothic::inst().defaultOutputUnits());
  const std::array<std::string,2> names = {prefix + ".BIN", prefix + ".DAT"};

  const auto version = Gothic::inst().version().game==1 ? zenkit::GameVersion::GOTHIC_1
                                                        : zenkit::GameVersion::GOTHIC_2;

  for(auto& OU:names) {
    if(Resources::hasFile(OU)) {
      std::unique_ptr<zenkit::Read> read;
      auto zen = Resources::openReader(OU, read);
      dialogs = std::move(*zen->read_object<zenkit::CutsceneLibrary>(version));
      return;
      }

    const size_t segment = OU.find_last_of("\\/");
    if(segment!=std::string::npos && Resources::hasFile(OU.substr(segment+1))) {
      std::unique_ptr<zenkit::Read> read;
      auto zen = Resources::openReader(OU.substr(segment+1), read);
      dialogs = std::move(*zen->read_object<zenkit::CutsceneLibrary>(version));
      return;
      }

    char16_t str16[256] = {};
    for(size_t i=0; OU[i] && i<255; ++i)
      str16[i] = char16_t(OU[i]);

    auto gcutscene = CommandLine::inst().cutscenePath();
    auto full      = FileUtil::caseInsensitiveSegment(gcutscene,str16,Dir::FT_File);
    if(!FileUtil::exists(std::u16string(full)) && vmLang>=0) {
      gcutscene = CommandLine::inst().cutscenePath(ScriptLang(vmLang));
      full      = FileUtil::caseInsensitiveSegment(gcutscene,str16,Dir::FT_File);
      }

    try {
      auto buf = zenkit::Read::from(full);
      if(buf==nullptr)
        continue;
      auto zen = zenkit::ReadArchive::from(buf.get());
      if(zen==nullptr)
        continue;
      // auto zen = Resources::openReader(full);
      dialogs = std::move(*zen->read_object<zenkit::CutsceneLibrary>(version));
      return;
      }
    catch(...){
      // loop to next possible path
      }
    }
  Log::e("none of Zen-files for OU could be loaded");
  }

std::vector<GameScript::DlgChoice> GameScript::dialogChoices(std::shared_ptr<zenkit::INpc> player,
                                                             std::shared_ptr<zenkit::INpc> hnpc,
                                                             const std::vector<uint32_t>& except,
                                                             bool includeImp) {
  ScopeVar self (*vm.global_self(),  hnpc);
  ScopeVar other(*vm.global_other(), player);
  std::vector<zenkit::IInfo*> hDialog;
  for(auto& info : dialogsInfo) {
    if(info->npc==static_cast<int>(hnpc->symbol_index())) {
      hDialog.push_back(info.get());
      }
    }

  std::vector<DlgChoice> choice;
  for(int important=includeImp ? 1 : 0;important>=0;--important){
    for(auto& i:hDialog) {
      const zenkit::IInfo& info = *i;
      if(info.important!=important)
        continue;
      bool npcKnowsInfo = doesNpcKnowInfo(*player,vm.find_symbol_by_instance(info)->index());
      if(npcKnowsInfo && !info.permanent)
        continue;

      if(info.important && info.permanent){
        bool skip=false;
        for(auto i:except)
          if(static_cast<int>(i)==info.information){
            skip=true;
            break;
            }
        if(skip)
          continue;
        }

      bool valid=true;
      if(info.condition) {
        auto* conditionSymbol = vm.find_symbol_by_index(uint32_t(info.condition));
        if (conditionSymbol != nullptr) {
          valid = vm.call_function<int>(conditionSymbol) != 0;
          }
        }
      if(!valid)
        continue;

      DlgChoice ch;
      ch.title    = info.description;
      ch.scriptFn = uint32_t(info.information);
      ch.handle   = i;
      ch.isTrade  = info.trade!=0;
      ch.sort     = info.nr;
      choice.emplace_back(std::move(ch));
      }
    if(!choice.empty()){
      sort(choice);
      return choice;
      }
    }
  sort(choice);
  return choice;
  }

std::vector<GameScript::DlgChoice> GameScript::updateDialog(const GameScript::DlgChoice &dlg, Npc& player,Npc& npc) {
  if(dlg.handle==nullptr)
    return {};
  const zenkit::IInfo&               info = *dlg.handle;
  std::vector<GameScript::DlgChoice> ret;

  ScopeVar self (*vm.global_self(), npc.handlePtr());
  ScopeVar other(*vm.global_other(), player.handlePtr());

  for(size_t i=0;i<info.choices.size();++i){
    auto& sub = info.choices[i];
    GameScript::DlgChoice ch;
    ch.title    = sub.text;
    ch.scriptFn = uint32_t(sub.function);
    ch.handle   = dlg.handle;
    ch.isTrade  = false;
    ch.sort     = int(i);
    ret.push_back(ch);
    }

  sort(ret);
  return ret;
  }

void GameScript::printCannotUseError(Npc& npc, int32_t atr, int32_t nValue) {
  auto id = vm.find_symbol_by_name("G_CanNotUse");
  if(id==nullptr)
    return;

  ScopeVar self(*vm.global_self(), npc.handlePtr());
  vm.call_function<void>(id, npc.isPlayer(), atr, nValue);
  }

void GameScript::printCannotCastError(Npc &npc, int32_t plM, int32_t itM) {
  auto id = vm.find_symbol_by_name("G_CanNotCast");
  if(id==nullptr)
    return;

  ScopeVar self(*vm.global_self(), npc.handlePtr());
  vm.call_function<void>(id, npc.isPlayer(), itM, plM);
  }

void GameScript::printCannotBuyError(Npc &npc) {
  auto id = vm.find_symbol_by_name("player_trade_not_enough_gold");
  if(id==nullptr)
    return;
  ScopeVar self(*vm.global_self(), npc.handlePtr());
  vm.call_function<void>(id);
  }

void GameScript::printMobMissingItem(Npc &npc) {
  auto id = vm.find_symbol_by_name("player_mob_missing_item");
  if(id==nullptr) {
    if(owner.version().game==1)
      owner.player()->playAnimByName("T_DONTKNOW", BS_NONE);
    return;
    }
  ScopeVar self(*vm.global_self(), npc.handlePtr());
  vm.call_function<void>(id);
  }

void GameScript::printMobMissingKey(Npc& npc) {
  auto id = vm.find_symbol_by_name("player_mob_missing_key");
  if(id==nullptr) {
    if(owner.version().game==1)
      owner.player()->playAnimByName("T_DONTKNOW", BS_NONE);
    return;
    }
  ScopeVar self(*vm.global_self(), npc.handlePtr());
  vm.call_function<void>(id);
  }

void GameScript::printMobAnotherIsUsing(Npc &npc) {
  auto id = vm.find_symbol_by_name("player_mob_another_is_using");
  if(id==nullptr) {
    if(owner.version().game==1)
      owner.player()->playAnimByName("T_DONTKNOW", BS_NONE);
    return;
    }
  ScopeVar self(*vm.global_self(), npc.handlePtr());
  vm.call_function<void>(id);
  }

void GameScript::printMobMissingKeyOrLockpick(Npc& npc) {
  auto id = vm.find_symbol_by_name("player_mob_missing_key_or_lockpick");
  if(id==nullptr) {
    if(owner.version().game==1)
      owner.player()->playAnimByName("T_DONTKNOW", BS_NONE);
    return;
    }
  ScopeVar self(*vm.global_self(), npc.handlePtr());
  vm.call_function<void>(id);
  }

void GameScript::printMobMissingLockpick(Npc& npc) {
  auto id = vm.find_symbol_by_name("player_mob_missing_lockpick");
  if(id==nullptr) {
    if(owner.version().game==1)
      owner.player()->playAnimByName("T_DONTKNOW", BS_NONE);
    return;
    }
  ScopeVar self(*vm.global_self(), npc.handlePtr());
  vm.call_function<void>(id);
  }

void GameScript::printMobTooFar(Npc& npc) {
  auto id = vm.find_symbol_by_name("player_mob_too_far_away");
  if(id==nullptr) {
    owner.player()->playAnimByName("T_DONTKNOW", BS_NONE);
    return;
    }
  ScopeVar self(*vm.global_self(), npc.handlePtr());
  vm.call_function<void>(id);
  }

bool GameScript::aiOutput(Npc &npc, std::string_view outputname, bool overlay) {
  string_frm name(outputname,".WAV");
  uint64_t   dt = 0;

  world().addDlgSound(name, npc.mapHeadBone(), WorldSound::talkRange, dt);
  npc.setAiOutputBarrier(messageTime(outputname),overlay);

  return true;
  }

bool GameScript::aiOutputSvm(Npc &npc, std::string_view outputname, bool overlay) {
  if(overlay) {
    if(tickCount()<svmBarrier)
      return true;
    svmBarrier = tickCount()+messageTime(outputname);
    }

  if(!outputname.empty())
    return aiOutput(npc,outputname,overlay);
  return true;
  }

std::string_view GameScript::messageFromSvm(std::string_view id, int voice) const {
  return svm->find(id,voice);
  }

std::string_view GameScript::messageByName(std::string_view id) const {
  auto blk = dialogs.block_by_name(id);
  if(blk == nullptr)
    return "";
  auto msg = blk->get_message();
  if(msg == nullptr)
    return "";
  return msg->text;
  }

uint32_t GameScript::messageTime(std::string_view id) const {
  static std::string tmp;
  tmp.assign(id);

  uint32_t& time = msgTimings[tmp];
  if(time>0)
    return time;

  string_frm name(id,".WAV");
  auto s = Resources::loadSoundBuffer(name);
  if(s.timeLength()>0) {
    time = uint32_t(s.timeLength());
    } else {
    auto txt = messageByName(id);
    time = uint32_t(float(txt.length())*viewTimePerChar);
    time = std::min(time, 16000u);
    }
  return time;
  }

void GameScript::printNothingToGet() {
  auto id = vm.find_symbol_by_name("player_plunder_is_empty");
  if(id==nullptr) {
    if(owner.version().game==1)
      owner.player()->playAnimByName("T_DONTKNOW", BS_NONE);
    return;
    }
  ScopeVar self(*vm.global_self(), owner.player()->handlePtr());
  vm.call_function<void>(id);
  }

zenkit::IInfo* GameScript::findInfo(size_t id) {
  auto* sym = vm.find_symbol_by_index(uint32_t(id));
  if(sym==nullptr||!sym->is_instance_of<zenkit::IInfo>())
    return nullptr;
  auto* h = sym->get_instance().get();
  if(h==nullptr)
    Log::e("invalid c_info object: \"",sym->name(),"\"");
  return reinterpret_cast<zenkit::IInfo*>(h);
  }

AiOuputPipe *GameScript::openAiOuput() {
  return aiDefaultPipe.get();
  }

AiOuputPipe *GameScript::openDlgOuput(Npc &player, Npc &npc) {
  if(player.isPlayer())
    return owner.openDlgOuput(player,npc);
  return owner.openDlgOuput(npc,player);
  }

bool GameScript::npc_knowsinfo(std::shared_ptr<zenkit::INpc> npcRef, int infoinstance) {
  auto npc = findNpc(npcRef);
  if(!npc)
    return false;

  zenkit::INpc& vnpc = npc->handle();
  return doesNpcKnowInfo(vnpc, uint32_t(infoinstance));
  }

void GameScript::info_addchoice(int infoInstance, std::string_view text, int func) {
  auto info = findInfo(uint32_t(infoInstance));
  if(info==nullptr)
    return;
  zenkit::IInfoChoice choice {};
  choice.text     = text;
  choice.function = func;
  info->add_choice(choice);
  }

void GameScript::info_clearchoices(int infoInstance) {
  auto info = findInfo(uint32_t(infoInstance));
  if(info==nullptr)
    return;
  info->choices.clear();
  }

bool GameScript::infomanager_hasfinished() {
  return !owner.isInDialog();
  }

void GameScript::sort(std::vector<GameScript::DlgChoice> &dlg) {
  std::sort(dlg.begin(),dlg.end(),[](const GameScript::DlgChoice& l,const GameScript::DlgChoice& r){
    return std::tie(l.sort,l.scriptFn)<std::tie(r.sort,r.scriptFn); // small hack with scriptfn to reproduce behavior of original game
    });
  }

void GameScript::setNpcInfoKnown(const zenkit::INpc& npc, const zenkit::IInfo& info) {
  auto id = std::make_pair(vm.find_symbol_by_instance(npc)->index(),vm.find_symbol_by_instance(info)->index());
  dlgKnownInfos.insert(id);
  }

bool GameScript::doesNpcKnowInfo(const zenkit::INpc& npc, size_t infoInstance) const {
  auto id = std::make_pair(vm.find_symbol_by_instance(npc)->index(),infoInstance);
  return dlgKnownInfos.find(id)!=dlgKnownInfos.end();
  }
