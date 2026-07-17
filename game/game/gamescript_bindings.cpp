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

struct ScopeCtx final {
  ScopeCtx(GameScript& script, NpcProcessPolicy pp) : script(script) {
    prev = script.aiProcessPolicy;
    script.aiProcessPolicy = pp;
    }

  ~ScopeCtx() {
    script.aiProcessPolicy = prev;
    }

  GameScript&      script;
  NpcProcessPolicy prev = NpcProcessPolicy::AiNormal;
  };

namespace {

struct MmoScriptIntValue final {
  size_t   symbolIndex = 0;
  uint16_t valueIndex  = 0;
  int32_t  value       = 0;
};

struct MmoProgressionValue final {
  int32_t level          = 0;
  int32_t experience     = 0;
  int32_t experienceNext = 0;
  int32_t learningPoints = 0;
};

struct MmoQuestValue final {
  std::string      name;
  QuestLog::Status status = QuestLog::Status::Running;
  size_t           entryCount = 0;
};

struct MmoScriptSnapshot final {
  bool                                      enabled = false;
  Npc*                                      actor = nullptr;
  uint32_t                                  scriptFunctionSymbol = 0;
  std::string                               scriptFunctionName;
  std::vector<MmoScriptIntValue>            intValues;
  MmoProgressionValue                       progression;
  std::set<std::pair<size_t, size_t>>       knownDialogs;
  std::vector<MmoQuestValue>                quests;
};

std::string_view questStatusName(QuestLog::Status status) noexcept {
  switch(status) {
    case QuestLog::Status::Running:  return "running";
    case QuestLog::Status::Success:  return "success";
    case QuestLog::Status::Failed:   return "failed";
    case QuestLog::Status::Obsolete: return "obsolete";
    }
  return "running";
}

MmoProgressionValue captureProgression(const Npc& actor) noexcept {
  MmoProgressionValue out;
  out.level          = actor.level();
  out.experience     = actor.experience();
  out.experienceNext = actor.experienceNext();
  out.learningPoints = actor.learningPoints();
  return out;
}

void captureScriptInts(GameScript& script, std::vector<MmoScriptIntValue>& out) {
  out.clear();
  out.reserve(512);
  const size_t count = script.symbolsCount();
  for(size_t i = 0; i < count; ++i) {
    auto* sym = script.findSymbol(i);
    if(sym == nullptr || sym->is_member() || sym->is_const() ||
       sym->type() != zenkit::DaedalusDataType::INT || sym->count() == 0)
      continue;
    const auto valueCount = sym->count();
    const auto boundedValueCount = valueCount > 65535 ? 65535 : valueCount;
    for(size_t j = 0; j < boundedValueCount; ++j) {
      const auto valueIndex = uint16_t(j);
      MmoScriptIntValue v;
      v.symbolIndex = i;
      v.valueIndex  = valueIndex;
      v.value       = sym->get_int(valueIndex);
      out.push_back(v);
      }
    }
}

std::vector<MmoQuestValue> captureQuests(const QuestLog& log) {
  std::vector<MmoQuestValue> out;
  out.reserve(log.questCount());
  for(size_t i = 0; i < log.questCount(); ++i) {
    const auto& q = log.quest(i);
    MmoQuestValue v;
    v.name       = q.name;
    v.status     = q.status;
    v.entryCount = q.entry.size();
    out.push_back(std::move(v));
    }
  return out;
}

const MmoScriptIntValue* findScriptIntValue(const std::vector<MmoScriptIntValue>& values,
                                            size_t symbolIndex,
                                            uint16_t valueIndex) noexcept {
  for(const auto& v : values) {
    if(v.symbolIndex == symbolIndex && v.valueIndex == valueIndex)
      return &v;
    }
  return nullptr;
}

const MmoQuestValue* findQuestValue(const std::vector<MmoQuestValue>& values,
                                    std::string_view name) noexcept {
  for(const auto& q : values) {
    if(q.name == name)
      return &q;
    }
  return nullptr;
}

MmoScriptSnapshot captureMmoScriptSnapshot(GameScript& script,
                                           Npc* actor,
                                           zenkit::DaedalusSymbol* scriptFunction) noexcept {
  MmoScriptSnapshot out;
  if(!Mmo::Hooks::shouldCaptureScriptAction(actor) || scriptFunction == nullptr)
    return out;
  try {
    out.enabled = true;
    out.actor = actor;
    out.scriptFunctionSymbol = uint32_t(scriptFunction->index());
    out.scriptFunctionName = scriptFunction->name();
    captureScriptInts(script, out.intValues);
    out.progression = captureProgression(*actor);
    out.knownDialogs = script.knownDialogInfos();
    out.quests = captureQuests(script.questLog());
    }
  catch(...) {
    out = {};
    }
  return out;
}

void emitMmoScriptDiff(GameScript& script, MmoScriptSnapshot& before, const char* sourceLocation) noexcept {
  if(!before.enabled || before.actor == nullptr)
    return;

  try {
    std::vector<MmoScriptIntValue> afterInts;
    captureScriptInts(script, afterInts);
  for(const auto& after : afterInts) {
    const auto* prior = findScriptIntValue(before.intValues, after.symbolIndex, after.valueIndex);
    if(prior == nullptr || prior->value == after.value)
      continue;
    auto* sym = script.findSymbol(after.symbolIndex);
    if(sym == nullptr)
      continue;
    Mmo::Hooks::onScriptIntChanged(*before.actor,
                                   before.scriptFunctionSymbol,
                                   before.scriptFunctionName,
                                   after.symbolIndex,
                                   after.valueIndex,
                                   sym->name(),
                                   prior->value,
                                   after.value,
                                   sourceLocation);
    }

  const auto afterProgression = captureProgression(*before.actor);
  Mmo::Hooks::onCharacterProgressionChanged(*before.actor,
                                            before.scriptFunctionSymbol,
                                            before.scriptFunctionName,
                                            before.progression.level,
                                            afterProgression.level,
                                            before.progression.experience,
                                            afterProgression.experience,
                                            before.progression.experienceNext,
                                            afterProgression.experienceNext,
                                            before.progression.learningPoints,
                                            afterProgression.learningPoints,
                                            sourceLocation);

  const auto& afterKnownDialogs = script.knownDialogInfos();
  for(const auto& known : afterKnownDialogs) {
    if(before.knownDialogs.find(known) != before.knownDialogs.end())
      continue;
    const auto* npcSym = script.findSymbol(known.first);
    const auto* infoSym = script.findSymbol(known.second);
    const std::string npcName = npcSym != nullptr ? npcSym->name() : std::string{};
    const std::string infoName = infoSym != nullptr ? infoSym->name() : std::string{};
    Mmo::Hooks::onKnownDialogChanged(*before.actor,
                                     before.scriptFunctionSymbol,
                                     before.scriptFunctionName,
                                     known.first,
                                     npcName,
                                     known.second,
                                     infoName,
                                     true,
                                     sourceLocation);
    }

  const auto afterQuests = captureQuests(script.questLog());
  for(const auto& quest : afterQuests) {
    const auto* prior = findQuestValue(before.quests, quest.name);
    if(prior != nullptr && prior->status == quest.status && prior->entryCount == quest.entryCount)
      continue;
    Mmo::Hooks::onQuestChanged(*before.actor,
                               before.scriptFunctionSymbol,
                               before.scriptFunctionName,
                               quest.name,
                               questStatusName(quest.status),
                               quest.entryCount,
                               sourceLocation);
    }
    }
  catch(...) {
    }
}

} // namespace

