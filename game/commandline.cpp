#include "commandline.h"

#include <Tempest/Log>
#include <Tempest/TextCodec>
#include <cstring>
#include <cmath>
#include <cassert>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#if __has_include("../../shared/config/mmo_runtime_config.h")
#include "../../shared/config/mmo_runtime_config.h"
#define OPENGOTHIC_MMO_RUNTIME_CONFIG 1
#else
#define OPENGOTHIC_MMO_RUNTIME_CONFIG 0
#endif

#if defined(__APPLE__)
#include <filesystem>
#endif

#include <algorithm>

#include "utils/installdetect.h"
#include "utils/fileutil.h"
#include "utils/string_frm.h"

#ifndef OPENGOTHIC_MMO_SQLITE_TOOLING
#define OPENGOTHIC_MMO_SQLITE_TOOLING 0
#endif

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

static uint64_t parseUint64Arg(const char* value, const uint64_t minimum) {
  const std::string text(value);
  std::size_t consumed = 0;
  const auto parsed = std::stoull(text, &consumed, 0);
  if(consumed != text.size() || parsed < minimum)
    throw std::out_of_range("command-line uint64 value is outside its valid range");
  return parsed;
  }

static uint32_t parseUint32Arg(const char* value, const uint32_t minimum) {
  const auto parsed = parseUint64Arg(value, minimum);
  if(parsed > std::numeric_limits<uint32_t>::max())
    throw std::out_of_range("command-line uint32 value is outside its valid range");
  return static_cast<uint32_t>(parsed);
  }

static bool parseConfigBool(std::string_view key, std::string_view value) {
  if(value=="true" || value=="1" || value=="yes" || value=="on")
    return true;
  if(value=="false" || value=="0" || value=="no" || value=="off")
    return false;
  throw std::invalid_argument(std::string(key)+" expects true/false");
  }

static uint64_t parseConfigUint64(std::string_view key, std::string_view value, uint64_t minimum) {
  const std::string text(value);
  try {
    std::size_t consumed = 0;
    const auto parsed = std::stoull(text,&consumed,10);
    if(consumed!=text.size() || parsed<minimum)
      throw std::invalid_argument("invalid");
    return parsed;
    }
  catch(const std::exception&) {
    throw std::invalid_argument(std::string(key)+" has an invalid unsigned integer value");
    }
  }

static uint32_t parseConfigUint32(std::string_view key, std::string_view value, uint32_t minimum) {
  const auto parsed = parseConfigUint64(key,value,minimum);
  if(parsed>std::numeric_limits<uint32_t>::max())
    throw std::invalid_argument(std::string(key)+" exceeds uint32 range");
  return static_cast<uint32_t>(parsed);
  }

static float parseConfigFloat(std::string_view key, std::string_view value, float minimum) {
  const std::string text(value);
  try {
    std::size_t consumed = 0;
    const auto parsed = std::stof(text,&consumed);
    if(consumed!=text.size() || !std::isfinite(parsed) || parsed<minimum)
      throw std::invalid_argument("invalid");
    return parsed;
    }
  catch(const std::exception&) {
    throw std::invalid_argument(std::string(key)+" has an invalid floating-point value");
    }
  }

