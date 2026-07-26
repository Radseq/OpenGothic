#include "npc.h"
#include "npc_transform_back.h"

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

void Npc::restorePersistentState(const PersistentState& state) {
  if(mmoAuthorityGate.rejectLocalGameplay(
         Mmo::ClientPresentation::NpcLocalGameplayEntryPoint::AttributeMutation)) {
    return;
  }
  hnpc->guild = state.guild;
  setTrueGuild(state.trueGuild);
  hnpc->level = std::max(0, state.level);
  hnpc->exp   = std::max(0, state.experience);
  hnpc->exp_next = std::max(0, state.experienceNext);
  hnpc->lp       = std::max(0, state.learningPoints);
  if(state.permanentAttitude>=ATT_NULL && state.permanentAttitude<=ATT_FRIENDLY)
    setAttitude(Attitude(state.permanentAttitude));
  if(state.temporaryAttitude>=ATT_NULL && state.temporaryAttitude<=ATT_FRIENDLY)
    setTempAttitude(Attitude(state.temporaryAttitude));

  for(size_t i=0; i<state.attributes.size(); ++i)
    hnpc->attribute[i] = std::max(0, state.attributes[i]);
  for(size_t i=0; i<state.protections.size(); ++i)
    hnpc->protection[i] = state.protections[i];
  for(size_t i=0; i<state.talentSkills.size(); ++i) {
    setTalentSkill(Talent(i), state.talentSkills[i]);
    setTalentValue(Talent(i), state.talentValues[i]);
    if(i<=zenkit::INpc::hitchance_count)
      hnpc->hitchance[i] = state.hitChances[i];
    }
  for(size_t i=0; i<state.missions.size(); ++i)
    hnpc->mission[i] = state.missions[i];
  for(size_t i=0; i<state.aiVariables.size(); ++i)
    hnpc->aivar[i] = state.aiVariables[i];

  if(state.dead) {
    hnpc->attribute[ATR_HITPOINTS] = 0;
    if(!isDead())
      onNoHealth(true, HS_NoSound);
    }
  }

namespace {

void restoreNpcStatIfPresent(int32_t& dst, int32_t value) noexcept {
  if(value != Npc::PersistentStats::Missing)
    dst = std::max(0, value);
}

} // namespace

void Npc::restorePersistentStats(const PersistentStats& state) {
  if(mmoAuthorityGate.rejectLocalGameplay(
         Mmo::ClientPresentation::NpcLocalGameplayEntryPoint::AttributeMutation)) {
    return;
  }
  restoreNpcStatIfPresent(hnpc->level, state.level);
  restoreNpcStatIfPresent(hnpc->exp, state.experience);
  restoreNpcStatIfPresent(hnpc->exp_next, state.experienceNext);
  restoreNpcStatIfPresent(hnpc->lp, state.learningPoints);

  if(state.guild != PersistentStats::Missing)
    hnpc->guild = state.guild;
  if(state.trueGuild != PersistentStats::Missing)
    setTrueGuild(state.trueGuild);

  restoreNpcStatIfPresent(hnpc->attribute[ATR_HITPOINTSMAX], state.healthMax);
  restoreNpcStatIfPresent(hnpc->attribute[ATR_MANAMAX], state.manaMax);
  restoreNpcStatIfPresent(hnpc->attribute[ATR_STRENGTH], state.strength);
  restoreNpcStatIfPresent(hnpc->attribute[ATR_DEXTERITY], state.dexterity);

  if(state.healthCurrent != PersistentStats::Missing) {
    hnpc->attribute[ATR_HITPOINTS] = std::max(0, state.healthCurrent);
    if(hnpc->attribute[ATR_HITPOINTSMAX] > 0)
      hnpc->attribute[ATR_HITPOINTS] = std::min(hnpc->attribute[ATR_HITPOINTS], hnpc->attribute[ATR_HITPOINTSMAX]);
    }
  if(state.manaCurrent != PersistentStats::Missing) {
    hnpc->attribute[ATR_MANA] = std::max(0, state.manaCurrent);
    if(hnpc->attribute[ATR_MANAMAX] > 0)
      hnpc->attribute[ATR_MANA] = std::min(hnpc->attribute[ATR_MANA], hnpc->attribute[ATR_MANAMAX]);
    }
}

