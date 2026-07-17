#include "gamesession.h"
#include "savegameheader.h"
#if OPENGOTHIC_MMO_SQLITE_TOOLING
#include "../../tools/mmo/mmoruntimesqlite.h"
#endif
#include "mmosemantichooks.h"
#include "mmoclientbridge.h"
#include "mmoclientpresentationcatalog.h"
#include "mmoserverpresentationbatchconsumer.h"
#include "mmorestoresnapshot.h"

#include <Tempest/Log>
#include <Tempest/MemReader>
#include <Tempest/MemWriter>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <set>
#include <chrono>
#include <charconv>
#include <thread>
#include <limits>
#include <iterator>
#include <functional>
#include <optional>
#include <utility>
#include <type_traits>

#include "utils/string_frm.h"
#include "worldstatestorage.h"
#include "world/objects/npc.h"
#include "world/objects/item.h"
#include "world/objects/interactive.h"
#include "world/world.h"
#include "world/waypoint.h"
#include "sound/soundfx.h"
#include "serialize.h"
#include "camera.h"
#include "gothic.h"
#include "commandline.h"

using namespace Tempest;

std::vector<GameScript::DlgChoice> GameSession::updateDialog(const GameScript::DlgChoice &dlg, Npc& player, Npc& npc) {
  auto ret = vm->updateDialog(dlg,player,npc);
#if OPENGOTHIC_MMO_SQLITE_TOOLING
  if(mmoSqlite!=nullptr) {
    mmoSqlite->recordDialogSelection(*this, player, npc, dlg, "update");
    mmoSqlite->recordDialogChoices(*this, player, npc, ret, "subchoices", false);
    }
#endif
  return ret;
  }

void GameSession::dialogExec(const GameScript::DlgChoice &dlg, Npc& player, Npc& npc) {
  markMmoServerSnapshotStoryDirty();
#if OPENGOTHIC_MMO_SQLITE_TOOLING
  if(mmoSqlite!=nullptr)
    mmoSqlite->recordDialogSelection(*this, player, npc, dlg, "exec");
#endif
  return vm->exec(dlg,player,npc);
  }

void GameSession::recordDialogChoices(Npc& player, Npc& npc,
                                      const std::vector<GameScript::DlgChoice>& choices,
                                      std::string_view phase, bool includeImportant) {
#if OPENGOTHIC_MMO_SQLITE_TOOLING
  if(mmoSqlite!=nullptr)
    mmoSqlite->recordDialogChoices(*this, player, npc, choices, phase, includeImportant);
#endif
  }

void GameSession::recordMmoChapterIntro(std::string_view title, std::string_view subtitle,
                                        std::string_view image, std::string_view sound, int time) {
#if OPENGOTHIC_MMO_SQLITE_TOOLING
  if(mmoSqlite!=nullptr)
    mmoSqlite->recordChapterIntro(*this, title, subtitle, image, sound, time);
#endif
  }

std::string_view GameSession::messageFromSvm(std::string_view id, int voice) const {
  if(!wrld)
    return "";
  return vm->messageFromSvm(id,voice);
  }

std::string_view GameSession::messageByName(std::string_view id) const {
  if(!wrld)
    return "";
  return vm->messageByName(id);
  }

uint32_t GameSession::messageTime(std::string_view id) const {
  if(!wrld)
    return 0;
  return vm->messageTime(id);
  }

AiOuputPipe *GameSession::openDlgOuput(Npc &player, Npc &npc) {
  AiOuputPipe* ret=nullptr;
  Gothic::inst().openDialogPipe(player, npc, ret);
  return ret;
  }

bool GameSession::isNpcInDialog(const Npc& npc) const {
  return Gothic::inst().isNpcInDialog(npc);
  }

bool GameSession::isInDialog() const {
  return Gothic::inst().isInDialog();
  }
