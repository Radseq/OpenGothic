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


void GameScript::snd_play(std::string_view fileS) {
  if(aiProcessPolicy>=NpcProcessPolicy::AiFar2)
    return;

  std::string file {fileS};
  for(auto& c:file)
    c = char(std::toupper(c));
  Gothic::inst().emitGlobalSound(file);
  }

void GameScript::snd_play3d(std::shared_ptr<zenkit::INpc> npcRef, std::string_view fileS) {
  if(aiProcessPolicy>=NpcProcessPolicy::AiFar2)
    return;

  std::string file {fileS};
  Npc*        npc  = findNpc(npcRef);
  if(npc==nullptr)
    return;
  for(auto& c:file)
    c = char(std::toupper(c));
  auto sfx = ::Sound(*owner.world(),::Sound::T_3D,file,npc->centerPosition(),0.f,false);
  sfx.play();
  }