void Npc::restorePersistentLifecycle(int32_t healthCurrent, int32_t healthMax, bool dead) {
  if(mmoAuthorityGate.rejectLocalGameplay(
         Mmo::ClientPresentation::NpcLocalGameplayEntryPoint::AttributeMutation)) {
    return;
  }
  if(healthMax >= 0)
    hnpc->attribute[ATR_HITPOINTSMAX] = std::max(0, healthMax);
  if(healthCurrent >= 0) {
    const int32_t maxHp = std::max(0, hnpc->attribute[ATR_HITPOINTSMAX]);
    hnpc->attribute[ATR_HITPOINTS] = maxHp > 0 ? std::clamp(healthCurrent, 0, maxHp) : std::max(0, healthCurrent);
    }

  if(dead || hnpc->attribute[ATR_HITPOINTS] <= 0) {
    hnpc->attribute[ATR_HITPOINTS] = 0;
    if(!isDead())
      onNoHealth(true, HS_NoSound);
    return;
    }

  if(!isDead())
    physic.setEnable(true);
}

void Npc::restorePersistentInventory(const std::vector<PersistentInventoryItem>& next) {
  invent.resetForPersistence(*this);
  for(const auto& item : next) {
    if(item.instanceSymbol==size_t(-1) || item.count==0)
      continue;
    invent.addItem(item.instanceSymbol, item.count, owner);
    }
  for(const auto& item : next) {
    if(!item.equipped || item.instanceSymbol==size_t(-1))
      continue;
    invent.equip(item.instanceSymbol, *this, true);
    }
  invent.updateView(*this);
  }

void Npc::save(Serialize &fout, size_t id, std::string_view directory) {
  fout.setEntry("worlds/",fout.worldName(),directory,id,"/data");
  fout.write(*hnpc);
  fout.write(npcPersistentId);
  fout.write(body,head,vHead,vTeeth,bdColor,vColor,bdFatness);
  fout.write(x,y,z,angle,sz);
  fout.write(wlkMode,trGuild,talentsSk,talentsVl,refuseTalkMilis);
  fout.write(permAttitude,tmpAttitude);
  fout.write(perceptionTime,perceptionNextTime);
  for(auto& i:perception)
    fout.write(i.func);

  // extra state
  fout.write(lastHitType,lastHitSpell);
  if(currentSpellCast<uint32_t(-1))
    fout.write(uint32_t(currentSpellCast)); else
    fout.write(uint32_t(-1));
  fout.write(uint8_t(castLevel),castNextTime,manaInvested,aiExpectedInvest);
  fout.write(spellInfo);

  saveTrState(fout);
  saveAiState(fout);

  fout.write(currentInteract,currentOther,currentVictim);
  fout.write(currentLookAt,currentLookAtNpc,currentTarget,nearestEnemy);

  go2.save(fout);
  fout.write(currentFp,currentFpLock);
  wayPath.save(fout);

  mvAlgo.save(fout);
  fghAlgo.save(fout);
  fout.write(lastEventTime,angleY,runAng);
  fout.write(invTorch);
  fout.write(isUsingTorch());

  Vec3 phyPos = physic.position();
  fout.write(phyPos);

  fout.setEntry("worlds/",fout.worldName(),directory,id,"/visual");
  visual.save(fout,*this);

  fout.setEntry("worlds/",fout.worldName(),directory,id,"/inventory");
  if(!invent.isEmpty() || id==size_t(-1))
    invent.save(fout);
  }

