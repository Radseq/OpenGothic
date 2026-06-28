#include "commandline.h"

#include <Tempest/Log>
#include <Tempest/TextCodec>
#include <cstring>
#include <cassert>

#if defined(__APPLE__)
#include <filesystem>
#endif

#include <algorithm>

#include "utils/installdetect.h"
#include "utils/fileutil.h"
#include "utils/string_frm.h"

using namespace Tempest;
using namespace FileUtil;

static CommandLine* instance = nullptr;

static const char16_t* toString(ScriptLang lang) {
  switch(lang) {
    case ScriptLang::EN: return u"Scripts_EN";
    case ScriptLang::DE: return u"Scripts_DE";
    case ScriptLang::PL: return u"Scripts_PL";
    case ScriptLang::RU: return u"Scripts_RU";
    case ScriptLang::FR: return u"Scripts_FR";
    case ScriptLang::ES: return u"Scripts_ES";
    case ScriptLang::IT: return u"Scripts_IT";
    case ScriptLang::CZ: return u"Scripts_CZ";
    case ScriptLang::NONE:
      break;
    }
  return u"Scripts";
  }

static bool boolArg(std::string_view v) {
  return std::string_view(v)!="0" && std::string_view(v)!="false";
  }

CommandLine::CommandLine(int argc, const char** argv) {
  instance = this;
  if(argc<1)
    return;

  std::string_view mod;
  for(int i=1;i<argc;++i) {
    std::string_view arg = argv[i];
    if(arg.find("-game:")==0) {
      if(!mod.empty())
        Log::e("-game specified twice");
      mod = arg.substr(6);
      }
    else if(arg=="-g") {
      ++i;
      if(i<argc)
        gpath.assign(argv[i],argv[i]+std::strlen(argv[i]));
      }
    else if(arg=="-devmode") {
      // http://www.gothic-library.ru/publ/marvin/1-1-0-547
      devmode = true;
      }
    else if(arg=="-save") {
      ++i;
      if(i<argc){
        if(std::strcmp(argv[i],"q")==0) {
          saveDef = "save_slot_0.sav";
          } else {
          saveDef = string_frm("save_slot_",argv[i],".sav");
          }
        }
      }
    else if(arg=="-w") {
      ++i;
      if(i<argc)
        wrldDef = argv[i];
      }
    else if(arg=="-dump-initial-world") {
      ++i;
      if(i<argc)
        dumpInitial = argv[i];
      }
    else if(arg=="-dump-save-world") {
      ++i;
      if(i<argc)
        dumpSave = argv[i];
      }
    else if(arg=="-mmo-sqlite") {
      // Enables local MMO persistence. The path identifies the SQLite database
      // opened after the world loads; it is used for capture and DB restore.
      ++i;
      if(i<argc)
        mmoSqliteDb = argv[i];
      }
    else if(arg=="-mmo-sqlite-interval-ms") {
      // Sets the minimum interval for incremental delta flushes. This does not
      // rebuild the canonical MMO projection; 250 ms prevents accidental I/O abuse.
      ++i;
      if(i<argc) {
        try {
          mmoSqliteInterval = std::max<uint64_t>(250, std::stoull(std::string(argv[i])));
          }
        catch(const std::exception&) {
          Log::i("failed to read -mmo-sqlite-interval-ms: \"", std::string(argv[i]), "\"");
          }
        }
      }
    else if(arg=="-mmo-sqlite-no-restore") {
      // Capture-only mode: writes the current session to SQLite but leaves the
      // world loaded from the regular save/New Game untouched by DB restore.
      mmoSqliteRestoreState = false;
      }
    else if(arg=="-mmo-sqlite-capture-baseline") {
      // Creates the immutable MMO world baseline from a deterministic New Game.
      // It is valid only for the first session of a fresh database, never a save.
      mmoSqliteCaptureBaselineState = true;
      }
    else if(arg=="-mmo-action-jsonl") {
      // Dev-only semantic action capture. The game thread only enqueues immutable
      // JSONL lines; final MMO architecture is still client -> server -> DB.
      ++i;
      if(i<argc)
        mmoActionJsonlPath = argv[i];
      }
    else if(arg=="-mmo-action-udp") {
      // Dev-only local server boundary. The game thread still only enqueues;
      // an async worker sends immutable JSONL envelopes to host:port over UDP.
      ++i;
      if(i<argc)
        mmoActionUdp = argv[i];
      }
    else if(arg=="-mmo-action-session-key") {
      ++i;
      if(i<argc)
        mmoActionSession = argv[i];
      }
    else if(arg=="-mmo-action-queue-capacity") {
      ++i;
      if(i<argc) {
        try {
          mmoActionQueueCap = std::max<uint64_t>(1, std::stoull(std::string(argv[i])));
          }
        catch(const std::exception&) {
          Log::i("failed to read -mmo-action-queue-capacity: \"", std::string(argv[i]), "\"");
          }
        }
      }
    else if(arg=="-mmo-action-strict-overflow") {
      mmoActionStrictOverflowState = true;
      }
    else if(arg=="-mmo-action-checkpoint-interval-ms") {
      // Step39 dev-only movement/checkpoint capture cadence. Zero disables
      // periodic checkpoint envelopes even when the semantic action sink is on.
      ++i;
      if(i<argc) {
        try {
          auto value = std::stoull(std::string(argv[i]));
          mmoActionCheckpointInterval = value == 0 ? 0 : std::max<uint64_t>(250, value);
          }
        catch(const std::exception&) {
          Log::i("failed to read -mmo-action-checkpoint-interval-ms: \"", std::string(argv[i]), "\"");
          }
        }
      }
    else if(arg=="-mmo-action-checkpoint-min-distance") {
      // Step39 v2: coalesce stationary checkpoints on the game side. The unit is
      // Gothic world units; zero keeps pure interval capture semantics.
      ++i;
      if(i<argc) {
        try {
          mmoActionCheckpointMinDistanceWorld = std::max(0.f, std::stof(std::string(argv[i])));
          }
        catch(const std::exception&) {
          Log::i("failed to read -mmo-action-checkpoint-min-distance: \"", std::string(argv[i]), "\"");
          }
        }
      }
    else if(arg=="-mmo-action-checkpoint-min-yaw-deg") {
      ++i;
      if(i<argc) {
        try {
          mmoActionCheckpointMinYaw = std::max(0.f, std::stof(std::string(argv[i])));
          }
        catch(const std::exception&) {
          Log::i("failed to read -mmo-action-checkpoint-min-yaw-deg: \"", std::string(argv[i]), "\"");
          }
        }
      }
    else if(arg=="-mmo-action-checkpoint-force-interval-ms") {
      // Optional keepalive interval. It emits even when position/yaw/stats are
      // unchanged, but never more often than -mmo-action-checkpoint-interval-ms.
      ++i;
      if(i<argc) {
        try {
          auto value = std::stoull(std::string(argv[i]));
          mmoActionCheckpointForceInterval = value == 0 ? 0 : std::max<uint64_t>(250, value);
          }
        catch(const std::exception&) {
          Log::i("failed to read -mmo-action-checkpoint-force-interval-ms: \"", std::string(argv[i]), "\"");
          }
        }
      }
    else if(arg=="-mmo-action-movement-proposal-interval-ms") {
      // Step41 dev-only movement proposal capture. This is not a DB write; it
      // produces client intent/proposal envelopes for a server-side validator.
      ++i;
      if(i<argc) {
        try {
          auto value = std::stoull(std::string(argv[i]));
          mmoActionMovementProposalInterval = value == 0 ? 0 : std::max<uint64_t>(50, value);
          }
        catch(const std::exception&) {
          Log::i("failed to read -mmo-action-movement-proposal-interval-ms: \"", std::string(argv[i]), "\"");
          }
        }
      }
    else if(arg=="-mmo-action-movement-proposal-min-distance") {
      ++i;
      if(i<argc) {
        try {
          mmoActionMovementProposalMinDistanceWorld = std::max(0.f, std::stof(std::string(argv[i])));
          }
        catch(const std::exception&) {
          Log::i("failed to read -mmo-action-movement-proposal-min-distance: \"", std::string(argv[i]), "\"");
          }
        }
      }
    else if(arg=="-mmo-action-movement-proposal-min-yaw-deg") {
      ++i;
      if(i<argc) {
        try {
          mmoActionMovementProposalMinYaw = std::max(0.f, std::stof(std::string(argv[i])));
          }
        catch(const std::exception&) {
          Log::i("failed to read -mmo-action-movement-proposal-min-yaw-deg: \"", std::string(argv[i]), "\"");
          }
        }
      }
    else if(arg=="-window") {
      isWindow = true;
      }
    else if(arg=="-nomenu") {
      noMenu = true;
      }
    else if(arg=="-benchmark") {
      isBenchmark = Benchmark::Normal;
      if(i+1<argc && argv[i+1][0]!='-') {
        ++i;
        isBenchmark = std::string_view(argv[i])=="ci" ? Benchmark::CiTooling : isBenchmark;
        }
      }
    else if(arg=="-g1") {
      forceG1 = true;
      }
    else if(arg=="-g2c") {
      forceG2 = true;
      }
    else if(arg=="-g2") {
      forceG2NR = true;
      }
    else if(arg=="-dx12") {
      graphics = GraphicBackend::DirectX12;
      }
    else if(arg=="-validation" || arg=="-v") {
      isDebug  = true;
      }
    else if(arg=="-rt") {
      ++i;
      if(i<argc)
        isRQuery = boolArg(argv[i]);
      }
    else if(arg=="-aa") {
      ++i;
      if(i<argc) {
        try {
          aaPresetId = uint32_t(std::stoul(std::string(argv[i])));
          aaPresetId = std::clamp(aaPresetId, 0u, uint32_t(AaPreset::PRESETS_COUNT)-1u);
          }
        catch (const std::exception& e) {
          Log::i("failed to read cmaa2 preset: \"", std::string(argv[i]), "\"");
          }
        }
      }
    else if(arg=="-gi") {
      ++i;
      if(i<argc)
        isGi = boolArg(argv[i]);
      }
    else if(arg=="-ms") {
      ++i;
      if(i<argc)
        isMeshSh = boolArg(argv[i]);
      }
    else if(arg=="-bl") {
      // not to document - debug only
      ++i;
      if(i<argc)
        isBindlessSh = boolArg(argv[i]);
      }
    else if(arg=="-vsm") {
      // not to document - debug only
      ++i;
      if(i<argc)
        isVsm = boolArg(argv[i]);
      }
    else if(arg=="-rtsm") {
      // not to document - debug only
      ++i;
      if(i<argc)
        isRtSm = boolArg(argv[i]);
      }
    else {
      Log::i("unreacognized commandline option: \"", arg, "\"");
      }
    }

  if(gpath.empty()) {
    InstallDetect inst;
    gpath = inst.detectG2();
#if defined(__APPLE__)
    if(!gpath.empty() && gpath==inst.applicationSupportDirectory()) {
      std::filesystem::current_path(gpath);
      }
#endif
    }

  for(auto& i:gpath)
    if(i=='\\')
      i='/';

  if(gpath.size()>0 && gpath.back()!='/')
    gpath.push_back('/');

  gscript   = nestedPath({u"_work",u"Data",u"Scripts",   u"_compiled"},Dir::FT_Dir);
  gcutscene = nestedPath({u"_work",u"Data",u"Scripts",   u"content",u"CUTSCENE"},Dir::FT_Dir);

  gmod    = TextCodec::toUtf16(mod);
  if(!gmod.empty())
    gmod = nestedPath({u"system",gmod.c_str()},Dir::FT_File);

  if(!validateGothicPath()) {
    if(gpath.empty()) {
      Log::e("Gothic path is not provided. Please use command line argument -g <path>");
      } else {
      Log::e("Invalid gothic path: \"",TextCodec::toUtf8(gpath),"\"");
      }
    throw GothicNotFoundException("gothic not found!"); // TODO: user-friendly message-box
    }
  }