void GameScript::initCommon() {
  const auto registerAiExternals = [this] {
    bindExternal("ai_output",                      &GameScript::ai_output);
    bindExternal("ai_stopprocessinfos",            &GameScript::ai_stopprocessinfos);
    bindExternal("ai_processinfos",                &GameScript::ai_processinfos);
    bindExternal("ai_standup",                     &GameScript::ai_standup);
    bindExternal("ai_standupquick",                &GameScript::ai_standupquick);
    bindExternal("ai_continueroutine",             &GameScript::ai_continueroutine);
    bindExternal("ai_stoplookat",                  &GameScript::ai_stoplookat);
    bindExternal("ai_lookat",                      &GameScript::ai_lookat);
    bindExternal("ai_lookatnpc",                   &GameScript::ai_lookatnpc);
    bindExternal("ai_removeweapon",                &GameScript::ai_removeweapon);
    bindExternal("ai_unreadyspell",                &GameScript::ai_unreadyspell);
    bindExternal("ai_turnaway",                    &GameScript::ai_turnaway);
    bindExternal("ai_turntonpc",                   &GameScript::ai_turntonpc);
    bindExternal("ai_whirlaround",                 &GameScript::ai_whirlaround);
    bindExternal("ai_outputsvm",                   &GameScript::ai_outputsvm);
    bindExternal("ai_outputsvm_overlay",           &GameScript::ai_outputsvm_overlay);
    bindExternal("ai_startstate",                  &GameScript::ai_startstate);
    bindExternal("ai_playani",                     &GameScript::ai_playani);
    bindExternal("ai_setwalkmode",                 &GameScript::ai_setwalkmode);
    bindExternal("ai_wait",                        &GameScript::ai_wait);
    bindExternal("ai_waitms",                      &GameScript::ai_waitms);
    bindExternal("ai_aligntowp",                   &GameScript::ai_aligntowp);
    bindExternal("ai_gotowp",                      &GameScript::ai_gotowp);
    bindExternal("ai_gotofp",                      &GameScript::ai_gotofp);
    bindExternal("ai_playanibs",                   &GameScript::ai_playanibs);
    bindExternal("ai_equiparmor",                  &GameScript::ai_equiparmor);
    bindExternal("ai_equipbestarmor",              &GameScript::ai_equipbestarmor);
    bindExternal("ai_equipbestmeleeweapon",        &GameScript::ai_equipbestmeleeweapon);
    bindExternal("ai_equipbestrangedweapon",       &GameScript::ai_equipbestrangedweapon);
    bindExternal("ai_usemob",                      &GameScript::ai_usemob);
    bindExternal("ai_teleport",                    &GameScript::ai_teleport);
    bindExternal("ai_stoppointat",                 &GameScript::ai_stoppointat);
    bindExternal("ai_drawweapon",                  &GameScript::ai_drawweapon);
    bindExternal("ai_readymeleeweapon",            &GameScript::ai_readymeleeweapon);
    bindExternal("ai_readyrangedweapon",           &GameScript::ai_readyrangedweapon);
    bindExternal("ai_readyspell",                  &GameScript::ai_readyspell);
    bindExternal("ai_attack",                      &GameScript::ai_attack);
    bindExternal("ai_flee",                        &GameScript::ai_flee);
    bindExternal("ai_dodge",                       &GameScript::ai_dodge);
    bindExternal("ai_unequipweapons",              &GameScript::ai_unequipweapons);
    bindExternal("ai_unequiparmor",                &GameScript::ai_unequiparmor);
    bindExternal("ai_gotonpc",                     &GameScript::ai_gotonpc);
    bindExternal("ai_gotonextfp",                  &GameScript::ai_gotonextfp);
    bindExternal("ai_aligntofp",                   &GameScript::ai_aligntofp);
    bindExternal("ai_useitem",                     &GameScript::ai_useitem);
    bindExternal("ai_useitemtostate",              &GameScript::ai_useitemtostate);
    bindExternal("ai_setnpcstostate",              &GameScript::ai_setnpcstostate);
    bindExternal("ai_finishingmove",               &GameScript::ai_finishingmove);
    bindExternal("ai_takeitem",                    &GameScript::ai_takeitem);
    bindExternal("ai_gotoitem",                    &GameScript::ai_gotoitem);
    bindExternal("ai_pointat",                     &GameScript::ai_pointat);
    bindExternal("ai_pointatnpc",                  &GameScript::ai_pointatnpc);
    bindExternal("ai_printscreen",                 &GameScript::ai_printscreen);
    bindExternal("ta_min",                         &GameScript::ta_min);
    bindExternal("perc_setrange",                  &GameScript::perc_setrange);
    };

  const auto registerNpcExternals = [this] {
    bindExternal("hlp_random",                     &GameScript::hlp_random);
    bindExternal("hlp_isvalidnpc",                 &GameScript::hlp_isvalidnpc);
    bindExternal("hlp_getnpc",                     &GameScript::hlp_getnpc);
    bindExternal("hlp_getinstanceid",              &GameScript::hlp_getinstanceid);
    bindExternal("npc_settofightmode",             &GameScript::npc_settofightmode);
    bindExternal("npc_settofistmode",              &GameScript::npc_settofistmode);
    bindExternal("npc_isinstate",                  &GameScript::npc_isinstate);
    bindExternal("npc_isinroutine",                &GameScript::npc_isinroutine);
    bindExternal("npc_wasinstate",                 &GameScript::npc_wasinstate);
    bindExternal("npc_getdisttowp",                &GameScript::npc_getdisttowp);
    bindExternal("npc_exchangeroutine",            &GameScript::npc_exchangeroutine);
    bindExternal("npc_isdead",                     &GameScript::npc_isdead);
    bindExternal("npc_settalentskill",             &GameScript::npc_settalentskill);
    bindExternal("npc_gettalentskill",             &GameScript::npc_gettalentskill);
    bindExternal("npc_settalentvalue",             &GameScript::npc_settalentvalue);
    bindExternal("npc_gettalentvalue",             &GameScript::npc_gettalentvalue);
    bindExternal("npc_setrefusetalk",              &GameScript::npc_setrefusetalk);
    bindExternal("npc_refusetalk",                 &GameScript::npc_refusetalk);
    bindExternal("npc_getbodystate",               &GameScript::npc_getbodystate);
    bindExternal("npc_getlookattarget",            &GameScript::npc_getlookattarget);
    bindExternal("npc_getdisttonpc",               &GameScript::npc_getdisttonpc);
    bindExternal("npc_setperctime",                &GameScript::npc_setperctime);
    bindExternal("npc_percenable",                 &GameScript::npc_percenable);
    bindExternal("npc_percdisable",                &GameScript::npc_percdisable);
    bindExternal("npc_getnearestwp",               &GameScript::npc_getnearestwp);
    bindExternal("npc_getnextwp",                  &GameScript::npc_getnextwp);
    bindExternal("npc_clearaiqueue",               &GameScript::npc_clearaiqueue);
    bindExternal("npc_isplayer",                   &GameScript::npc_isplayer);
    bindExternal("npc_getstatetime",               &GameScript::npc_getstatetime);
    bindExternal("npc_setstatetime",               &GameScript::npc_setstatetime);
    bindExternal("npc_changeattribute",            &GameScript::npc_changeattribute);
    bindExternal("npc_isonfp",                     &GameScript::npc_isonfp);
    bindExternal("npc_getheighttonpc",             &GameScript::npc_getheighttonpc);
    bindExternal("npc_canseenpc",                  &GameScript::npc_canseenpc);
    bindExternal("npc_canseenpcfreelos",           &GameScript::npc_canseenpcfreelos);
    bindExternal("npc_canseeitem",                 &GameScript::npc_canseeitem);
    bindExternal("npc_isinfightmode",              &GameScript::npc_isinfightmode);
    bindExternal("npc_settarget",                  &GameScript::npc_settarget);
    bindExternal("npc_gettarget",                  &GameScript::npc_gettarget);
    bindExternal("npc_getnexttarget",              &GameScript::npc_getnexttarget);
    bindExternal("npc_sendpassiveperc",            &GameScript::npc_sendpassiveperc);
    bindExternal("npc_sendsingleperc",             &GameScript::npc_sendsingleperc);
    bindExternal("npc_checkinfo",                  &GameScript::npc_checkinfo);
    bindExternal("npc_getportalguild",             &GameScript::npc_getportalguild);
    bindExternal("npc_isinplayersroom",            &GameScript::npc_isinplayersroom);
    bindExternal("npc_perceiveall",                &GameScript::npc_perceiveall);
    bindExternal("npc_stopani",                    &GameScript::npc_stopani);
    bindExternal("npc_settrueguild",               &GameScript::npc_settrueguild);
    bindExternal("npc_gettrueguild",               &GameScript::npc_gettrueguild);
    bindExternal("npc_getattitude",                &GameScript::npc_getattitude);
    bindExternal("npc_getpermattitude",            &GameScript::npc_getpermattitude);
    bindExternal("npc_setattitude",                &GameScript::npc_setattitude);
    bindExternal("npc_settempattitude",            &GameScript::npc_settempattitude);
    bindExternal("npc_hasbodyflag",                &GameScript::npc_hasbodyflag);
    bindExternal("npc_getlasthitspellid",          &GameScript::npc_getlasthitspellid);
    bindExternal("npc_getlasthitspellcat",         &GameScript::npc_getlasthitspellcat);
    bindExternal("npc_playani",                    &GameScript::npc_playani);
    bindExternal("npc_isdetectedmobownedbynpc",    &GameScript::npc_isdetectedmobownedbynpc);
    bindExternal("npc_getdetectedmob",             &GameScript::npc_getdetectedmob);
    bindExternal("npc_isdetectedmobownedbyguild",  &GameScript::npc_isdetectedmobownedbyguild);
    bindExternal("npc_canseesource",               &GameScript::npc_canseesource);
    bindExternal("npc_isincutscene",               &GameScript::npc_isincutscene);
    bindExternal("npc_getdisttoplayer",            &GameScript::npc_getdisttoplayer);
    };

  const auto registerInventoryExternals = [this] {
    bindExternal("npc_hasitems",                   &GameScript::npc_hasitems);
    bindExternal("npc_hasspell",                   &GameScript::npc_hasspell);
    bindExternal("npc_getinvitem",                 &GameScript::npc_getinvitem);
    bindExternal("npc_getinvitembyslot",           &GameScript::npc_getinvitembyslot);
    bindExternal("npc_removeinvitem",              &GameScript::npc_removeinvitem);
    bindExternal("npc_removeinvitems",             &GameScript::npc_removeinvitems);
    bindExternal("npc_hasequippedarmor",           &GameScript::npc_hasequippedarmor);
    bindExternal("npc_getequippedmeleeweapon",     &GameScript::npc_getequippedmeleeweapon);
    bindExternal("npc_getequippedrangedweapon",    &GameScript::npc_getequippedrangedweapon);
    bindExternal("npc_getequippedarmor",           &GameScript::npc_getequippedarmor);
    bindExternal("npc_hasequippedweapon",          &GameScript::npc_hasequippedweapon);
    bindExternal("npc_hasequippedmeleeweapon",     &GameScript::npc_hasequippedmeleeweapon);
    bindExternal("npc_hasequippedrangedweapon",    &GameScript::npc_hasequippedrangedweapon);
    bindExternal("npc_getactivespell",             &GameScript::npc_getactivespell);
    bindExternal("npc_getactivespellisscroll",     &GameScript::npc_getactivespellisscroll);
    bindExternal("npc_getactivespellcat",          &GameScript::npc_getactivespellcat);
    bindExternal("npc_setactivespellinfo",         &GameScript::npc_setactivespellinfo);
    bindExternal("npc_getactivespelllevel",        &GameScript::npc_getactivespelllevel);
    bindExternal("npc_getreadiedweapon",           &GameScript::npc_getreadiedweapon);
    bindExternal("npc_hasreadiedweapon",           &GameScript::npc_hasreadiedweapon);
    bindExternal("npc_hasreadiedmeleeweapon",      &GameScript::npc_hasreadiedmeleeweapon);
    bindExternal("npc_hasreadiedrangedweapon",     &GameScript::npc_hasreadiedrangedweapon);
    bindExternal("npc_hasrangedweaponwithammo",    &GameScript::npc_hasrangedweaponwithammo);
    bindExternal("npc_isdrawingspell",             &GameScript::npc_isdrawingspell);
    bindExternal("npc_isdrawingweapon",            &GameScript::npc_isdrawingweapon);
    bindExternal("npc_clearinventory",             &GameScript::npc_clearinventory);
    bindExternal("mob_hasitems",                   &GameScript::mob_hasitems);
    bindExternal("equipitem",                      &GameScript::equipitem);
    bindExternal("createinvitem",                  &GameScript::createinvitem);
    bindExternal("createinvitems",                 &GameScript::createinvitems);
    };

  const auto registerWorldExternals = [this] {
    bindExternal("wld_insertnpc",                  &GameScript::wld_insertnpc);
    bindExternal("wld_removenpc",                  &GameScript::wld_removenpc);
    bindExternal("wld_insertitem",                 &GameScript::wld_insertitem);
    bindExternal("wld_settime",                    &GameScript::wld_settime);
    bindExternal("wld_getday",                     &GameScript::wld_getday);
    bindExternal("wld_playeffect",                 &GameScript::wld_playeffect);
    bindExternal("wld_stopeffect",                 &GameScript::wld_stopeffect);
    bindExternal("wld_getplayerportalguild",       &GameScript::wld_getplayerportalguild);
    bindExternal("wld_getformerplayerportalguild", &GameScript::wld_getformerplayerportalguild);
    bindExternal("wld_setguildattitude",           &GameScript::wld_setguildattitude);
    bindExternal("wld_getguildattitude",           &GameScript::wld_getguildattitude);
    bindExternal("wld_exchangeguildattitudes",     &GameScript::wld_exchangeguildattitudes);
    bindExternal("wld_istime",                     &GameScript::wld_istime);
    bindExternal("wld_isfpavailable",              &GameScript::wld_isfpavailable);
    bindExternal("wld_isnextfpavailable",          &GameScript::wld_isnextfpavailable);
    bindExternal("wld_ismobavailable",             &GameScript::wld_ismobavailable);
    bindExternal("wld_setmobroutine",              &GameScript::wld_setmobroutine);
    bindExternal("wld_getmobstate",                &GameScript::wld_getmobstate);
    bindExternal("wld_assignroomtoguild",          &GameScript::wld_assignroomtoguild);
    bindExternal("wld_detectnpc",                  &GameScript::wld_detectnpc);
    bindExternal("wld_detectnpcex",                &GameScript::wld_detectnpcex);
    bindExternal("wld_detectitem",                 &GameScript::wld_detectitem);
    bindExternal("wld_spawnnpcrange",              &GameScript::wld_spawnnpcrange);
    bindExternal("wld_sendtrigger",                &GameScript::wld_sendtrigger);
    bindExternal("wld_senduntrigger",              &GameScript::wld_senduntrigger);
    bindExternal("wld_israining",                  &GameScript::wld_israining);
    bindExternal("game_initgerman",                &GameScript::game_initgerman);
    bindExternal("game_initenglish",               &GameScript::game_initenglish);
    bindExternal("exitsession",                    &GameScript::exitsession);
    };

  const auto registerItemExternals = [this] {
    bindExternal("hlp_isvaliditem",                &GameScript::hlp_isvaliditem);
    bindExternal("hlp_isitem",                     &GameScript::hlp_isitem);
    bindExternal("npc_ownedbynpc",                 &GameScript::npc_ownedbynpc);
    bindExternal("npc_getdisttoitem",              &GameScript::npc_getdisttoitem);
    bindExternal("npc_getheighttoitem",            &GameScript::npc_getheighttoitem);
    };

  const auto registerModelExternals = [this] {
    bindExternal("mdl_setvisual",                  &GameScript::mdl_setvisual);
    bindExternal("mdl_setvisualbody",              &GameScript::mdl_setvisualbody);
    bindExternal("mdl_setmodelfatness",            &GameScript::mdl_setmodelfatness);
    bindExternal("mdl_applyoverlaymds",            &GameScript::mdl_applyoverlaymds);
    bindExternal("mdl_applyoverlaymdstimed",       &GameScript::mdl_applyoverlaymdstimed);
    bindExternal("mdl_removeoverlaymds",           &GameScript::mdl_removeoverlaymds);
    bindExternal("mdl_setmodelscale",              &GameScript::mdl_setmodelscale);
    bindExternal("mdl_startfaceani",               &GameScript::mdl_startfaceani);
    bindExternal("mdl_applyrandomani",             &GameScript::mdl_applyrandomani);
    bindExternal("mdl_applyrandomanifreq",         &GameScript::mdl_applyrandomanifreq);
    bindExternal("mdl_applyrandomfaceani",         &GameScript::mdl_applyrandomfaceani);
    };

  const auto registerAudioExternals = [this] {
    bindExternal("snd_play",                       &GameScript::snd_play);
    bindExternal("snd_play3d",                     &GameScript::snd_play3d);
    };

  const auto registerDialogExternals = [this] {
    bindExternal("npc_knowsinfo",                  &GameScript::npc_knowsinfo);
    bindExternal("info_addchoice",                 &GameScript::info_addchoice);
    bindExternal("info_clearchoices",              &GameScript::info_clearchoices);
    bindExternal("infomanager_hasfinished",        &GameScript::infomanager_hasfinished);
    };

  const auto registerMissionLogExternals = [this] {
    bindExternal("log_createtopic",                &GameScript::log_createtopic);
    bindExternal("log_settopicstatus",             &GameScript::log_settopicstatus);
    bindExternal("log_addentry",                   &GameScript::log_addentry);
    };

  registerAiExternals();
  registerNpcExternals();
  registerInventoryExternals();
  registerWorldExternals();
  registerItemExternals();
  registerModelExternals();
  registerAudioExternals();
  registerDialogExternals();
  registerMissionLogExternals();

  // vm.validateExternals();

  spells               = std::make_unique<SpellDefinitions>(vm);
  svm                  = std::make_unique<SvmDefinitions>(vm);

  cFocusNorm           = findFocus("Focus_Normal");
  cFocusMelee          = findFocus("Focus_Melee");
  cFocusRange          = findFocus("Focus_Ranged");
  cFocusMage           = findFocus("Focus_Magic");

  ZS_Dead              = aiState(findSymbolIndex("ZS_Dead")).funcIni;
  ZS_Unconscious       = aiState(findSymbolIndex("ZS_Unconscious")).funcIni;
  ZS_Talk              = aiState(findSymbolIndex("ZS_Talk")).funcIni;
  ZS_Attack            = aiState(findSymbolIndex("ZS_Attack")).funcIni;
  ZS_MM_Attack         = aiState(findSymbolIndex("ZS_MM_Attack")).funcIni;

  spellFxInstanceNames = vm.find_symbol_by_name("spellFxInstanceNames");
  spellFxAniLetters    = vm.find_symbol_by_name("spellFxAniLetters");

  if(spellFxInstanceNames==nullptr || spellFxAniLetters==nullptr) {
    throw std::runtime_error("spellFxInstanceNames and/or spellFxAniLetters not found");
    }

  if(owner.version().game==2) {
    auto* currency = vm.find_symbol_by_name("TRADE_CURRENCY_INSTANCE");
    itMi_Gold      = currency!=nullptr ? vm.find_symbol_by_name(currency->get_string()) : nullptr;
    if(itMi_Gold!=nullptr){ // FIXME
      auto item = vm.init_instance<zenkit::IItem>(itMi_Gold);
      goldTxt = item->name;
      }
    auto* tradeMul = vm.find_symbol_by_name("TRADE_VALUE_MULTIPLIER");
    tradeValMult   = tradeMul != nullptr ? tradeMul->get_float() : 1.0f;

    auto* vtime     = vm.find_symbol_by_name("VIEW_TIME_PER_CHAR");
    viewTimePerChar = vtime != nullptr ? vtime->get_float() : 550.f;
    if(viewTimePerChar<=0.f)
      viewTimePerChar = 550.f;

    ItKE_lockpick     = vm.find_symbol_by_name("ItKE_lockpick");
    B_RefreshAtInsert = vm.find_symbol_by_name("B_RefreshAtInsert");
    } else {
    itMi_Gold      = vm.find_symbol_by_name("ItMiNugget");
    if(itMi_Gold!=nullptr) { // FIXME
      auto item = vm.init_instance<zenkit::IItem>(itMi_Gold);
      goldTxt = item->name;
      }
    //
    tradeValMult    = 1.f;
    viewTimePerChar = 550.f;
    ItKE_lockpick   = vm.find_symbol_by_name("itkelockpick");
    }

  if(auto v = vm.find_symbol_by_name("DAM_CRITICAL_MULTIPLIER")) {
    damCriticalMultiplier = v->get_int();
    }

  auto* gilMax = vm.find_symbol_by_name("GIL_MAX");
  gilCount = gilMax!=nullptr ? size_t(gilMax->get_int()) : 0;

  auto* tblSz = vm.find_symbol_by_name("TAB_ANZAHL");
  gilTblSize = tblSz!=nullptr ? size_t(std::sqrt(tblSz->get_int())) : 0;
  gilAttitudes.resize(gilCount*gilCount,ATT_HOSTILE);
  wld_exchangeguildattitudes("GIL_ATTITUDES");

  auto id = vm.find_symbol_by_name("Gil_Values");
  if(id!=nullptr){
    cGuildVal = vm.init_instance<zenkit::IGuildValues>(id);
    for(size_t i=0;i<Guild::GIL_PUBLIC;++i){
      cGuildVal->water_depth_knee   [i]=cGuildVal->water_depth_knee   [Guild::GIL_HUMAN];
      cGuildVal->water_depth_chest  [i]=cGuildVal->water_depth_chest  [Guild::GIL_HUMAN];
      cGuildVal->jumpup_height      [i]=cGuildVal->jumpup_height      [Guild::GIL_HUMAN];
      cGuildVal->swim_time          [i]=cGuildVal->swim_time          [Guild::GIL_HUMAN];
      cGuildVal->dive_time          [i]=cGuildVal->dive_time          [Guild::GIL_HUMAN];
      cGuildVal->step_height        [i]=cGuildVal->step_height        [Guild::GIL_HUMAN];
      cGuildVal->jumplow_height     [i]=cGuildVal->jumplow_height     [Guild::GIL_HUMAN];
      cGuildVal->jumpmid_height     [i]=cGuildVal->jumpmid_height     [Guild::GIL_HUMAN];
      cGuildVal->slide_angle        [i]=cGuildVal->slide_angle        [Guild::GIL_HUMAN];
      cGuildVal->slide_angle2       [i]=cGuildVal->slide_angle2       [Guild::GIL_HUMAN];
      cGuildVal->disable_autoroll   [i]=cGuildVal->disable_autoroll   [Guild::GIL_HUMAN];
      cGuildVal->surface_align      [i]=cGuildVal->surface_align      [Guild::GIL_HUMAN];
      cGuildVal->climb_heading_angle[i]=cGuildVal->climb_heading_angle[Guild::GIL_HUMAN];
      cGuildVal->climb_horiz_angle  [i]=cGuildVal->climb_horiz_angle  [Guild::GIL_HUMAN];
      cGuildVal->climb_ground_angle [i]=cGuildVal->climb_ground_angle [Guild::GIL_HUMAN];
      cGuildVal->fight_range_base   [i]=cGuildVal->fight_range_base   [Guild::GIL_HUMAN];
      cGuildVal->fight_range_fist   [i]=cGuildVal->fight_range_fist   [Guild::GIL_HUMAN];
      cGuildVal->fight_range_g      [i]=cGuildVal->fight_range_g      [Guild::GIL_HUMAN];
      cGuildVal->fight_range_1hs    [i]=cGuildVal->fight_range_1hs    [Guild::GIL_HUMAN];
      cGuildVal->fight_range_1ha    [i]=cGuildVal->fight_range_1ha    [Guild::GIL_HUMAN];
      cGuildVal->fight_range_2hs    [i]=cGuildVal->fight_range_2hs    [Guild::GIL_HUMAN];
      cGuildVal->fight_range_2ha    [i]=cGuildVal->fight_range_2ha    [Guild::GIL_HUMAN];
      cGuildVal->falldown_height    [i]=cGuildVal->falldown_height    [Guild::GIL_HUMAN];
      cGuildVal->falldown_damage    [i]=cGuildVal->falldown_damage    [Guild::GIL_HUMAN];
      cGuildVal->blood_disabled     [i]=cGuildVal->blood_disabled     [Guild::GIL_HUMAN];
      cGuildVal->blood_max_distance [i]=cGuildVal->blood_max_distance [Guild::GIL_HUMAN];
      cGuildVal->blood_amount       [i]=cGuildVal->blood_amount       [Guild::GIL_HUMAN];
      cGuildVal->blood_flow         [i]=cGuildVal->blood_flow         [Guild::GIL_HUMAN];
      cGuildVal->blood_emitter      [i]=cGuildVal->blood_emitter      [Guild::GIL_HUMAN];
      cGuildVal->blood_texture      [i]=cGuildVal->blood_texture      [Guild::GIL_HUMAN];
      cGuildVal->turn_speed         [i]=cGuildVal->turn_speed         [Guild::GIL_HUMAN];
      }
    }

  if(DirectMemory::isRequired(vm))
    dma.reset(new DirectMemory(*this, vm));
  }

