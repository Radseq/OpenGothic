#include "mmo_content_build_loader.h"

#include <zenkit/Archive.hh>
#include <zenkit/CutsceneLibrary.hh>
#include <zenkit/DaedalusScript.hh>
#include <zenkit/Error.hh>
#include <zenkit/Stream.hh>
#include <zenkit/World.hh>
#include <zenkit/vobs/Light.hh>
#include <zenkit/vobs/Misc.hh>
#include <zenkit/vobs/MovableObject.hh>
#include <zenkit/vobs/Sound.hh>
#include <zenkit/vobs/Trigger.hh>
#include <zenkit/vobs/VirtualObject.hh>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <optional>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace Mmo::ContentBuild {
namespace {

struct ZenEntity final {
  std::string worldName;
  std::string entityKind;
  std::string entityKey;
  std::string name;
  std::optional<double> posX;
  std::optional<double> posY;
  std::optional<double> posZ;
  std::optional<double> dirX;
  std::optional<double> dirY;
  std::optional<double> dirZ;
  std::optional<double> radius;
  std::string sourceClass;
  std::string visualName;
  std::string scriptInstance;
  std::string triggerTarget;
  std::string focusName;
  std::string containerContents;
  std::uint32_t archiveId = 0;
  std::size_t treeOrdinal = 0;
};

struct WaypointEdge final {
  std::string worldName;
  std::string fromWaypointKey;
  std::string toWaypointKey;
  double travelCost = 1.0;
};

struct DaedalusSymbolRecord final {
  std::string symbolName;
  std::string symbolKind;
  std::string dataType;
  std::string parentSymbol;
  std::uint32_t index = 0;
  std::uint32_t address = 0;
  std::uint32_t count = 0;
  std::uint32_t fileIndex = 0;
  bool isConst = false;
  bool isMember = false;
  bool isExternal = false;
  bool hasReturn = false;
};

struct NpcTemplateRecord final {
  std::string npcInstance;
  std::string displayName;
  std::string guild;
  std::string level;
  std::string routineSymbol;
  std::string perceptionSymbol;
  std::string fightTactic;
  std::string voiceSymbol;
  std::uint32_t symbolIndex = 0;
  std::uint32_t address = 0;
  std::string extractionMode = "symbol_candidate";
};

struct ItemTemplateRecord final {
  std::string itemInstance;
  std::string displayName;
  std::string itemCategory;
  std::string mainFlag;
  std::string flagsValue;
  std::string valueAmount;
  std::string damageTotal;
  std::uint32_t symbolIndex = 0;
  std::uint32_t address = 0;
  std::string extractionMode = "symbol_candidate";
};

struct RoutineRecord final {
  std::string npcInstance;
  std::string routineSymbol;
  std::string ownerHint;
  std::string actionSymbol;
  std::uint32_t symbolIndex = 0;
  std::uint32_t address = 0;
  std::string extractionMode = "function_name_candidate";
};

struct PerceptionBindingRecord final {
  std::string ownerSymbol;
  std::string ownerKind = "global";
  std::string perceptionKind;
  std::string functionSymbol;
  std::uint32_t priority = 0;
  std::uint32_t symbolIndex = 0;
  std::uint32_t address = 0;
  std::string extractionMode = "function_name_candidate";
};

struct DialogInfoRecord final {
  std::string infoSymbol;
  std::string npcInstance;
  std::string conditionSymbol;
  std::string informationSymbol;
  bool permanent = false;
  bool important = false;
  bool trade = false;
  std::uint32_t symbolIndex = 0;
  std::uint32_t address = 0;
  std::string extractionMode = "symbol_candidate";
};

struct DialogOutputRecord final {
  std::string outputName;
  std::string text;
  std::string audioRef;
  std::uint32_t messageType = 0;
};

struct ParserErrorRecord final {
  std::string severity = "warning";
  std::string errorScope = "parser";
  std::string errorCode;
  std::string sourceLogicalPath;
  std::string messageText;
};

struct SnapshotData final {
  SourceOptions sources;
  std::vector<ZenEntity> zenEntities;
  std::vector<WaypointEdge> waypointEdges;
  std::vector<DaedalusSymbolRecord> daedalusSymbols;
  std::vector<NpcTemplateRecord> npcTemplates;
  std::vector<ItemTemplateRecord> itemTemplates;
  std::vector<RoutineRecord> routines;
  std::vector<PerceptionBindingRecord> perceptionBindings;
  std::vector<DialogInfoRecord> dialogInfos;
  std::vector<DialogOutputRecord> dialogOutputs;
  std::vector<ParserErrorRecord> parserErrors;
};

[[nodiscard]] bool isSet(const SourceFile& source) {
  return !source.hostPath.empty();
}

[[nodiscard]] std::uintmax_t byteSize(const std::filesystem::path& path) {
  std::error_code ec;
  const auto size = std::filesystem::file_size(path, ec);
  return ec ? 0 : size;
}

[[nodiscard]] std::string genericPath(std::filesystem::path path) {
  return path.lexically_normal().generic_string();
}

[[nodiscard]] std::string lowerAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    if(c >= 'A' && c <= 'Z') {
      return static_cast<char>(c - 'A' + 'a');
    }
    return static_cast<char>(c);
  });
  return value;
}