const CommandLine& CommandLine::inst() {
  assert(instance!=nullptr);
  return *instance;
  }

CommandLine::GraphicBackend CommandLine::graphicsApi() const {
  return graphics;
  }

std::u16string_view CommandLine::rootPath() const {
  return gpath;
  }

std::u16string CommandLine::scriptPath() const {
  return gscript;
  }

std::u16string CommandLine::scriptPath(ScriptLang lang) const {
  const char16_t* scripts = toString(lang);
  return nestedPath({u"_work",u"Data",scripts,u"_compiled"},Dir::FT_Dir);
  }

std::u16string CommandLine::cutscenePath() const {
  return gcutscene;
  }

std::u16string CommandLine::cutscenePath(ScriptLang lang) const {
  const char16_t* scripts = toString(lang);
  return nestedPath({u"_work",u"Data",scripts},Dir::FT_Dir);
  }

std::u16string CommandLine::nestedPath(const std::initializer_list<const char16_t*>& name, Tempest::Dir::FileType type) const {
  return FileUtil::nestedPath(gpath, name, type);
  }

bool CommandLine::validateGothicPath() const {
  if(gpath.empty())
    return false;
  if(!FileUtil::exists(gscript))
    return false;
  if(!FileUtil::exists(nestedPath({u"Data"},Dir::FT_Dir)))
    return false;
  if(!FileUtil::exists(nestedPath({u"_work",u"Data"},Dir::FT_Dir)))
    return false;
  return true;
  }






