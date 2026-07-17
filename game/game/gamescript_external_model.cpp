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


void GameScript::mdl_setvisual(std::shared_ptr<zenkit::INpc> npcRef, std::string_view visual) {
  auto npc = findNpc(npcRef);
  if(npc==nullptr)
    return;
  npc->setVisual(visual);
  }

void GameScript::mdl_setvisualbody(std::shared_ptr<zenkit::INpc> npcRef, std::string_view body, int bodyTexNr, int bodyTexColor, std::string_view head, int headTexNr, int teethTexNr, int armor) {
  auto npc = findNpc(npcRef);
  if(npc==nullptr)
    return;

  npc->setVisualBody(headTexNr,teethTexNr,bodyTexNr,bodyTexColor,body,head);
  if(armor>0) {
    if(npc->itemCount(uint32_t(armor))==0)
      npc->addItem(uint32_t(armor),1);
    npc->useItem(uint32_t(armor),Item::NSLOT,true);
    }
  }

void GameScript::mdl_setmodelfatness(std::shared_ptr<zenkit::INpc> npcRef, float fat) {
  auto npc = findNpc(npcRef);
  if(npc!=nullptr)
    npc->setFatness(fat);
  }

void GameScript::mdl_applyoverlaymds(std::shared_ptr<zenkit::INpc> npcRef, std::string_view overlayname) {
  auto npc = findNpc(npcRef);
  if(npc!=nullptr) {
    auto skelet = Resources::loadSkeleton(overlayname);
    npc->addOverlay(skelet,0);
    }
  }

void GameScript::mdl_applyoverlaymdstimed(std::shared_ptr<zenkit::INpc> npcRef, std::string_view overlayname, int ticks) {
  auto npc = findNpc(npcRef);
  if(npc!=nullptr && ticks>0) {
    auto skelet = Resources::loadSkeleton(overlayname);
    npc->addOverlay(skelet,uint64_t(ticks));
    }
  }

void GameScript::mdl_removeoverlaymds(std::shared_ptr<zenkit::INpc> npcRef, std::string_view overlayname) {
  auto npc = findNpc(npcRef);
  if(npc!=nullptr) {
    auto skelet = Resources::loadSkeleton(overlayname);
    npc->delOverlay(skelet);
  }
}

void GameScript::mdl_setmodelscale(std::shared_ptr<zenkit::INpc> npcRef, float x, float y, float z) {
  auto npc = findNpc(npcRef);
  if(npcRef!=nullptr)
    npc->setScale(x,y,z);
  }

void GameScript::mdl_startfaceani(std::shared_ptr<zenkit::INpc> npcRef, std::string_view ani, float intensity, float time) {
  if(npcRef!=nullptr)
    findNpc(npcRef.get())->startFaceAnim(ani,intensity,uint64_t(time*1000.f));
  }

void GameScript::mdl_applyrandomani(std::shared_ptr<zenkit::INpc> npcRef, std::string_view s1, std::string_view s0) {
  (void)npcRef;
  (void)s1;
  (void)s0;

  static bool first=true;
  if(first){
    Log::e("not implemented call [mdl_applyrandomani]");
    first=false;
    }
  }

void GameScript::mdl_applyrandomanifreq(std::shared_ptr<zenkit::INpc> npcRef, std::string_view s1, float f0) {
  (void)f0;
  (void)s1;
  (void)npcRef;

  static bool first=true;
  if(first){
    Log::e("not implemented call [mdl_applyrandomanifreq]");
    first=false;
    }
  }

void GameScript::mdl_applyrandomfaceani(std::shared_ptr<zenkit::INpc> npcRef, std::string_view name, float timeMin, float timeMinVar, float timeMax, float timeMaxVar, float probMin) {
  (void)probMin;
  (void)timeMaxVar;
  (void)timeMax;
  (void)timeMinVar;
  (void)timeMin;
  (void)name;
  (void)npcRef;

  static bool first=true;
  if(first){
    Log::e("not implemented call [mdl_applyrandomfaceani]");
    first=false;
    }
  }