CommandLine::CommandLine(int argc, const char** argv) {
  instance = this;
  if(argc<1)
    return;

  std::string_view mod;
  bool mmoSqliteOptionRequested = false;
  std::string runtimeConfigPath;
  std::string runtimeProfile;
#if OPENGOTHIC_MMO_RUNTIME_CONFIG
  std::vector<Mmo::RuntimeConfig::Entry> runtimeConfigEntries;
#endif

  for(int i=1;i<argc;++i) {
    const std::string_view arg = argv[i];
    if(arg!="-config" && arg!="-profile")
      continue;
    if(i+1>=argc)
      throw std::invalid_argument(std::string(arg)+" requires value");
    const std::string value = argv[++i];
    if(arg=="-config") {
      if(!runtimeConfigPath.empty())
        throw std::invalid_argument("-config may be specified only once");
      runtimeConfigPath = value;
      }
    else {
      runtimeProfile = value;
      }
    }

#if OPENGOTHIC_MMO_RUNTIME_CONFIG
  if(!runtimeConfigPath.empty())
    runtimeConfigEntries = Mmo::RuntimeConfig::load(runtimeConfigPath);
  if(runtimeProfile.empty()) {
    const auto configured = Mmo::RuntimeConfig::findLast(runtimeConfigEntries, "profile");
    if(!configured.empty())
      runtimeProfile = configured;
    }
#else
  if(!runtimeConfigPath.empty())
    throw std::invalid_argument("-config requires the OpenGothic MMO workspace build");
#endif

  if(runtimeProfile.empty())
    runtimeProfile = "singleplayer";
  applyRuntimeProfile(runtimeProfile);
#if OPENGOTHIC_MMO_RUNTIME_CONFIG
  for(const auto& entry:runtimeConfigEntries) {
    if(entry.key.starts_with("mmo.sqlite."))
      mmoSqliteOptionRequested = true;
    applyRuntimeConfig(entry.key,entry.value);
    }
#endif

  for(int i=1;i<argc;++i) {
    std::string_view arg = argv[i];
    if(arg=="-config" || arg=="-profile") {
      ++i;
      continue;
      }
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
    else if(arg=="-native-telemetry") {
      ++i;
      if(i<argc)
        nativeTelemetryPath = argv[i];
      }
    else if(arg=="-mmo-client-server") {
      // Explicit opt-in for a server-bound client. Old single-player behavior
      // is unchanged unless this flag/profile is present. The optional value uses
      // the same host:port syntax as the mmo.server config key.
      mmoClientUsesServerState = true;
      if(i + 1 < argc && argv[i + 1][0] != '-') {
        ++i;
        mmoServerEndpointValue = argv[i];
        mmoActionUdp = mmoServerEndpointValue;
        }
      }
    else if(arg=="-mmo-process-gate-report") {
      ++i;
      if(i<argc && argv[i][0] != '\0') {
        mmoClientUsesServerState = true;
        mmoProcessGateReportPath = argv[i];
        }
      }
    else if(arg=="-mmo-process-gate-client-id") {
      ++i;
      if(i<argc && argv[i][0] != '\0')
        mmoProcessGateClientIdValue = argv[i];
      }
    else if(arg=="-mmo-process-gate-manifest-id") {
      ++i;
      if(i<argc) {
        try {
          mmoProcessGateContentManifestIdValue = parseUint64Arg(argv[i], 1);
          }
        catch(const std::exception&) {
          Log::e("failed to read -mmo-process-gate-manifest-id: ", argv[i]);
          }
        }
      }
    else if(arg=="-mmo-process-gate-archetype") {
      ++i;
      if(i<argc) {
        try {
          mmoProcessGateArchetypeIdValue = parseUint32Arg(argv[i], 1);
          }
        catch(const std::exception&) {
          Log::e("failed to read -mmo-process-gate-archetype: ", argv[i]);
          }
        }
      }
    else if(arg=="-mmo-process-gate-appearance") {
      ++i;
      if(i<argc) {
        try {
          mmoProcessGateAppearanceProfileIdValue = parseUint32Arg(argv[i], 0);
          }
        catch(const std::exception&) {
          Log::e("failed to read -mmo-process-gate-appearance: ", argv[i]);
          }
        }
      }
    else if(arg=="-mmo-client-presentation-catalog") {
      ++i;
      if(i<argc && argv[i][0] != '\0')
        mmoClientPresentationCatalogPath = argv[i];
    }
    else if(arg=="-mmo-client-presentation-manifest-id") {
      ++i;
      if(i<argc) {
        try {
          mmoClientPresentationManifestIdValue = parseUint64Arg(argv[i], 1U);
        }
        catch(const std::exception&) {
          Log::e("failed to read -mmo-client-presentation-manifest-id: ", argv[i]);
        }
      }
    }
    else if(arg=="-mmo-action-session-key") {
      ++i;
      if(i<argc)
        mmoActionSession = argv[i];
      }
    else if(arg=="-mmo-character-key") {
      ++i;
      if(i<argc && argv[i][0] != '\0')
        mmoCharacterKeyValue = argv[i];
      }
    else if(arg=="-mmo-character-name") {
      ++i;
      if(i<argc && argv[i][0] != '\0')
        mmoCharacterDisplayNameValue = argv[i];
      }
    else if(arg=="-mmo-character-archetype") {
      ++i;
      if(i<argc) {
        try {
          const auto value = std::stoull(std::string(argv[i]));
          if(value == 0U || value > std::numeric_limits<uint32_t>::max())
            throw std::out_of_range("mmo character archetype");
          mmoCharacterArchetypeIdValue = static_cast<uint32_t>(value);
          }
        catch(const std::exception&) {
          Log::i("failed to read -mmo-character-archetype: \"", std::string(argv[i]), "\"");
          }
        }
      }
    else if(arg=="-mmo-character-appearance") {
      ++i;
      if(i<argc) {
        try {
          const auto value = std::stoull(std::string(argv[i]));
          if(value > std::numeric_limits<uint32_t>::max())
            throw std::out_of_range("mmo character appearance");
          mmoCharacterAppearanceProfileIdValue = static_cast<uint32_t>(value);
          }
        catch(const std::exception&) {
          Log::i("failed to read -mmo-character-appearance: \"", std::string(argv[i]), "\"");
          }
        }
      }
    else if(arg=="-mmo-content-manifest-id") {
      ++i;
      if(i<argc) {
        try {
          mmoContentManifestIdValue = std::max<uint64_t>(1U, std::stoull(std::string(argv[i])));
          }
        catch(const std::exception&) {
          Log::i("failed to read -mmo-content-manifest-id: \"", std::string(argv[i]), "\"");
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
    else if(arg.starts_with("-mmo-")) {
      throw std::invalid_argument(
          "removed or unknown MMO command-line option: "+std::string(arg)+
          "; use -profile or -config for runtime tuning");
      }
    else {
      Log::i("unreacognized commandline option: \"", arg, "\"");
      }
    }

  if(mmoClientUsesServerState) {
    if(mmoServerEndpointValue.empty())
      mmoServerEndpointValue = mmoActionUdp;
    if(mmoActionUdp.empty()) {
      Log::e("MMO server-bound mode has no endpoint; set mmo.server in config or pass -mmo-client-server HOST:PORT");
      }
    else {
      Log::i("MMO server-bound client mode enabled: ", mmoActionUdp);
      Log::i("MMO graphical client contract: opengothic-mmo-graphical-v1");
      }

    // Conservative server-mode defaults. They apply whenever the MMO profile
    // is active and only fill values that the config/CLI did not override.
    if(mmoActionQueueCap < 8192)
      mmoActionQueueCap = 8192;
    if(mmoActionMovementProposalInterval == 0)
      mmoActionMovementProposalInterval = 100;
    if(mmoActionMovementProposalMinDistanceWorld <= 0.f)
      mmoActionMovementProposalMinDistanceWorld = 25.f;
    if(mmoActionMovementProposalMinYaw <= 0.f)
      mmoActionMovementProposalMinYaw = 5.f;
    if(mmoActionCheckpointInterval == 0)
      mmoActionCheckpointInterval = 1000;
    if(mmoActionCheckpointMinDistanceWorld <= 0.f)
      mmoActionCheckpointMinDistanceWorld = 100.f;
    if(mmoActionCheckpointMinYaw <= 0.f)
      mmoActionCheckpointMinYaw = 15.f;
    if(mmoActionCheckpointForceInterval == 0)
      mmoActionCheckpointForceInterval = 5000;

    // In server-bound mode the local .sav file is only a compatibility/debug
    // cache. A DB-backed character should be able to enter through Load/Continue
    // without requiring a fake -save slot on the command line.
    mmoDbContinueWithoutNativeSaveState = true;
    Log::i("MMO DB continue without native save enabled by server-bound mode");
    if(mmoClientDialogObservationReceiptState)
      Log::i("MMO client dialog main-thread observation receipt enabled: no UI/audio apply");
    }

  if(!mmoClientPresentationCatalogPath.empty() &&
     mmoClientPresentationManifestIdValue == 0U) {
    throw std::invalid_argument(
        "-mmo-client-presentation-catalog requires "
        "-mmo-client-presentation-manifest-id");
  }
  if(mmoClientPresentationCatalogPath.empty() &&
     mmoClientPresentationManifestIdValue != 0U) {
    throw std::invalid_argument(
        "-mmo-client-presentation-manifest-id requires "
        "-mmo-client-presentation-catalog");
  }


  if(mmoSqliteOptionRequested && !OPENGOTHIC_MMO_SQLITE_TOOLING)
    throw std::invalid_argument("MMO SQLite options require OPENGOTHIC_MMO_ENABLE_SQLITE_TOOLING=ON");

  if(mmoDbContinueWithoutNativeSaveState && !mmoClientUsesServerState)
    Log::e("mmo.db-continue-without-native-save requires an MMO server profile/endpoint");

  if(mmoRequireDbSaveCheckpointRestoreState && !mmoClientUsesServerState)
    Log::e("mmo.require-db-save-checkpoint-restore requires an MMO server profile/endpoint");

  if(!mmoProcessGateReportPath.empty() && mmoActionUdp.empty())
    Log::e("-mmo-process-gate-report requires -mmo-client-server HOST:PORT");

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

void CommandLine::applyRuntimeProfile(std::string_view profile) {
  if(profile.empty() || profile=="singleplayer")
    return;
  if(profile=="mmo" || profile=="mmo-test") {
    mmoClientUsesServerState = true;
    if(mmoServerEndpointValue.empty())
      mmoServerEndpointValue = "127.0.0.1:29777";
    if(mmoActionUdp.empty())
      mmoActionUdp = mmoServerEndpointValue;
    mmoDbContinueWithoutNativeSaveState = true;
    if(profile=="mmo-test") {
      mmoClientDialogObservationReceiptState = true;
      mmoActionStrictOverflowState = true;
      }
    return;
    }
  throw std::invalid_argument(
      "unknown client profile: "+std::string(profile)+
      " (expected singleplayer, mmo or mmo-test)");
  }

void CommandLine::applyRuntimeConfig(std::string_view key, std::string_view value) {
  if(key=="profile")
    return;
  if(key=="gothic-path") {
    gpath.assign(value.begin(),value.end());
    }
  else if(key=="world") {
    wrldDef = value;
    }
  else if(key=="graphics.window") {
    isWindow = parseConfigBool(key,value);
    }
  else if(key=="graphics.validation") {
    isDebug = parseConfigBool(key,value);
    }
  else if(key=="graphics.backend") {
    if(value=="vulkan") graphics = GraphicBackend::Vulkan;
    else if(value=="dx12") graphics = GraphicBackend::DirectX12;
    else throw std::invalid_argument("graphics.backend expects vulkan or dx12");
    }
  else if(key=="graphics.ray-query") {
    isRQuery = parseConfigBool(key,value);
    }
  else if(key=="graphics.gi") {
    isGi = parseConfigBool(key,value);
    }
  else if(key=="graphics.mesh-shading") {
    isMeshSh = parseConfigBool(key,value);
    }
  else if(key=="graphics.aa-preset") {
    aaPresetId = std::clamp(
        parseConfigUint32(key,value,0U),0U,uint32_t(AaPreset::PRESETS_COUNT)-1U);
    }
  else if(key=="start-menu") {
    noMenu = !parseConfigBool(key,value);
    }
  else if(key=="mmo.server") {
    if(value.empty())
      throw std::invalid_argument("mmo.server cannot be empty");
    mmoClientUsesServerState = true;
    mmoServerEndpointValue = value;
    mmoActionUdp = value;
    }
  else if(key=="mmo.sqlite.path") {
    mmoSqliteDb = value;
    }
  else if(key=="mmo.sqlite.interval-ms") {
    mmoSqliteInterval = std::max<uint64_t>(250U,parseConfigUint64(key,value,1U));
    }
  else if(key=="mmo.sqlite.restore") {
    mmoSqliteRestoreState = parseConfigBool(key,value);
    }
  else if(key=="mmo.sqlite.capture-baseline") {
    mmoSqliteCaptureBaselineState = parseConfigBool(key,value);
    }
  else if(key=="mmo.sqlite.capture-pre-start-exit") {
    mmoSqliteCapturePreStartExitState = parseConfigBool(key,value);
    if(mmoSqliteCapturePreStartExitState) {
      mmoSqliteCaptureBaselineState = true;
      mmoSqliteRestoreState = false;
      }
    }
  else if(key=="mmo.action-jsonl") {
    mmoActionJsonlPath = value;
    }
  else if(key=="mmo.action-udp") {
    mmoActionUdp = value;
    }
  else if(key=="mmo.dialog-observation-receipt") {
    mmoClientDialogObservationReceiptState = parseConfigBool(key,value);
    }
  else if(key=="mmo.client-content-manifest-hash") {
    mmoClientUsesServerState = true;
    mmoClientContentManifestHashValue = value;
    }
  else if(key=="mmo.presentation-catalog") {
    mmoClientPresentationCatalogPath = value;
    }
  else if(key=="mmo.presentation-manifest-id") {
    mmoClientPresentationManifestIdValue = parseConfigUint64(key,value,1U);
    }
  else if(key=="mmo.process-gate.report") {
    mmoClientUsesServerState = true;
    mmoProcessGateReportPath = value;
    }
  else if(key=="mmo.process-gate.client-id") {
    mmoProcessGateClientIdValue = value;
    }
  else if(key=="mmo.process-gate.manifest-id") {
    mmoProcessGateContentManifestIdValue = parseConfigUint64(key,value,1U);
    }
  else if(key=="mmo.process-gate.archetype") {
    mmoProcessGateArchetypeIdValue = parseConfigUint32(key,value,1U);
    }
  else if(key=="mmo.process-gate.appearance") {
    mmoProcessGateAppearanceProfileIdValue = parseConfigUint32(key,value,0U);
    }
  else if(key=="mmo.process-gate.require-restart") {
    mmoProcessGateRequireRestartState = parseConfigBool(key,value);
    }
  else if(key=="mmo.db-continue-without-native-save") {
    mmoDbContinueWithoutNativeSaveState = parseConfigBool(key,value);
    }
  else if(key=="mmo.db-bootstrap-world") {
    mmoDbBootstrapWorldValue = value;
    }
  else if(key=="mmo.require-db-save-checkpoint-restore") {
    mmoRequireDbSaveCheckpointRestoreState = parseConfigBool(key,value);
    }
  else if(key=="mmo.action-session-key") {
    mmoActionSession = value;
    }
  else if(key=="mmo.character-key") {
    mmoCharacterKeyValue = value;
    }
  else if(key=="mmo.character-name") {
    mmoCharacterDisplayNameValue = value;
    }
  else if(key=="mmo.character-id") {
    mmoCharacterIdValue = parseConfigUint64(key,value,0U);
    }
  else if(key=="mmo.character-archetype") {
    mmoCharacterArchetypeIdValue = parseConfigUint32(key,value,1U);
    }
  else if(key=="mmo.character-appearance") {
    mmoCharacterAppearanceProfileIdValue = parseConfigUint32(key,value,0U);
    }
  else if(key=="mmo.content-manifest-id") {
    mmoContentManifestIdValue = parseConfigUint64(key,value,1U);
    }
  else if(key=="mmo.action-queue-capacity") {
    mmoActionQueueCap = parseConfigUint64(key,value,1U);
    }
  else if(key=="mmo.action-strict-overflow") {
    mmoActionStrictOverflowState = parseConfigBool(key,value);
    }
  else if(key=="mmo.checkpoint.interval-ms") {
    const auto parsed = parseConfigUint64(key,value,0U);
    mmoActionCheckpointInterval = parsed==0U ? 0U : std::max<uint64_t>(250U,parsed);
    }
  else if(key=="mmo.checkpoint.min-distance") {
    mmoActionCheckpointMinDistanceWorld = parseConfigFloat(key,value,0.f);
    }
  else if(key=="mmo.checkpoint.min-yaw-deg") {
    mmoActionCheckpointMinYaw = parseConfigFloat(key,value,0.f);
    }
  else if(key=="mmo.checkpoint.force-interval-ms") {
    const auto parsed = parseConfigUint64(key,value,0U);
    mmoActionCheckpointForceInterval = parsed==0U ? 0U : std::max<uint64_t>(250U,parsed);
    }
  else if(key=="mmo.movement.interval-ms") {
    const auto parsed = parseConfigUint64(key,value,0U);
    mmoActionMovementProposalInterval = parsed==0U ? 0U : std::max<uint64_t>(50U,parsed);
    }
  else if(key=="mmo.movement.min-distance") {
    mmoActionMovementProposalMinDistanceWorld = parseConfigFloat(key,value,0.f);
    }
  else if(key=="mmo.movement.min-yaw-deg") {
    mmoActionMovementProposalMinYaw = parseConfigFloat(key,value,0.f);
    }
  else {
    throw std::invalid_argument("unknown client config key: "+std::string(key));
    }
  }

const CommandLine& CommandLine::inst() {
  assert(instance!=nullptr);
  return *instance;
  }

void CommandLine::setMmoCharacterIdentity(std::string_view key, std::string_view displayName) const {
  mmoCharacterIdValue = 0;
  if(!key.empty())
    mmoCharacterKeyValue = key;
  if(!displayName.empty())
    mmoCharacterDisplayNameValue = displayName;
  else if(!key.empty())
    mmoCharacterDisplayNameValue = key;
}

void CommandLine::setMmoCharacterSelection(
    const uint64_t characterId,
    std::string_view key,
    std::string_view displayName) const {
  mmoCharacterIdValue = characterId;
  if(!key.empty())
    mmoCharacterKeyValue = key;
  if(!displayName.empty())
    mmoCharacterDisplayNameValue = displayName;
  else if(!key.empty())
    mmoCharacterDisplayNameValue = key;
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