void Npc::load(Serialize &fin, size_t id, std::string_view directory) {
  fin.setEntry("worlds/",fin.worldName(),directory,id,"/data");

  hnpc = std::make_shared<zenkit::INpc>();
  hnpc->user_ptr        = this;
  fin.readNpc(owner.script().getVm(), hnpc);
  if(fin.version()>=56)
    fin.read(npcPersistentId);
  else
    npcPersistentId = uint32_t(id);
  fin.read(body,head,vHead,vTeeth,bdColor,vColor,bdFatness);

  auto* sym = owner.script().findSymbol(hnpc->symbol_index());
  if (sym != nullptr)
    sym->set_instance(hnpc);

  fin.read(x,y,z,angle,sz);
  fin.read(wlkMode,trGuild,talentsSk,talentsVl,refuseTalkMilis);
  durtyTranform = TR_Pos|TR_Rot|TR_Scale;
  if(fin.version()<55)
    angle -= 90;

  fin.read(permAttitude,tmpAttitude);
  fin.read(perceptionTime,perceptionNextTime);
  for(auto& i:perception)
    fin.read(i.func);

  //if(owner.id)

  // extra state
  fin.read(lastHitType,lastHitSpell);
  {
  uint32_t currentSpellCastU32 = uint32_t(-1);
  fin.read(currentSpellCastU32);
  currentSpellCast = (currentSpellCastU32==uint32_t(-1) ? size_t(-1) : currentSpellCastU32);
  }
  fin.read(reinterpret_cast<uint8_t&>(castLevel),castNextTime);
  if(fin.version()>44)
    fin.read(manaInvested,aiExpectedInvest);
  fin.read(spellInfo);
  loadTrState(fin);
  loadAiState(fin);

  fin.read(currentInteract,currentOther,currentVictim);
  if(fin.version()>=42)
    fin.read(currentLookAt);
  fin.read(currentLookAtNpc,currentTarget,nearestEnemy);

  go2.load(fin);
  fin.read(currentFp,currentFpLock);
  wayPath.load(fin);

  mvAlgo.load(fin);
  fghAlgo.load(fin);
  fin.read(lastEventTime,angleY,runAng);

  bool isUsingTorch = false;
  if(fin.version()>36) {
    fin.read(invTorch);
    fin.read(isUsingTorch);
    }

  Vec3 phyPos = {};
  fin.read(phyPos);

  fin.setEntry("worlds/",fin.worldName(),directory,id,"/visual");
  visual.load(fin,*this);
  physic.setPosition(phyPos);

  setVisualBody(vHead,vTeeth,vColor,bdColor,body,head);

  if(fin.setEntry("worlds/",fin.worldName(),directory,id,"/inventory"))
    invent.load(fin,*this);

  // post-alignment
  updateTransform();
  if(isUsingTorch)
    visual.setTorch(true,owner);
  if(isDead())
    physic.setEnable(false);

  //auto x = hnpc->name;
  //append_unique("logs/npcs_load2_save.txt", x->data(), hnpc->id, phyPos.x, phyPos.y, phyPos.z);

  }

void Npc::postValidate() {
  if(currentInteract!=nullptr && !currentInteract->isAttached(*this))
    currentInteract = nullptr;
  }

void Npc::saveAiState(Serialize& fout) const {
  fout.write(aniWaitTime,waitTime,faiWaitTime,outWaitTime);
  fout.write(uint8_t(aiPolicy));
  fout.write(aiState.funcIni,aiState.funcLoop,aiState.funcEnd,aiState.sTime,aiState.eTime,aiState.started,aiState.loopNextTime);
  fout.write(aiPrevState);

  aiQueue.save(fout);
  aiQueueOverlay.save(fout);

  fout.write(uint32_t(routines.size()));
  for(auto& i:routines) {
    fout.write(i.start,i.end,i.callback,i.point,i.fallbackName);
    }
  }

void Npc::loadAiState(Serialize& fin) {
  fin.read(aniWaitTime);
  fin.read(waitTime,faiWaitTime);
  fin.read(outWaitTime);
  fin.read(reinterpret_cast<uint8_t&>(aiPolicy));
  fin.read(aiState.funcIni,aiState.funcLoop,aiState.funcEnd,aiState.sTime,aiState.eTime,aiState.started,aiState.loopNextTime);
  fin.read(aiPrevState);

#ifndef NDEBUG
  if(auto s = owner.script().findSymbol(aiState.funcIni.ptr)) {
    aiState.hint = s->name().c_str();
    }
#endif

  aiQueue.load(fin);
  aiQueueOverlay.load(fin);

  uint32_t size=0;
  fin.read(size);
  routines.resize(size);
  for(auto& i:routines) {
    fin.read(i.start,i.end,i.callback,i.point);
    if(fin.version()>51)
      fin.read(i.fallbackName);
    }
  }

void Npc::saveTrState(Serialize& fout) const {
  if(transformSpl!=nullptr) {
    fout.write(true);
    transformSpl->save(fout);
    } else {
    fout.write(false);
    }
  }

void Npc::loadTrState(Serialize& fin) {
  bool hasTr = false;
  fin.read(hasTr);
  if(hasTr)
    transformSpl.reset(new TransformBack(*this, owner.script().getVm(), fin));
  }

void Npc::transformBack() {
  if(transformSpl==nullptr)
    return;
  transformSpl->undo(*this);
  setVisual(transformSpl->skeleton);
  setVisualBody(vHead,vTeeth,vColor,bdColor,body,head);
  closeWeapon(true);

  // invalidate tallent overlays
  for(size_t i=0; i<TALENT_MAX_G2; ++i)
    setTalentSkill(Talent(i),talentsSk[i]);

  invent.updateView(*this);
  transformSpl.reset();
  }