void GameScript::exec(const GameScript::DlgChoice &dlg, Npc& player, Npc& npc) {
  ScopeVar self (*vm.global_self(), npc.handlePtr());
  ScopeVar other(*vm.global_other(), player.handlePtr());

  zenkit::IInfo& info = *dlg.handle;

  if(&player!=&npc)
    player.stopAnim("");
  auto& pl = player.handle();

  auto* scriptSymbol = vm.find_symbol_by_index(dlg.scriptFn);
  auto  mmoBefore = captureMmoScriptSnapshot(*this, &player, scriptSymbol);

  if(info.information==int(dlg.scriptFn)) {
    setNpcInfoKnown(pl,info);
    } else {
    for(size_t i=0;i<info.choices.size();){
      if(info.choices[i].function==int(dlg.scriptFn))
        info.choices.erase(info.choices.begin()+int(i)); else
        ++i;
      }
    }
  vm.call_function(scriptSymbol);
  emitMmoScriptDiff(*this, mmoBefore, "GameScript::exec");
  }

void GameScript::invokeState(const std::shared_ptr<zenkit::INpc>& hnpc, const std::shared_ptr<zenkit::INpc>& oth, const char *name) {
  auto id = vm.find_symbol_by_name(name);
  if(id==nullptr)
    return;

  ScopeVar self (*vm.global_self(),  hnpc);
  ScopeVar other(*vm.global_other(), oth);
  vm.call_function<void>(id);
  }