[[nodiscard]] std::string upperAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    if(c >= 'a' && c <= 'z') {
      return static_cast<char>(c - 'a' + 'A');
    }
    return static_cast<char>(c);
  });
  return value;
}

[[nodiscard]] bool startsWith(std::string_view text, std::string_view prefix) noexcept {
  return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

[[nodiscard]] bool contains(std::string_view text, std::string_view needle) noexcept {
  return text.find(needle) != std::string_view::npos;
}

[[nodiscard]] std::string logicalPathOrDefault(const SourceFile& source) {
  if(!source.logicalPath.empty()) {
    return lowerAscii(genericPath(source.logicalPath));
  }
  return lowerAscii(genericPath(source.hostPath.filename()));
}

[[nodiscard]] SourceFile normalizedSource(SourceFile source) {
  if(source.sourceKey.empty()) {
    source.sourceKey = "other";
  }
  source.logicalPath = logicalPathOrDefault(source);
  source.byteSize = byteSize(source.hostPath);
  return source;
}

void appendUtf8Codepoint(std::string& out, std::uint32_t cp) {
  if(cp <= 0x7F) {
    out.push_back(static_cast<char>(cp));
  } else if(cp <= 0x7FF) {
    out.push_back(static_cast<char>(0xC0U | (cp >> 6U)));
    out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
  } else if(cp <= 0xFFFF) {
    out.push_back(static_cast<char>(0xE0U | (cp >> 12U)));
    out.push_back(static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
  } else {
    out.push_back(static_cast<char>(0xF0U | (cp >> 18U)));
    out.push_back(static_cast<char>(0x80U | ((cp >> 12U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
  }
}

[[nodiscard]] bool isValidUtf8(std::string_view text) {
  for(std::size_t i = 0; i < text.size();) {
    const auto c = static_cast<unsigned char>(text[i]);
    if(c <= 0x7F) {
      ++i;
      continue;
    }
    std::size_t need = 0;
    std::uint32_t min = 0;
    std::uint32_t cp = 0;
    if((c & 0xE0U) == 0xC0U) {
      need = 1;
      min = 0x80;
      cp = c & 0x1FU;
    } else if((c & 0xF0U) == 0xE0U) {
      need = 2;
      min = 0x800;
      cp = c & 0x0FU;
    } else if((c & 0xF8U) == 0xF0U) {
      need = 3;
      min = 0x10000;
      cp = c & 0x07U;
    } else {
      return false;
    }
    if(i + need >= text.size()) {
      return false;
    }
    for(std::size_t j = 0; j < need; ++j) {
      const auto cc = static_cast<unsigned char>(text[i + 1 + j]);
      if((cc & 0xC0U) != 0x80U) {
        return false;
      }
      cp = (cp << 6U) | (cc & 0x3FU);
    }
    if(cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
      return false;
    }
    i += need + 1;
  }
  return true;
}

[[nodiscard]] std::string windows1252ToUtf8(std::string_view text) {
  static constexpr std::uint16_t cp1252[32] = {
      0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
      0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0xFFFD, 0x017D, 0xFFFD,
      0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
      0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0xFFFD, 0x017E, 0x0178,
  };
  std::string out;
  out.reserve(text.size());
  for(const unsigned char c : text) {
    if(c < 0x80) {
      out.push_back(static_cast<char>(c));
    } else if(c >= 0x80 && c <= 0x9F) {
      appendUtf8Codepoint(out, cp1252[c - 0x80]);
    } else {
      appendUtf8Codepoint(out, c);
    }
  }
  return out;
}

[[nodiscard]] std::string jsonText(std::string_view text) {
  if(isValidUtf8(text)) {
    return std::string(text);
  }
  return windows1252ToUtf8(text);
}

class JsonWriter final {
public:
  explicit JsonWriter(std::ostream& out) : out(out) {}

  void beginObject() {
    beginValue();
    out << "{";
    stack.push_back(true);
    ++indent;
  }

  void endObject() {
    --indent;
    newline();
    out << "}";
    stack.pop_back();
  }

  void beginArray() {
    beginValue();
    out << "[";
    stack.push_back(true);
    ++indent;
  }

  void endArray() {
    --indent;
    newline();
    out << "]";
    stack.pop_back();
  }

  void key(std::string_view name) {
    beginValue();
    writeString(name);
    out << ": ";
    pendingKey = true;
  }

  void field(std::string_view name, std::string_view value) {
    key(name);
    pendingKey = false;
    writeString(value);
  }

  void field(std::string_view name, const char* value) {
    field(name, std::string_view(value != nullptr ? value : ""));
  }

  void field(std::string_view name, bool value) {
    key(name);
    pendingKey = false;
    out << (value ? "true" : "false");
  }

  void field(std::string_view name, std::uint64_t value) {
    key(name);
    pendingKey = false;
    out << value;
  }

  void field(std::string_view name, std::uint32_t value) {
    field(name, static_cast<std::uint64_t>(value));
  }

  void field(std::string_view name, double value) {
    key(name);
    pendingKey = false;
    if(std::isfinite(value)) {
      out << std::setprecision(9) << value;
    } else {
      out << "null";
    }
  }

  void field(std::string_view name, std::optional<double> value) {
    key(name);
    pendingKey = false;
    if(value.has_value() && std::isfinite(*value)) {
      out << std::setprecision(9) << *value;
    } else {
      out << "null";
    }
  }

private:
  void beginValue() {
    if(pendingKey) {
      pendingKey = false;
      return;
    }
    if(!stack.empty()) {
      if(stack.back()) {
        stack.back() = false;
      } else {
        out << ",";
      }
      newline();
    }
  }

  void newline() {
    out << "\n";
    for(int i = 0; i < indent; ++i) {
      out << "  ";
    }
  }

  void writeString(std::string_view value) {
    out << '"';
    const std::string utf8 = jsonText(value);
    for(const unsigned char c : utf8) {
      switch(c) {
        case '"':
          out << "\\\"";
          break;
        case '\\':
          out << "\\\\";
          break;
        case '\b':
          out << "\\b";
          break;
        case '\f':
          out << "\\f";
          break;
        case '\n':
          out << "\\n";
          break;
        case '\r':
          out << "\\r";
          break;
        case '\t':
          out << "\\t";
          break;
        default:
          if(c < 0x20) {
            out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c) << std::dec
                << std::setfill(' ');
          } else {
            out << static_cast<char>(c);
          }
          break;
      }
    }
    out << '"';
  }

  std::ostream& out;
  int indent = 0;
  bool pendingKey = false;
  std::vector<bool> stack;
};

[[nodiscard]] std::string dataTypeName(zenkit::DaedalusDataType type) {
  switch(type) {
    case zenkit::DaedalusDataType::VOID:
      return "void";
    case zenkit::DaedalusDataType::FLOAT:
      return "float";
    case zenkit::DaedalusDataType::INT:
      return "int";
    case zenkit::DaedalusDataType::STRING:
      return "string";
    case zenkit::DaedalusDataType::CLASS:
      return "class";
    case zenkit::DaedalusDataType::FUNCTION:
      return "function";
    case zenkit::DaedalusDataType::PROTOTYPE:
      return "prototype";
    case zenkit::DaedalusDataType::INSTANCE:
      return "instance";
  }
  return "unknown";
}

[[nodiscard]] std::string parentName(const zenkit::DaedalusScript& script, const zenkit::DaedalusSymbol& symbol) {
  const auto* parent = script.find_symbol_by_index(symbol.parent());
  return parent != nullptr ? parent->name() : "";
}

[[nodiscard]] std::string symbolKind(const zenkit::DaedalusScript& script, const zenkit::DaedalusSymbol& symbol) {
  if(symbol.type() == zenkit::DaedalusDataType::CLASS) {
    return "class";
  }
  if(symbol.type() == zenkit::DaedalusDataType::FUNCTION) {
    return symbol.is_external() ? "external_function" : "function";
  }
  if(symbol.type() == zenkit::DaedalusDataType::PROTOTYPE) {
    return "prototype";
  }
  if(symbol.type() == zenkit::DaedalusDataType::INSTANCE) {
    const auto parent = upperAscii(parentName(script, symbol));
    if(parent == "C_NPC") {
      return "npc_instance";
    }
    if(parent == "C_ITEM") {
      return "item_instance";
    }
    if(parent == "C_INFO") {
      return "dialog_info";
    }
    if(parent == "C_MISSION") {
      return "mission_instance";
    }
    return "instance";
  }
  if(symbol.is_member()) {
    return "member";
  }
  if(symbol.is_const()) {
    return "const";
  }
  return "variable";
}

[[nodiscard]] bool isFunctionSymbol(const zenkit::DaedalusSymbol& symbol) noexcept {
  return symbol.type() == zenkit::DaedalusDataType::FUNCTION && !symbol.is_external();
}

[[nodiscard]] bool isRoutineFunctionName(std::string_view upperName) noexcept {
  return startsWith(upperName, "RTN_") || startsWith(upperName, "ROUTINE_") || contains(upperName, "_RTN_");
}

[[nodiscard]] bool isPerceptionFunctionName(std::string_view upperName) noexcept {
  return contains(upperName, "ASSESS") || contains(upperName, "PERCEPTION") || startsWith(upperName, "PERC_");
}

[[nodiscard]] std::string routineOwnerHint(std::string_view symbolName) {
  const auto last = symbolName.rfind('_');
  if(last == std::string_view::npos || last + 1 >= symbolName.size()) {
    return {};
  }
  const auto suffix = symbolName.substr(last + 1);
  const bool numeric = !suffix.empty() && std::all_of(suffix.begin(), suffix.end(), [](unsigned char c) {
    return c >= '0' && c <= '9';
  });
  return numeric ? std::string(suffix) : std::string();
}

[[nodiscard]] std::string perceptionKindForFunction(std::string_view upperName) {
  if(contains(upperName, "ASSESSPLAYER")) {
    return "PERC_ASSESSPLAYER";
  }
  if(contains(upperName, "ASSESSENEMY")) {
    return "PERC_ASSESSENEMY";
  }
  if(contains(upperName, "ASSESSFIGHTER")) {
    return "PERC_ASSESSFIGHTER";
  }
  if(contains(upperName, "ASSESSBODY")) {
    return "PERC_ASSESSBODY";
  }
  if(contains(upperName, "ASSESSMAGIC")) {
    return "PERC_ASSESSMAGIC";
  }
  if(contains(upperName, "ASSESSWARN")) {
    return "PERC_ASSESSWARN";
  }
  if(contains(upperName, "ASSESS") || contains(upperName, "PERCEPTION") || startsWith(upperName, "PERC_")) {
    return "PERC_CANDIDATE";
  }
  return {};
}

[[nodiscard]] std::string functionByUpperName(const std::unordered_map<std::string, std::string>& functionsByUpper,
                                              std::string name) {
  const auto it = functionsByUpper.find(upperAscii(std::move(name)));
  return it != functionsByUpper.end() ? it->second : std::string();
}

[[nodiscard]] std::string classNameFor(const zenkit::VirtualObject& vob) {
  switch(vob.type) {
    case zenkit::VirtualObjectType::zCVob:
      return "zCVob";
    case zenkit::VirtualObjectType::zCVobLevelCompo:
      return "zCVobLevelCompo";
    case zenkit::VirtualObjectType::oCItem:
      return "oCItem";
    case zenkit::VirtualObjectType::oCNpc:
      return "oCNpc";
    case zenkit::VirtualObjectType::zCTrigger:
      return "zCTrigger";
    case zenkit::VirtualObjectType::zCTriggerList:
      return "zCTriggerList";
    case zenkit::VirtualObjectType::zCTriggerWorldStart:
      return "zCTriggerWorldStart";
    case zenkit::VirtualObjectType::oCTriggerScript:
      return "oCTriggerScript";
    case zenkit::VirtualObjectType::oCTriggerChangeLevel:
      return "oCTriggerChangeLevel";
    case zenkit::VirtualObjectType::zCMover:
      return "zCMover";
    case zenkit::VirtualObjectType::oCMOB:
      return "oCMOB";
    case zenkit::VirtualObjectType::oCMobInter:
      return "oCMobInter";
    case zenkit::VirtualObjectType::oCMobContainer:
      return "oCMobContainer";
    case zenkit::VirtualObjectType::oCMobDoor:
      return "oCMobDoor";
    case zenkit::VirtualObjectType::oCMobSwitch:
      return "oCMobSwitch";
    case zenkit::VirtualObjectType::oCMobWheel:
      return "oCMobWheel";
    case zenkit::VirtualObjectType::oCMobBed:
      return "oCMobBed";
    case zenkit::VirtualObjectType::oCMobFire:
      return "oCMobFire";
    case zenkit::VirtualObjectType::zCVobLight:
      return "zCVobLight";
    case zenkit::VirtualObjectType::zCVobSound:
      return "zCVobSound";
    case zenkit::VirtualObjectType::zCVobSoundDaytime:
      return "zCVobSoundDaytime";
    default:
      return "unknown";
  }
}

[[nodiscard]] std::string entityKindFor(const zenkit::VirtualObject& vob) {
  switch(vob.type) {
    case zenkit::VirtualObjectType::oCNpc:
    case zenkit::VirtualObjectType::oCItem:
      return "spawn";
    case zenkit::VirtualObjectType::zCTrigger:
    case zenkit::VirtualObjectType::zCTriggerList:
    case zenkit::VirtualObjectType::zCTriggerWorldStart:
    case zenkit::VirtualObjectType::oCTriggerScript:
    case zenkit::VirtualObjectType::oCTriggerChangeLevel:
    case zenkit::VirtualObjectType::oCCSTrigger:
      return "trigger";
    case zenkit::VirtualObjectType::zCMover:
      return "mover";
    case zenkit::VirtualObjectType::zCVobSound:
    case zenkit::VirtualObjectType::zCVobSoundDaytime:
      return "sound";
    case zenkit::VirtualObjectType::zCVobLight:
    case zenkit::VirtualObjectType::zCVobSpot:
      return "light";
    default:
      return "vob";
  }
}

[[nodiscard]] std::string visualNameFor(const zenkit::VirtualObject& vob) {
  if(vob.visual != nullptr && !vob.visual->name.empty()) {
    return vob.visual->name;
  }
  return vob.visual_name;
}

[[nodiscard]] std::string firstNonEmpty(std::initializer_list<std::string_view> values) {
  for(const auto value : values) {
    if(!value.empty()) {
      return std::string(value);
    }
  }
  return {};
}

[[nodiscard]] std::string keyForVob(const std::string& worldName,
                                    const zenkit::VirtualObject& vob,
                                    std::size_t ordinal,
                                    std::string_view scriptInstance) {
  const auto sourceClass = classNameFor(vob);
  const auto baseName = firstNonEmpty({vob.vob_name, scriptInstance, visualNameFor(vob), vob.preset_name});
  std::ostringstream out;
  out << lowerAscii(worldName) << ":" << sourceClass << ":";
  if(!baseName.empty()) {
    out << baseName << ":";
  }
  out << ordinal;
  return out.str();
}

[[nodiscard]] double distance(const zenkit::Vec3& a, const zenkit::Vec3& b) {
  const double dx = static_cast<double>(a.x) - static_cast<double>(b.x);
  const double dy = static_cast<double>(a.y) - static_cast<double>(b.y);
  const double dz = static_cast<double>(a.z) - static_cast<double>(b.z);
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

void collectVob(const std::string& worldName,
                const std::shared_ptr<zenkit::VirtualObject>& node,
                std::vector<ZenEntity>& out,
                std::size_t& ordinal) {
  if(node == nullptr) {
    return;
  }

  const auto currentOrdinal = ordinal++;
  ZenEntity entity;
  entity.worldName = worldName;
  entity.entityKind = entityKindFor(*node);
  entity.sourceClass = classNameFor(*node);
  entity.visualName = visualNameFor(*node);
  entity.archiveId = node->id;
  entity.treeOrdinal = currentOrdinal;
  entity.name = firstNonEmpty({node->vob_name, entity.visualName, node->preset_name});
  entity.posX = node->position.x;
  entity.posY = node->position.y;
  entity.posZ = node->position.z;

  if(const auto item = std::dynamic_pointer_cast<zenkit::VItem>(node)) {
    entity.scriptInstance = item->instance;
  }
  if(const auto npc = std::dynamic_pointer_cast<zenkit::VNpc>(node)) {
    entity.scriptInstance = npc->npc_instance;
  }
  if(const auto trigger = std::dynamic_pointer_cast<zenkit::VTrigger>(node)) {
    entity.triggerTarget = trigger->target;
  }
  if(const auto sound = std::dynamic_pointer_cast<zenkit::VSound>(node)) {
    entity.radius = sound->radius;
    if(entity.name.empty()) {
      entity.name = sound->sound_name;
    }
  }
  if(const auto light = std::dynamic_pointer_cast<zenkit::VLight>(node)) {
    entity.radius = light->range;
  }
  if(const auto mob = std::dynamic_pointer_cast<zenkit::VMovableObject>(node)) {
    entity.focusName = mob->name;
  }
  if(const auto mob = std::dynamic_pointer_cast<zenkit::VInteractiveObject>(node)) {
    if(entity.triggerTarget.empty()) {
      entity.triggerTarget = mob->target;
    }
  }
  if(const auto container = std::dynamic_pointer_cast<zenkit::VContainer>(node)) {
    entity.containerContents = container->contents;
  }
  if(entity.name.empty()) {
    entity.name = entity.sourceClass + ":" + std::to_string(currentOrdinal);
  }
  entity.entityKey = keyForVob(worldName, *node, currentOrdinal, entity.scriptInstance);
  out.push_back(std::move(entity));

  for(const auto& child : node->children) {
    collectVob(worldName, child, out, ordinal);
  }
}

void loadZenWorld(const SourceFile& source, std::string worldName, SnapshotData& data) {
  zenkit::World world;
  auto read = zenkit::Read::from(source.hostPath);
  world.load(read.get());

  if(worldName.empty()) {
    worldName = lowerAscii(source.hostPath.stem().generic_string());
  }

  ZenEntity worldEntity;
  worldEntity.worldName = worldName;
  worldEntity.entityKind = "world";
  worldEntity.entityKey = worldName;
  worldEntity.name = worldName;
  worldEntity.sourceClass = "oCWorld";
  data.zenEntities.push_back(std::move(worldEntity));

  if(world.way_net != nullptr) {
    for(const auto& point : world.way_net->points) {
      if(point == nullptr) {
        continue;
      }
      ZenEntity entity;
      entity.worldName = worldName;
      entity.entityKind = point->free_point ? "freepoint" : "waypoint";
      entity.entityKey = point->name;
      entity.name = point->name;
      entity.sourceClass = "zCWaypoint";
      entity.posX = point->position.x;
      entity.posY = point->position.y;
      entity.posZ = point->position.z;
      entity.dirX = point->direction.x;
      entity.dirY = point->direction.y;
      entity.dirZ = point->direction.z;
      data.zenEntities.push_back(std::move(entity));
    }

    for(const auto& edge : world.way_net->edges) {
      if(edge.first == nullptr || edge.second == nullptr) {
        continue;
      }
      WaypointEdge out;
      out.worldName = worldName;
      out.fromWaypointKey = edge.first->name;
      out.toWaypointKey = edge.second->name;
      out.travelCost = distance(edge.first->position, edge.second->position);
      data.waypointEdges.push_back(std::move(out));
    }
  }

  std::size_t ordinal = 0;
  for(const auto& root : world.world_vobs) {
    collectVob(worldName, root, data.zenEntities, ordinal);
  }
}

void loadDaedalusSymbols(const SourceFile& source, SnapshotData& data) {
  zenkit::DaedalusScript script;
  auto read = zenkit::Read::from(source.hostPath);
  script.load(read.get());

  std::unordered_map<std::string, std::string> functionsByUpper;
  functionsByUpper.reserve(script.symbols().size());
  for(const auto& symbol : script.symbols()) {
    if(isFunctionSymbol(symbol)) {
      functionsByUpper.emplace(upperAscii(symbol.name()), symbol.name());
    }
  }

  data.daedalusSymbols.reserve(data.daedalusSymbols.size() + script.symbols().size());
  for(const auto& symbol : script.symbols()) {
    const auto kind = symbolKind(script, symbol);
    DaedalusSymbolRecord out;
    out.symbolName = symbol.name();
    out.symbolKind = kind;
    out.dataType = dataTypeName(symbol.type());
    out.parentSymbol = parentName(script, symbol);
    out.index = symbol.index();
    out.address = symbol.address();
    out.count = symbol.count();
    out.fileIndex = symbol.file_index();
    out.isConst = symbol.is_const();
    out.isMember = symbol.is_member();
    out.isExternal = symbol.is_external();
    out.hasReturn = symbol.has_return();
    data.daedalusSymbols.push_back(std::move(out));

    if(kind == "npc_instance") {
      NpcTemplateRecord npc;
      npc.npcInstance = symbol.name();
      npc.symbolIndex = symbol.index();
      npc.address = symbol.address();
      data.npcTemplates.push_back(std::move(npc));
    } else if(kind == "item_instance") {
      ItemTemplateRecord item;
      item.itemInstance = symbol.name();
      item.symbolIndex = symbol.index();
      item.address = symbol.address();
      data.itemTemplates.push_back(std::move(item));
    } else if(kind == "dialog_info") {
      DialogInfoRecord info;
      info.infoSymbol = symbol.name();
      info.conditionSymbol = functionByUpperName(functionsByUpper, symbol.name() + "_Condition");
      info.informationSymbol = functionByUpperName(functionsByUpper, symbol.name() + "_Info");
      if(info.informationSymbol.empty()) {
        info.informationSymbol = functionByUpperName(functionsByUpper, symbol.name() + "_Information");
      }
      info.symbolIndex = symbol.index();
      info.address = symbol.address();
      data.dialogInfos.push_back(std::move(info));
    } else if(isFunctionSymbol(symbol)) {
      const auto upperName = upperAscii(symbol.name());
      if(isRoutineFunctionName(upperName)) {
        RoutineRecord routine;
        routine.routineSymbol = symbol.name();
        routine.ownerHint = routineOwnerHint(symbol.name());
        routine.symbolIndex = symbol.index();
        routine.address = symbol.address();
        data.routines.push_back(std::move(routine));
      }

      const auto perceptionKind = perceptionKindForFunction(upperName);
      if(!perceptionKind.empty()) {
        PerceptionBindingRecord binding;
        binding.perceptionKind = perceptionKind;
        binding.functionSymbol = symbol.name();
        binding.symbolIndex = symbol.index();
        binding.address = symbol.address();
        data.perceptionBindings.push_back(std::move(binding));
      }
    }
  }
}

[[nodiscard]] zenkit::GameVersion gameVersionFor(std::string_view gameCode) {
  const auto code = lowerAscii(std::string(gameCode));
  if(code == "gothic1" || code == "g1" || code == "gothic") {
    return zenkit::GameVersion::GOTHIC_1;
  }
  return zenkit::GameVersion::GOTHIC_2;
}

void loadDialogOutputs(const SourceFile& source, const SnapshotOptions& options, SnapshotData& data) {
  auto read = zenkit::Read::from(source.hostPath);
  auto archive = zenkit::ReadArchive::from(read.get());
  auto library = archive->read_object<zenkit::CutsceneLibrary>(gameVersionFor(options.gameCode));
  if(library == nullptr) {
    return;
  }

  data.dialogOutputs.reserve(data.dialogOutputs.size() + library->blocks.size());
  for(const auto& block : library->blocks) {
    if(block == nullptr) {
      continue;
    }
    const auto message = block->get_message();
    if(message == nullptr) {
      continue;
    }
    DialogOutputRecord out;
    out.outputName = block->name;
    out.text = message->text;
    out.audioRef = message->name;
    out.messageType = message->type;
    data.dialogOutputs.push_back(std::move(out));
  }
}

void appendParserError(SnapshotData& data,
                       const SourceFile& source,
                       std::string errorCode,
                       std::string message,
                       std::string scope = "parser",
                       std::string severity = "warning") {
  ParserErrorRecord error;
  error.severity = std::move(severity);
  error.errorScope = std::move(scope);
  error.errorCode = std::move(errorCode);
  error.sourceLogicalPath = source.logicalPath;
  error.messageText = std::move(message);
  data.parserErrors.push_back(std::move(error));
}

[[nodiscard]] SnapshotData loadSnapshotData(SnapshotOptions options) {
  SnapshotData data;
  if(isSet(options.sources.worldZen)) {
    data.sources.worldZen = normalizedSource(options.sources.worldZen);
    try {
      loadZenWorld(data.sources.worldZen, options.worldName, data);
    } catch(const std::exception& e) {
      appendParserError(data, data.sources.worldZen, "zen_world_parse_failed", e.what(), "world_zen", "error");
    }
  }
  if(isSet(options.sources.scriptsDat)) {
    data.sources.scriptsDat = normalizedSource(options.sources.scriptsDat);
    try {
      loadDaedalusSymbols(data.sources.scriptsDat, data);
    } catch(const std::exception& e) {
      appendParserError(data, data.sources.scriptsDat, "daedalus_dat_parse_failed", e.what(), "scripts_dat", "error");
    }
  }
  if(isSet(options.sources.dialogOu)) {
    data.sources.dialogOu = normalizedSource(options.sources.dialogOu);
    try {
      loadDialogOutputs(data.sources.dialogOu, options, data);
    } catch(const std::exception& e) {
      appendParserError(data, data.sources.dialogOu, "dialog_ou_parse_failed", e.what(), "dialog_ou", "warning");
    }
  }
  return data;
}

void writeSourceValue(JsonWriter& json, const SourceFile& source) {
  json.beginObject();
  json.field("logical_path", source.logicalPath);
  json.field("host_path", genericPath(source.hostPath));
  json.field("file_role", source.sourceKey);
  json.field("byte_size", static_cast<std::uint64_t>(source.byteSize));
  json.field("required_for_server_authority", source.requiredForServerAuthority);
  if(!source.sha256.empty()) {
    json.field("sha256", source.sha256);
  }
  json.endObject();
}

void writeSources(JsonWriter& json, const SnapshotData& data) {
  json.key("sources");
  json.beginObject();
  if(isSet(data.sources.worldZen)) {
    json.key("world_zen");
    writeSourceValue(json, data.sources.worldZen);
  }
  if(isSet(data.sources.scriptsDat)) {
    json.key("scripts_dat");
    writeSourceValue(json, data.sources.scriptsDat);
  }
  if(isSet(data.sources.dialogOu)) {
    json.key("dialog_ou");
    writeSourceValue(json, data.sources.dialogOu);
  }
  json.endObject();
}

void writeZenEntity(JsonWriter& json, const ZenEntity& entity) {
  json.beginObject();
  json.field("world_name", entity.worldName);
  json.field("entity_kind", entity.entityKind);
  json.field("entity_key", entity.entityKey);
  json.field("name", entity.name);
  json.field("pos_x", entity.posX);
  json.field("pos_y", entity.posY);
  json.field("pos_z", entity.posZ);
  json.field("dir_x", entity.dirX);
  json.field("dir_y", entity.dirY);
  json.field("dir_z", entity.dirZ);
  json.field("radius", entity.radius);
  json.field("source_class", entity.sourceClass);
  json.field("visual_name", entity.visualName);
  json.field("script_instance", entity.scriptInstance);
  json.field("trigger_target", entity.triggerTarget);
  json.field("focus_name", entity.focusName);
  json.field("container_contents", entity.containerContents);
  json.field("archive_id", entity.archiveId);
  json.field("tree_ordinal", static_cast<std::uint64_t>(entity.treeOrdinal));
  json.endObject();
}

void writeWaypointEdge(JsonWriter& json, const WaypointEdge& edge) {
  json.beginObject();
  json.field("world_name", edge.worldName);
  json.field("from_waypoint_key", edge.fromWaypointKey);
  json.field("to_waypoint_key", edge.toWaypointKey);
  json.field("travel_cost", edge.travelCost);
  json.field("edge_flags", "");
  json.endObject();
}

void writeDaedalusSymbol(JsonWriter& json, const DaedalusSymbolRecord& symbol) {
  json.beginObject();
  json.field("symbol_name", symbol.symbolName);
  json.field("symbol_kind", symbol.symbolKind);
  json.field("data_type", symbol.dataType);
  json.field("parent_symbol", symbol.parentSymbol);
  json.field("index", symbol.index);
  json.field("address", symbol.address);
  json.field("count", symbol.count);
  json.field("file_index", symbol.fileIndex);
  json.field("is_const", symbol.isConst);
  json.field("is_member", symbol.isMember);
  json.field("is_external", symbol.isExternal);
  json.field("has_return", symbol.hasReturn);
  json.endObject();
}

void writeNpcTemplate(JsonWriter& json, const NpcTemplateRecord& npc) {
  json.beginObject();
  json.field("npc_instance", npc.npcInstance);
  json.field("display_name", npc.displayName);
  json.field("guild", npc.guild);
  json.field("level", npc.level);
  json.field("routine_symbol", npc.routineSymbol);
  json.field("perception_symbol", npc.perceptionSymbol);
  json.field("fight_tactic", npc.fightTactic);
  json.field("voice_symbol", npc.voiceSymbol);
  json.field("symbol_index", npc.symbolIndex);
  json.field("address", npc.address);
  json.field("extraction_mode", npc.extractionMode);
  json.endObject();
}

void writeItemTemplate(JsonWriter& json, const ItemTemplateRecord& item) {
  json.beginObject();
  json.field("item_instance", item.itemInstance);
  json.field("display_name", item.displayName);
  json.field("item_category", item.itemCategory);
  json.field("main_flag", item.mainFlag);
  json.field("flags_value", item.flagsValue);
  json.field("value_amount", item.valueAmount);
  json.field("damage_total", item.damageTotal);
  json.field("symbol_index", item.symbolIndex);
  json.field("address", item.address);
  json.field("extraction_mode", item.extractionMode);
  json.endObject();
}

void writeRoutine(JsonWriter& json, const RoutineRecord& routine) {
  json.beginObject();
  json.field("npc_instance", routine.npcInstance);
  json.field("routine_symbol", routine.routineSymbol);
  json.field("owner_hint", routine.ownerHint);
  json.field("action_symbol", routine.actionSymbol);
  json.field("day_minute_start", "");
  json.field("day_minute_end", "");
  json.field("target_point_key", "");
  json.field("symbol_index", routine.symbolIndex);
  json.field("address", routine.address);
  json.field("extraction_mode", routine.extractionMode);
  json.endObject();
}

void writePerceptionBinding(JsonWriter& json, const PerceptionBindingRecord& binding) {
  json.beginObject();
  json.field("owner_symbol", binding.ownerSymbol);
  json.field("owner_kind", binding.ownerKind);
  json.field("perception_kind", binding.perceptionKind);
  json.field("function_symbol", binding.functionSymbol);
  json.field("priority", binding.priority);
  json.field("symbol_index", binding.symbolIndex);
  json.field("address", binding.address);
  json.field("extraction_mode", binding.extractionMode);
  json.endObject();
}

void writeDialogInfo(JsonWriter& json, const DialogInfoRecord& info) {
  json.beginObject();
  json.field("info_symbol", info.infoSymbol);
  json.field("npc_instance", info.npcInstance);
  json.field("condition_symbol", info.conditionSymbol);
  json.field("information_symbol", info.informationSymbol);
  json.field("permanent", info.permanent);
  json.field("important", info.important);
  json.field("trade", info.trade);
  json.field("symbol_index", info.symbolIndex);
  json.field("address", info.address);
  json.field("extraction_mode", info.extractionMode);
  json.endObject();
}

void writeDialogOutput(JsonWriter& json, const DialogOutputRecord& output) {
  json.beginObject();
  json.field("output_name", output.outputName);
  json.field("text", output.text);
  json.field("audio_ref", output.audioRef);
  json.field("message_type", output.messageType);
  json.field("speaker_symbol", "");
  json.field("target_symbol", "");
  json.endObject();
}

void writeParserError(JsonWriter& json, const ParserErrorRecord& error) {
  json.beginObject();
  json.field("severity", error.severity);
  json.field("error_scope", error.errorScope);
  json.field("error_code", error.errorCode);
  json.field("source_logical_path", error.sourceLogicalPath);
  json.field("message_text", error.messageText);
  json.endObject();
}

} // namespace

ImportSummary writeParserSnapshot(std::ostream& out, const SnapshotOptions& options) {
  if(options.contentRevisionKey.empty()) {
    throw std::invalid_argument("content_revision_key is required");
  }

  const SnapshotData data = loadSnapshotData(options);
  ImportSummary summary;
  summary.sourceCount = (isSet(data.sources.worldZen) ? 1U : 0U) + (isSet(data.sources.scriptsDat) ? 1U : 0U) +
                        (isSet(data.sources.dialogOu) ? 1U : 0U);
  summary.zenEntityCount = data.zenEntities.size();
  summary.waypointEdgeCount = data.waypointEdges.size();
  summary.daedalusSymbolCount = data.daedalusSymbols.size();
  summary.npcTemplateCount = data.npcTemplates.size();
  summary.itemTemplateCount = data.itemTemplates.size();
  summary.routineCount = data.routines.size();
  summary.perceptionBindingCount = data.perceptionBindings.size();
  summary.dialogInfoCount = data.dialogInfos.size();
  summary.dialogOutputCount = data.dialogOutputs.size();
  summary.parserErrorCount = data.parserErrors.size();

  JsonWriter json(out);
  json.beginObject();
  json.field("schema", "mmo.content_build_parser_snapshot.zenkit.v1");
  json.field("tool", "mmo_content_build_importer");
  json.field("content_revision_key", options.contentRevisionKey);
  json.field("game_code", options.gameCode);
  json.field("source_root_label", options.sourceRootLabel);
  if(!options.manifestHash.empty()) {
    json.field("manifest_hash", options.manifestHash);
  }
  writeSources(json, data);

  json.key("zen_entities");
  json.beginArray();
  for(const auto& entity : data.zenEntities) {
    writeZenEntity(json, entity);
  }
  json.endArray();

  json.key("waypoint_edges");
  json.beginArray();
  for(const auto& edge : data.waypointEdges) {
    writeWaypointEdge(json, edge);
  }
  json.endArray();

  json.key("daedalus_symbols");
  json.beginArray();
  for(const auto& symbol : data.daedalusSymbols) {
    writeDaedalusSymbol(json, symbol);
  }
  json.endArray();

  json.key("npc_templates");
  json.beginArray();
  for(const auto& npc : data.npcTemplates) {
    writeNpcTemplate(json, npc);
  }
  json.endArray();

  json.key("item_templates");
  json.beginArray();
  for(const auto& item : data.itemTemplates) {
    writeItemTemplate(json, item);
  }
  json.endArray();

  json.key("routines");
  json.beginArray();
  for(const auto& routine : data.routines) {
    writeRoutine(json, routine);
  }
  json.endArray();

  json.key("perception_bindings");
  json.beginArray();
  for(const auto& binding : data.perceptionBindings) {
    writePerceptionBinding(json, binding);
  }
  json.endArray();

  json.key("dialog_infos");
  json.beginArray();
  for(const auto& info : data.dialogInfos) {
    writeDialogInfo(json, info);
  }
  json.endArray();

  json.key("dialog_outputs");
  json.beginArray();
  for(const auto& output : data.dialogOutputs) {
    writeDialogOutput(json, output);
  }
  json.endArray();

  json.key("parser_errors");
  json.beginArray();
  for(const auto& error : data.parserErrors) {
    writeParserError(json, error);
  }
  json.endArray();
  json.endObject();
  out << "\n";

  return summary;
}

} // namespace Mmo::ContentBuild


