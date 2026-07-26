#include "npc.h"

#include <algorithm>

#include <Tempest/Matrix4x4>
#include <Tempest/Log>

#include "graphics/mesh/skeleton.h"
#include "graphics/visualfx.h"
#include "game/damagecalculator.h"
#include "game/gamesession.h"
#include "game/serialize.h"
#include "game/gamescript.h"
#include "game/mmosemantichooks.h"
#include "utils/string_frm.h"
#include "world/objects/interactive.h"
#include "world/objects/item.h"
#include "world/world.h"
#include "utils/versioninfo.h"
#include "utils/fileext.h"
#include "utils/dbgpainter.h"
#include "camera.h"
#include "gothic.h"
#include "resources.h"

using namespace Tempest;

void Npc::invalidateTalentOverlays() {
  const Talent tl[] = {TALENT_1H, TALENT_2H, TALENT_BOW, TALENT_CROSSBOW, TALENT_ACROBAT};
  for(Talent i:tl) {
    invalidateTalentOverlays(i);
    }
  }

void Npc::invalidateTalentOverlays(Talent t) {
  const auto scheme = visual.visualSkeletonScheme();
  if(scheme.empty())
    return;

  const auto lvl = talentsSk[t];
  if(t==TALENT_1H){
    if(lvl==0){
      delOverlay(string_frm(scheme,"_1HST1.MDS"));
      delOverlay(string_frm(scheme,"_1HST2.MDS"));
      }
    else if(lvl==1){
      addOverlay(string_frm(scheme,"_1HST1.MDS"),0);
      delOverlay(string_frm(scheme,"_1HST2.MDS"));
      }
    else if(lvl==2){
      delOverlay(string_frm(scheme,"_1HST1.MDS"));
      addOverlay(string_frm(scheme,"_1HST2.MDS"),0);
      }
    }
  else if(t==TALENT_2H){
    if(lvl==0){
      delOverlay(string_frm(scheme,"_2HST1.MDS"));
      delOverlay(string_frm(scheme,"_2HST2.MDS"));
      }
    else if(lvl==1){
      addOverlay(string_frm(scheme,"_2HST1.MDS"),0);
      delOverlay(string_frm(scheme,"_2HST2.MDS"));
      }
    else if(lvl==2){
      delOverlay(string_frm(scheme,"_2HST1.MDS"));
      addOverlay(string_frm(scheme,"_2HST2.MDS"),0);
      }
    }
  else if(t==TALENT_BOW){
    if(lvl==0){
      delOverlay(string_frm(scheme,"_BOWT1.MDS"));
      delOverlay(string_frm(scheme,"_BOWT2.MDS"));
      }
    else if(lvl==1){
      addOverlay(string_frm(scheme,"_BOWT1.MDS"),0);
      delOverlay(string_frm(scheme,"_BOWT2.MDS"));
      }
    else if(lvl==2){
      delOverlay(string_frm(scheme,"_BOWT1.MDS"));
      addOverlay(string_frm(scheme,"_BOWT2.MDS"),0);
      }
    }
  else if(t==TALENT_CROSSBOW){
    if(lvl==0){
      delOverlay(string_frm(scheme,"_CBOWT1.MDS"));
      delOverlay(string_frm(scheme,"_CBOWT2.MDS"));
      }
    else if(lvl==1){
      addOverlay(string_frm(scheme,"_CBOWT1.MDS"),0);
      delOverlay(string_frm(scheme,"_CBOWT2.MDS"));
      }
    else if(lvl==2){
      delOverlay(string_frm(scheme,"_CBOWT1.MDS"));
      addOverlay(string_frm(scheme,"_CBOWT2.MDS"),0);
      }
    }
  else if(t==TALENT_ACROBAT){
    if(lvl==0)
      delOverlay(string_frm(scheme,"_ACROBATIC.MDS")); else
      addOverlay(string_frm(scheme,"_ACROBATIC.MDS"),0);
    }
  }

void Npc::setTalentSkill(Talent t, int32_t lvl) {
  if(t>=TALENT_MAX_G2)
    return;
  talentsSk[t] = lvl;
  invalidateTalentOverlays(t);
  }

int32_t Npc::talentSkill(Talent t) const {
  if(t<TALENT_MAX_G2)
    return talentsSk[t];
  return 0;
  }

void Npc::setTalentValue(Talent t, int32_t lvl) {
  if(t<TALENT_MAX_G2)
    talentsVl[t] = lvl;
  }

int32_t Npc::talentValue(Talent t) const {
  if(t<TALENT_MAX_G2)
    return talentsVl[t];
  return 0;
  }

int32_t Npc::hitChance(Talent t) const {
  if(t<=zenkit::INpc::hitchance_count)
    return hnpc->hitchance[t];
  return 0;
  }

bool Npc::isRefuseTalk() const {
  return refuseTalkMilis>=owner.tickCount();
  }

int32_t Npc::mageCycle() const {
  return talentSkill(TALENT_MAGE);
  }