int GameScript::invokeState(Npc* npc, Npc* oth, Npc* vic, ScriptFn fn) {
  if(!fn.isValid())
    return 0;
  if(oth==nullptr){
    // oth=npc; //FIXME: PC_Levelinspektor?
    }
  if(vic==nullptr){
    // vic=owner.player();
    }

  if(fn==ZS_Talk){
    if(oth==nullptr || !oth->isPlayer()) {
      Log::e("unxepected perc acton");
      return 0;
      }
    }

  ScopeCtx ctx   (*this, npc!=nullptr ? npc->processPolicy() : NpcProcessPolicy::AiNormal);
  ScopeVar self  (*vm.global_self(),   npc != nullptr ? npc->handlePtr() : nullptr);
  ScopeVar other (*vm.global_other(),  oth != nullptr ? oth->handlePtr() : nullptr);
  ScopeVar victim(*vm.global_victim(), vic != nullptr ? vic->handlePtr() : nullptr);

  auto* sym = vm.find_symbol_by_index(uint32_t(fn.ptr));
  int   ret = 0;
  if(sym!=nullptr && sym->rtype() == zenkit::DaedalusDataType::INT) {
    ret = vm.call_function<int>(sym);
    }
  else if(sym!=nullptr) {
    vm.call_function<void>(sym);
    ret = 0;
    }

  if(vm.global_other()->is_instance_of<zenkit::INpc>()){
    auto oth2 = reinterpret_cast<zenkit::INpc*>(vm.global_other()->get_instance().get());
    if(oth!=nullptr && oth2!=&oth->handle()) {
      Npc* other = findNpc(oth2);
      npc->setOther(other);
      }
    }
  return ret;
  }

