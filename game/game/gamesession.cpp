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

// rate 14.5 to 1
const uint64_t GameSession::multTime=14500;
const uint64_t GameSession::divTime =1000;

const VersionInfo& GameSession::version() const {
  return Gothic::inst().version();
  }

WorldView *GameSession::view() const {
  if(wrld)
    return wrld->view();
  return nullptr;
  }

Tempest::SoundEffect GameSession::loadSound(const Tempest::Sound &raw) {
  try {
    return sound.load(raw);
    }
  catch(std::bad_alloc&) {
    Tempest::Log::d("Exceeding OpenAL source limit");
    return Tempest::SoundEffect();
    }
  }

Tempest::SoundEffect GameSession::loadSound(const SoundFx &fx, bool& looped) {
  try {
    return fx.load(sound,looped);
    }
  catch(std::bad_alloc&) {
    Tempest::Log::d("Exceeding OpenAL source limit");
    return Tempest::SoundEffect();
    }
  }

Npc* GameSession::player() {
  if(wrld)
    return wrld->player();
  return nullptr;
  }