bool Npc::canSneak() const {
  return talentSkill(TALENT_SNEAK)!=0;
  }

void Npc::setRefuseTalk(uint64_t milis) {
  refuseTalkMilis = owner.tickCount()+milis;
  }

int32_t Npc::attribute(Attribute a) const {
  if(a<ATR_MAX)
    return hnpc->attribute[a];
  return 0;
  }

void Npc::changeAttribute(Attribute a, int32_t val, bool allowUnconscious) {
  if(mmoAuthorityGate.rejectLocalGameplay(
         Mmo::ClientPresentation::NpcLocalGameplayEntryPoint::AttributeMutation))
    return;
  if(a>=ATR_MAX || val==0)
    return;

  if(val<0 && a==ATR_HITPOINTS) {
    if(isPlayer() && Gothic::inst().isGodMode())
      return;
    if(isPlayer() && owner.currentCs()!=nullptr)
      return;
    if(isImmortal())
      return;
    }

  const int32_t valueBefore = hnpc->attribute[a];

  hnpc->attribute[a]+=val;
  if(hnpc->attribute[a]<0)
    hnpc->attribute[a]=0;
  if(a==ATR_HITPOINTS && hnpc->attribute[a]>hnpc->attribute[ATR_HITPOINTSMAX])
    hnpc->attribute[a] = hnpc->attribute[ATR_HITPOINTSMAX];
  if(a==ATR_MANA && hnpc->attribute[a]>hnpc->attribute[ATR_MANAMAX])
    hnpc->attribute[a] = hnpc->attribute[ATR_MANAMAX];

  if(val<0)
    invent.invalidateCond(*this);

  if(a==ATR_HITPOINTS) {
    checkHealth(true,allowUnconscious);
    if(aiPolicy==NpcProcessPolicy::AiFar || aiPolicy==NpcProcessPolicy::AiFar2)
      aiState.started = true;
    }

  Mmo::Hooks::onCharacterAttributeChanged(*this, a, valueBefore, hnpc->attribute[a], val,
                                          currentOther,
                                          "game/world/objects/npc_attributes.cpp:Npc::changeAttribute");
  }

int32_t Npc::protection(Protection p) const {
  if(p<PROT_MAX)
    return hnpc->protection[p];
  return 0;
  }

void Npc::changeProtection(Protection p, int32_t val) {
  if(mmoAuthorityGate.rejectLocalGameplay(
         Mmo::ClientPresentation::NpcLocalGameplayEntryPoint::AttributeMutation))
    return;
  if(p<PROT_MAX)
    hnpc->protection[p]=val;
  }

uint32_t Npc::instanceSymbol() const {
  return uint32_t(hnpc->symbol_index());
  }

uint32_t Npc::guild() const {
  return std::min(uint32_t(hnpc->guild), uint32_t(GIL_MAX-1));
  }

bool Npc::isMonster() const {
  const bool g2 = owner.version().game==2;
  const auto SEPERATOR_ORC = g2 ? GIL_SEPERATOR_ORC : GIL_G1_SEPERATOR_ORC;
  const auto SEPERATOR_HUM = g2 ? GIL_SEPERATOR_HUM : GIL_G1_SEPERATOR_HUM;
  return SEPERATOR_HUM<guild() && guild()<SEPERATOR_ORC;
  }

bool Npc::isHuman() const {
  const bool g2 = owner.version().game==2;
  const auto SEPERATOR_HUM = g2 ? GIL_SEPERATOR_HUM : GIL_G1_SEPERATOR_HUM;
  return guild() < SEPERATOR_HUM;
  }

void Npc::setTrueGuild(int32_t g) {
  trGuild = g;
  }

int32_t Npc::trueGuild() const {
  if(trGuild==GIL_NONE)
    return hnpc->guild;
  return trGuild;
  }

int32_t Npc::magicCyrcle() const {
  return talentSkill(TALENT_RUNES);
  }

int32_t Npc::level() const {
  return hnpc->level;
  }

int32_t Npc::experience() const {
  return hnpc->exp;
  }

int32_t Npc::experienceNext() const {
  return hnpc->exp_next;
  }

int32_t Npc::learningPoints() const {
  return hnpc->lp;
  }

int32_t Npc::diveTime() const {
  return mvAlgo.diveTime();
  }

void Npc::setAttitude(Attitude att) {
  if(mmoAuthorityGate.rejectLocalGameplay(
         Mmo::ClientPresentation::NpcLocalGameplayEntryPoint::TargetSelection))
    return;
  permAttitude = att;
  }

bool Npc::isFriend() const {
  bool g2 = owner.version().game==2;
  return ( g2 && hnpc->type==zenkit::NpcType::G2_FRIEND) ||
         (!g2 && hnpc->type==zenkit::NpcType::G1_FRIEND);
  }

void Npc::setTempAttitude(Attitude att) {
  if(mmoAuthorityGate.rejectLocalGameplay(
         Mmo::ClientPresentation::NpcLocalGameplayEntryPoint::TargetSelection))
    return;
  tmpAttitude = att;
  }