void GameScript::invokeItem(Npc *npc, ScriptFn fn) {
  if(fn==size_t(-1) || fn == 0)
    return;
  auto functionSymbol = vm.find_symbol_by_index(uint32_t(fn.ptr));

  if (functionSymbol == nullptr)
    return;

  ScopeVar self(*vm.global_self(), npc->handlePtr());
  auto mmoBefore = captureMmoScriptSnapshot(*this, npc, functionSymbol);
  vm.call_function<void>(functionSymbol);
  emitMmoScriptDiff(*this, mmoBefore, "GameScript::invokeItem");
  }

int GameScript::invokeMana(Npc &npc, Npc* target, int mana) {
  auto fn = vm.find_symbol_by_name("Spell_ProcessMana");
  if(fn==nullptr)
    return SpellCode::SPL_SENDSTOP;

  ScopeVar self (*vm.global_self(),  npc.handlePtr());
  ScopeVar other(*vm.global_other(), target != nullptr ? target->handlePtr() : nullptr);

  return vm.call_function<int>(fn,mana);
  }

int GameScript::invokeManaRelease(Npc &npc, Npc* target, int mana) {
  auto fn = vm.find_symbol_by_name("Spell_ProcessMana_Release");
  if(fn==nullptr)
    return SpellCode::SPL_SENDSTOP;

  ScopeVar self (*vm.global_self(),  npc.handlePtr());
  ScopeVar other(*vm.global_other(), target != nullptr ? target->handlePtr() : nullptr);

  return vm.call_function<int>(fn,mana);
  }

void GameScript::invokeSpell(Npc &npc, Npc* target, Item &it) {
  auto&      tag = spellFxInstanceNames->get_string(uint16_t(it.spellId()));
  string_frm name("Spell_Cast_",tag);
  auto       fn = vm.find_symbol_by_name(name);
  if(fn==nullptr)
    return;

  // FIXME: actually set the spell level!
  int32_t  splLevel = 0;
  ScopeVar self (*vm.global_self(),  npc.handlePtr());
  ScopeVar other(*vm.global_other(), target != nullptr ? target->handlePtr() : nullptr);
  try {
    if(fn->count()==1) {
      // this is a leveled spell
      vm.call_function<void>(fn, splLevel);
      } else {
      vm.call_function<void>(fn);
      }
    }
  catch(...) {
    Log::d("unable to call spell-script: \"",name,"\'");
    }
  }

int GameScript::invokeCond(Npc& npc, std::string_view func) {
  auto fn = vm.find_symbol_by_name(func);
  if(fn==nullptr) {
    Gothic::inst().onPrint("MOBSI::conditionFunc is not invalid");
    return 1;
    }
  ScopeVar self(*vm.global_self(), npc.handlePtr());
  return vm.call_function<int>(fn);
  }

void GameScript::invokePickLock(Npc& npc, int bSuccess, int bBrokenOpen) {
  auto fn   = vm.find_symbol_by_name("G_PickLock");
  if(fn==nullptr)
    return;
  ScopeVar self(*vm.global_self(), npc.handlePtr());
  vm.call_function<void>(fn, bSuccess, bBrokenOpen);
  }

void GameScript::invokeRefreshAtInsert(Npc& npc) {
  if(B_RefreshAtInsert==nullptr || owner.version().game!=2)
    return;
  ScopeVar self(*vm.global_self(), npc.handlePtr());
  vm.call_function<void>(B_RefreshAtInsert);
  }

void GameScript::useInteractive(const std::shared_ptr<zenkit::INpc>& hnpc, std::string_view func) {
  auto fn = vm.find_symbol_by_name(func);
  if(fn == nullptr)
    return;

  auto* actor = findNpc(hnpc);
  ScopeVar self(*vm.global_self(),hnpc);
  auto mmoBefore = captureMmoScriptSnapshot(*this, actor, fn);
  try {
    vm.call_function<void>(fn);
    emitMmoScriptDiff(*this, mmoBefore, "GameScript::useInteractive");
    }
  catch (...) {
    Log::i("unable to use interactive [",func,"]");
    }
  }
