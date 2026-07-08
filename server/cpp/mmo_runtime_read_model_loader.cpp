#include "mmo_runtime_read_model_loader.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <system_error>

namespace Mmo::RuntimeReadModel {
namespace {

constexpr std::string_view ExpectedSchema = "mmo.content_build_runtime_read_model.v1";
constexpr char KeySeparator = '\x1f';

[[nodiscard]] std::string readFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if(!in) {
    throw std::runtime_error("cannot open runtime read model: " + path.generic_string());
  }
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}

[[nodiscard]] std::string lowerAscii(std::string value) {
  std::ranges::transform(value, value.begin(), [](unsigned char c) {
    if(c >= 'A' && c <= 'Z') {
      return static_cast<char>(c - 'A' + 'a');
    }
    return static_cast<char>(c);
  });
  return value;
}

[[nodiscard]] bool isHexSha256(std::string_view value) noexcept {
  if(value.size() != 64) {
    return false;
  }
  return std::ranges::all_of(value, [](unsigned char c) {
    return std::isxdigit(c) != 0 && (c < 'A' || c > 'F');
  });
}

void appendUtf8Codepoint(std::string& out, std::uint32_t cp) {
  if(cp <= 0x7FU) {
    out.push_back(static_cast<char>(cp));
  } else if(cp <= 0x7FFU) {
    out.push_back(static_cast<char>(0xC0U | (cp >> 6U)));
    out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
  } else if(cp <= 0xFFFFU) {
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

[[nodiscard]] int hexValue(char c) noexcept {
  if(c >= '0' && c <= '9') {
    return c - '0';
  }
  if(c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if(c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

class JsonCursor final {
public:
  explicit JsonCursor(std::string_view text) : text_(text) {}

  [[nodiscard]] bool finished() {
    skipWhitespace();
    return pos_ == text_.size();
  }

  [[nodiscard]] std::string parseString() {
    skipWhitespace();
    expect('"');
    std::string out;
    while(pos_ < text_.size()) {
      const char c = text_[pos_++];
      if(c == '"') {
        return out;
      }
      if(static_cast<unsigned char>(c) < 0x20U) {
        fail("control character inside JSON string");
      }
      if(c != '\\') {
        out.push_back(c);
        continue;
      }
      if(pos_ >= text_.size()) {
        fail("unterminated JSON escape");
      }
      const char escaped = text_[pos_++];
      switch(escaped) {
        case '"':
        case '\\':
        case '/':
          out.push_back(escaped);
          break;
        case 'b':
          out.push_back('\b');
          break;
        case 'f':
          out.push_back('\f');
          break;
        case 'n':
          out.push_back('\n');
          break;
        case 'r':
          out.push_back('\r');
          break;
        case 't':
          out.push_back('\t');
          break;
        case 'u':
          appendUtf8Codepoint(out, parseUnicodeEscape());
          break;
        default:
          fail("invalid JSON escape");
      }
    }
    fail("unterminated JSON string");
  }

  template<class Fn>
  void parseObject(Fn&& fn) {
    skipWhitespace();
    expect('{');
    skipWhitespace();
    if(consume('}')) {
      return;
    }
    while(true) {
      const std::string key = parseString();
      skipWhitespace();
      expect(':');
      fn(key, *this);
      skipWhitespace();
      if(consume('}')) {
        return;
      }
      expect(',');
    }
  }

  template<class Fn>
  [[nodiscard]] std::size_t parseArray(Fn&& fn) {
    skipWhitespace();
    expect('[');
    skipWhitespace();
    if(consume(']')) {
      return 0;
    }
    std::size_t count = 0;
    while(true) {
      fn(*this);
      if(count == std::numeric_limits<std::size_t>::max()) {
        fail("JSON array element count overflow");
      }
      ++count;
      skipWhitespace();
      if(consume(']')) {
        return count;
      }
      expect(',');
    }
  }

  [[nodiscard]] std::size_t countArrayElements() {
    return parseArray([](JsonCursor& value) {
      value.skipValue();
    });
  }

  [[nodiscard]] std::size_t parseSize() {
    skipWhitespace();
    const std::string_view token = parseNumberToken();
    if(token.find_first_of(".eE-+") != std::string_view::npos) {
      fail("expected unsigned integer JSON number");
    }
    std::size_t value = 0;
    const auto* first = token.data();
    const auto* last = first + token.size();
    const auto result = std::from_chars(first, last, value);
    if(result.ec != std::errc{} || result.ptr != last) {
      fail("invalid unsigned integer JSON number");
    }
    return value;
  }

  [[nodiscard]] std::string parseScalarString() {
    skipWhitespace();
    if(pos_ >= text_.size()) {
      fail("unexpected end of JSON scalar");
    }
    if(text_[pos_] == '"') {
      return parseString();
    }
    if(text_[pos_] == 'n') {
      expectLiteral("null");
      return {};
    }
    if(text_[pos_] == 't') {
      expectLiteral("true");
      return "true";
    }
    if(text_[pos_] == 'f') {
      expectLiteral("false");
      return "false";
    }
    if(text_[pos_] == '-' || (text_[pos_] >= '0' && text_[pos_] <= '9')) {
      const auto token = parseNumberToken();
      return std::string(token);
    }
    skipValue();
    return {};
  }

  [[nodiscard]] bool parseBool(bool fallback = false) {
    skipWhitespace();
    if(pos_ >= text_.size()) {
      fail("unexpected end of JSON bool");
    }
    if(text_[pos_] == 't') {
      expectLiteral("true");
      return true;
    }
    if(text_[pos_] == 'f') {
      expectLiteral("false");
      return false;
    }
    if(text_[pos_] == 'n') {
      expectLiteral("null");
      return fallback;
    }
    if(text_[pos_] == '"') {
      const auto text = lowerAscii(parseString());
      return text == "1" || text == "true" || text == "yes";
    }
    if(text_[pos_] == '-' || (text_[pos_] >= '0' && text_[pos_] <= '9')) {
      const auto token = parseNumberToken();
      return token != "0" && token != "-0";
    }
    skipValue();
    return fallback;
  }

  [[nodiscard]] double parseDouble(double fallback = 0.0) {
    skipWhitespace();
    if(pos_ >= text_.size()) {
      fail("unexpected end of JSON number");
    }
    std::string owned;
    std::string_view token;
    if(text_[pos_] == 'n') {
      expectLiteral("null");
      return fallback;
    }
    if(text_[pos_] == '"') {
      owned = parseString();
      token = owned;
    } else {
      token = parseNumberToken();
    }
    if(token.empty()) {
      return fallback;
    }
    double value = fallback;
    const auto* first = token.data();
    const auto* last = first + token.size();
    const auto result = std::from_chars(first, last, value);
    if(result.ec != std::errc{} || result.ptr != last) {
      fail("invalid JSON floating-point number");
    }
    return value;
  }

  void skipValue() {
    skipWhitespace();
    if(pos_ >= text_.size()) {
      fail("unexpected end of JSON value");
    }
    switch(text_[pos_]) {
      case '{':
        skipObject();
        return;
      case '[':
        skipArray();
        return;
      case '"':
        static_cast<void>(parseString());
        return;
      case 't':
        expectLiteral("true");
        return;
      case 'f':
        expectLiteral("false");
        return;
      case 'n':
        expectLiteral("null");
        return;
      default:
        if(text_[pos_] == '-' || (text_[pos_] >= '0' && text_[pos_] <= '9')) {
          static_cast<void>(parseNumberToken());
          return;
        }
        fail("unexpected JSON value");
    }
  }

private:
  std::string_view text_;
  std::size_t pos_ = 0;

  [[noreturn]] void fail(std::string_view message) const {
    throw std::runtime_error(std::string(message) + " at byte " + std::to_string(pos_));
  }

  void skipWhitespace() noexcept {
    while(pos_ < text_.size()) {
      const char c = text_[pos_];
      if(c != ' ' && c != '\n' && c != '\r' && c != '\t') {
        return;
      }
      ++pos_;
    }
  }

  [[nodiscard]] bool consume(char expected) noexcept {
    if(pos_ < text_.size() && text_[pos_] == expected) {
      ++pos_;
      return true;
    }
    return false;
  }

  void expect(char expected) {
    skipWhitespace();
    if(!consume(expected)) {
      fail(std::string("expected '") + expected + "'");
    }
  }

  void expectLiteral(std::string_view literal) {
    if(text_.substr(pos_, literal.size()) != literal) {
      fail("invalid JSON literal");
    }
    pos_ += literal.size();
  }

  [[nodiscard]] std::uint32_t parseUnicodeEscape() {
    std::uint32_t value = 0;
    for(int i = 0; i < 4; ++i) {
      if(pos_ >= text_.size()) {
        fail("unterminated unicode escape");
      }
      const int digit = hexValue(text_[pos_++]);
      if(digit < 0) {
        fail("invalid unicode escape");
      }
      value = (value << 4U) | static_cast<std::uint32_t>(digit);
    }
    return value;
  }

  void skipObject() {
    parseObject([](const std::string&, JsonCursor& value) {
      value.skipValue();
    });
  }

  void skipArray() {
    static_cast<void>(parseArray([](JsonCursor& value) {
      value.skipValue();
    }));
  }

  [[nodiscard]] std::string_view parseNumberToken() {
    skipWhitespace();
    const std::size_t begin = pos_;
    if(pos_ < text_.size() && text_[pos_] == '-') {
      ++pos_;
    }
    if(pos_ >= text_.size()) {
      fail("unterminated JSON number");
    }
    if(text_[pos_] == '0') {
      ++pos_;
    } else if(text_[pos_] >= '1' && text_[pos_] <= '9') {
      while(pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
        ++pos_;
      }
    } else {
      fail("invalid JSON number");
    }
    if(pos_ < text_.size() && text_[pos_] == '.') {
      ++pos_;
      if(pos_ >= text_.size() || text_[pos_] < '0' || text_[pos_] > '9') {
        fail("invalid JSON number fraction");
      }
      while(pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
        ++pos_;
      }
    }
    if(pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
      ++pos_;
      if(pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) {
        ++pos_;
      }
      if(pos_ >= text_.size() || text_[pos_] < '0' || text_[pos_] > '9') {
        fail("invalid JSON number exponent");
      }
      while(pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
        ++pos_;
      }
    }
    return text_.substr(begin, pos_ - begin);
  }
};

[[nodiscard]] std::string compositeKey(std::string_view first, std::string_view second) {
  std::string key;
  key.reserve(first.size() + second.size() + 1);
  key.append(first);
  key.push_back(KeySeparator);
  key.append(second);
  return key;
}

[[nodiscard]] std::string compositeKey(std::string_view first, std::string_view second, std::string_view third) {
  std::string key;
  key.reserve(first.size() + second.size() + third.size() + 2);
  key.append(first);
  key.push_back(KeySeparator);
  key.append(second);
  key.push_back(KeySeparator);
  key.append(third);
  return key;
}

} // namespace

std::string makeWorldZenEntityIndexKey(
    std::string_view worldName,
    std::string_view entityKind,
    std::string_view entityKey) {
  return compositeKey(worldName, entityKind, entityKey);
}

std::string makeWaypointEdgeRouteIndexKey(
    std::string_view worldName,
    std::string_view fromWaypointKey,
    std::string_view toWaypointKey) {
  return compositeKey(worldName, fromWaypointKey, toWaypointKey);
}

namespace {

void parseSummaryField(const std::string& key, JsonCursor& value, SectionCounts& summary, SectionFlags& present) {
  if(key == "world_zen_entity_count") {
    summary.worldZenEntityCount = value.parseSize();
    present.worldZenEntities = true;
  } else if(key == "waypoint_edge_count") {
    summary.waypointEdgeCount = value.parseSize();
    present.waypointEdges = true;
  } else if(key == "npc_template_count") {
    summary.npcTemplateCount = value.parseSize();
    present.npcTemplates = true;
  } else if(key == "item_template_count") {
    summary.itemTemplateCount = value.parseSize();
    present.itemTemplates = true;
  } else if(key == "routine_count") {
    summary.routineCount = value.parseSize();
    present.routines = true;
  } else if(key == "perception_binding_count") {
    summary.perceptionBindingCount = value.parseSize();
    present.perceptionBindings = true;
  } else if(key == "dialog_info_count") {
    summary.dialogInfoCount = value.parseSize();
    present.dialogInfos = true;
  } else if(key == "dialog_output_count") {
    summary.dialogOutputCount = value.parseSize();
    present.dialogOutputs = true;
  } else {
    value.skipValue();
  }
}

[[nodiscard]] WorldZenEntityRecord parseWorldZenEntity(JsonCursor& value) {
  WorldZenEntityRecord record;
  value.parseObject([&](const std::string& key, JsonCursor& field) {
    if(key == "world_name") {
      record.worldName = field.parseScalarString();
    } else if(key == "entity_kind") {
      record.entityKind = field.parseScalarString();
    } else if(key == "entity_key") {
      record.entityKey = field.parseScalarString();
    } else if(key == "name" || key == "entity_name") {
      record.name = field.parseScalarString();
    } else {
      field.skipValue();
    }
  });
  return record;
}

[[nodiscard]] WaypointEdgeRecord parseWaypointEdge(JsonCursor& value) {
  WaypointEdgeRecord record;
  value.parseObject([&](const std::string& key, JsonCursor& field) {
    if(key == "world_name") {
      record.worldName = field.parseScalarString();
    } else if(key == "from_waypoint_key") {
      record.fromWaypointKey = field.parseScalarString();
    } else if(key == "to_waypoint_key") {
      record.toWaypointKey = field.parseScalarString();
    } else if(key == "travel_cost") {
      record.travelCost = field.parseDouble(0.0);
    } else {
      field.skipValue();
    }
  });
  return record;
}

[[nodiscard]] NpcTemplateRecord parseNpcTemplate(JsonCursor& value) {
  NpcTemplateRecord record;
  value.parseObject([&](const std::string& key, JsonCursor& field) {
    if(key == "npc_instance") {
      record.npcInstance = field.parseScalarString();
    } else if(key == "display_name") {
      record.displayName = field.parseScalarString();
    } else if(key == "guild") {
      record.guild = field.parseScalarString();
    } else if(key == "routine_symbol") {
      record.routineSymbol = field.parseScalarString();
    } else if(key == "perception_symbol") {
      record.perceptionSymbol = field.parseScalarString();
    } else {
      field.skipValue();
    }
  });
  return record;
}

[[nodiscard]] ItemTemplateRecord parseItemTemplate(JsonCursor& value) {
  ItemTemplateRecord record;
  value.parseObject([&](const std::string& key, JsonCursor& field) {
    if(key == "item_instance") {
      record.itemInstance = field.parseScalarString();
    } else if(key == "display_name") {
      record.displayName = field.parseScalarString();
    } else if(key == "item_category") {
      record.itemCategory = field.parseScalarString();
    } else {
      field.skipValue();
    }
  });
  return record;
}

[[nodiscard]] RoutineRecord parseRoutine(JsonCursor& value) {
  RoutineRecord record;
  value.parseObject([&](const std::string& key, JsonCursor& field) {
    if(key == "npc_instance") {
      record.npcInstance = field.parseScalarString();
    } else if(key == "routine_symbol") {
      record.routineSymbol = field.parseScalarString();
    } else if(key == "action_symbol") {
      record.actionSymbol = field.parseScalarString();
    } else if(key == "target_point_key") {
      record.targetPointKey = field.parseScalarString();
    } else {
      field.skipValue();
    }
  });
  return record;
}

[[nodiscard]] PerceptionBindingRecord parsePerceptionBinding(JsonCursor& value) {
  PerceptionBindingRecord record;
  value.parseObject([&](const std::string& key, JsonCursor& field) {
    if(key == "owner_symbol") {
      record.ownerSymbol = field.parseScalarString();
    } else if(key == "owner_kind") {
      record.ownerKind = field.parseScalarString();
    } else if(key == "perception_kind") {
      record.perceptionKind = field.parseScalarString();
    } else if(key == "function_symbol") {
      record.functionSymbol = field.parseScalarString();
    } else {
      field.skipValue();
    }
  });
  return record;
}

[[nodiscard]] DialogInfoRecord parseDialogInfo(JsonCursor& value) {
  DialogInfoRecord record;
  value.parseObject([&](const std::string& key, JsonCursor& field) {
    if(key == "info_symbol") {
      record.infoSymbol = field.parseScalarString();
    } else if(key == "npc_instance") {
      record.npcInstance = field.parseScalarString();
    } else if(key == "condition_symbol") {
      record.conditionSymbol = field.parseScalarString();
    } else if(key == "information_symbol") {
      record.informationSymbol = field.parseScalarString();
    } else if(key == "permanent" || key == "permanent_flag") {
      record.permanent = field.parseBool(false);
    } else if(key == "important" || key == "important_flag") {
      record.important = field.parseBool(false);
    } else if(key == "trade" || key == "trade_flag") {
      record.trade = field.parseBool(false);
    } else {
      field.skipValue();
    }
  });
  return record;
}

[[nodiscard]] DialogOutputRecord parseDialogOutput(JsonCursor& value) {
  DialogOutputRecord record;
  value.parseObject([&](const std::string& key, JsonCursor& field) {
    if(key == "output_name") {
      record.outputName = field.parseScalarString();
    } else if(key == "text" || key == "text_value") {
      record.text = field.parseScalarString();
    } else if(key == "audio_ref") {
      record.audioRef = field.parseScalarString();
    } else {
      field.skipValue();
    }
  });
  return record;
}

[[nodiscard]] bool hasAllFlags(const SectionFlags& flags) noexcept {
  return flags.worldZenEntities && flags.waypointEdges && flags.npcTemplates && flags.itemTemplates
      && flags.routines && flags.perceptionBindings && flags.dialogInfos && flags.dialogOutputs;
}

void addMissingFlagErrors(const SectionFlags& flags, std::string_view prefix, std::vector<std::string>& errors) {
  if(!flags.worldZenEntities) {
    errors.push_back(std::string(prefix) + " world_zen_entities/world_zen_entity_count");
  }
  if(!flags.waypointEdges) {
    errors.push_back(std::string(prefix) + " waypoint_edges/waypoint_edge_count");
  }
  if(!flags.npcTemplates) {
    errors.push_back(std::string(prefix) + " npc_templates/npc_template_count");
  }
  if(!flags.itemTemplates) {
    errors.push_back(std::string(prefix) + " item_templates/item_template_count");
  }
  if(!flags.routines) {
    errors.push_back(std::string(prefix) + " routines/routine_count");
  }
  if(!flags.perceptionBindings) {
    errors.push_back(std::string(prefix) + " perception_bindings/perception_binding_count");
  }
  if(!flags.dialogInfos) {
    errors.push_back(std::string(prefix) + " dialog_infos/dialog_info_count");
  }
  if(!flags.dialogOutputs) {
    errors.push_back(std::string(prefix) + " dialog_outputs/dialog_output_count");
  }
}

void addCountMismatch(std::vector<std::string>& errors, std::string_view name, std::size_t expected, std::size_t actual) {
  if(expected != actual) {
    errors.push_back(std::string(name) + " summary=" + std::to_string(expected) + " actual=" + std::to_string(actual));
  }
}

template<class Map>
void addUniqueIndex(Map& map, std::string key, std::size_t index, std::size_t& duplicateCount) {
  if(key.empty()) {
    return;
  }
  const auto [_, inserted] = map.emplace(std::move(key), index);
  if(!inserted) {
    ++duplicateCount;
  }
}

template<class Map>
void addMultiIndex(Map& map, std::string key, std::size_t index) {
  if(key.empty()) {
    return;
  }
  map[std::move(key)].push_back(index);
}

void addWarningIfPositive(std::vector<std::string>& warnings, std::size_t count, std::string_view message) {
  if(count != 0) {
    warnings.push_back(std::string(message) + std::to_string(count));
  }
}

void buildIndexes(RuntimeReadModel& model) {
  model.worldZenEntityByKey.reserve(model.worldZenEntities.size());
  model.waypointEdgeByRoute.reserve(model.waypointEdges.size());
  model.npcTemplateByInstance.reserve(model.npcTemplates.size());
  model.itemTemplateByInstance.reserve(model.itemTemplates.size());
  model.routinesBySymbol.reserve(model.routines.size());
  model.perceptionBindingsByKind.reserve(model.perceptionBindings.size());
  model.dialogInfoBySymbol.reserve(model.dialogInfos.size());
  model.dialogOutputByName.reserve(model.dialogOutputs.size());

  std::size_t duplicateWorldEntities = 0;
  std::size_t duplicateWaypointEdges = 0;
  std::size_t duplicateNpcTemplates = 0;
  std::size_t duplicateItemTemplates = 0;
  std::size_t duplicateDialogInfos = 0;
  std::size_t duplicateDialogOutputs = 0;
  std::size_t routinesWithoutNpc = 0;
  std::size_t perceptionBindingsWithoutOwner = 0;

  for(std::size_t i = 0; i < model.worldZenEntities.size(); ++i) {
    const auto& record = model.worldZenEntities[i];
    addUniqueIndex(model.worldZenEntityByKey, makeWorldZenEntityIndexKey(record.worldName, record.entityKind, record.entityKey), i, duplicateWorldEntities);
  }
  for(std::size_t i = 0; i < model.waypointEdges.size(); ++i) {
    const auto& record = model.waypointEdges[i];
    addUniqueIndex(model.waypointEdgeByRoute, makeWaypointEdgeRouteIndexKey(record.worldName, record.fromWaypointKey, record.toWaypointKey), i, duplicateWaypointEdges);
  }
  for(std::size_t i = 0; i < model.npcTemplates.size(); ++i) {
    addUniqueIndex(model.npcTemplateByInstance, model.npcTemplates[i].npcInstance, i, duplicateNpcTemplates);
  }
  for(std::size_t i = 0; i < model.itemTemplates.size(); ++i) {
    addUniqueIndex(model.itemTemplateByInstance, model.itemTemplates[i].itemInstance, i, duplicateItemTemplates);
  }
  for(std::size_t i = 0; i < model.routines.size(); ++i) {
    const auto& record = model.routines[i];
    if(record.npcInstance.empty()) {
      ++routinesWithoutNpc;
    } else {
      addMultiIndex(model.routinesByNpcInstance, record.npcInstance, i);
    }
    addMultiIndex(model.routinesBySymbol, record.routineSymbol, i);
  }
  for(std::size_t i = 0; i < model.perceptionBindings.size(); ++i) {
    const auto& record = model.perceptionBindings[i];
    addMultiIndex(model.perceptionBindingsByKind, record.perceptionKind, i);
    if(record.ownerSymbol.empty()) {
      ++perceptionBindingsWithoutOwner;
    } else {
      addMultiIndex(model.perceptionBindingsByOwner, record.ownerSymbol, i);
    }
  }
  for(std::size_t i = 0; i < model.dialogInfos.size(); ++i) {
    addUniqueIndex(model.dialogInfoBySymbol, model.dialogInfos[i].infoSymbol, i, duplicateDialogInfos);
  }
  for(std::size_t i = 0; i < model.dialogOutputs.size(); ++i) {
    addUniqueIndex(model.dialogOutputByName, model.dialogOutputs[i].outputName, i, duplicateDialogOutputs);
  }

  auto& warnings = model.inspection.warnings;
  addWarningIfPositive(warnings, duplicateWorldEntities, "duplicate world ZEN entity index keys: ");
  addWarningIfPositive(warnings, duplicateWaypointEdges, "duplicate waypoint route index keys: ");
  addWarningIfPositive(warnings, duplicateNpcTemplates, "duplicate NPC template index keys: ");
  addWarningIfPositive(warnings, duplicateItemTemplates, "duplicate item template index keys: ");
  addWarningIfPositive(warnings, duplicateDialogInfos, "duplicate dialog info index keys: ");
  addWarningIfPositive(warnings, duplicateDialogOutputs, "duplicate dialog output index keys: ");
  addWarningIfPositive(warnings, routinesWithoutNpc, "routines not indexed by npc_instance: ");
  addWarningIfPositive(warnings, perceptionBindingsWithoutOwner, "perception bindings not indexed by owner_symbol: ");
}

[[nodiscard]] RuntimeReadModel parseRuntimeReadModel(const std::filesystem::path& path, bool materialize) {
  RuntimeReadModel model;
  model.inspection.sourcePath = path;

  const std::string text = readFile(path);
  JsonCursor cursor(text);
  cursor.parseObject([&](const std::string& key, JsonCursor& value) {
    auto parseOrCount = [&](auto& records, auto parser, auto& count, bool& present) {
      if(materialize) {
        count = value.parseArray([&](JsonCursor& item) {
          records.push_back(parser(item));
        });
      } else {
        count = value.countArrayElements();
      }
      present = true;
    };

    if(key == "schema") {
      model.inspection.schema = value.parseString();
    } else if(key == "content_revision_key") {
      model.inspection.contentRevisionKey = value.parseString();
    } else if(key == "game_code") {
      model.inspection.gameCode = value.parseString();
    } else if(key == "payload_sha256") {
      model.inspection.payloadSha256 = value.parseString();
    } else if(key == "source_kind") {
      model.inspection.sourceKind = value.parseString();
    } else if(key == "snapshot_path") {
      model.inspection.snapshotPath = value.parseString();
    } else if(key == "summary") {
      value.parseObject([&](const std::string& summaryKey, JsonCursor& summaryValue) {
        parseSummaryField(summaryKey, summaryValue, model.inspection.summary, model.inspection.summaryFields);
      });
    } else if(key == "world_zen_entities") {
      parseOrCount(model.worldZenEntities, parseWorldZenEntity, model.inspection.sectionCounts.worldZenEntityCount, model.inspection.sectionArrays.worldZenEntities);
    } else if(key == "waypoint_edges") {
      parseOrCount(model.waypointEdges, parseWaypointEdge, model.inspection.sectionCounts.waypointEdgeCount, model.inspection.sectionArrays.waypointEdges);
    } else if(key == "npc_templates") {
      parseOrCount(model.npcTemplates, parseNpcTemplate, model.inspection.sectionCounts.npcTemplateCount, model.inspection.sectionArrays.npcTemplates);
    } else if(key == "item_templates") {
      parseOrCount(model.itemTemplates, parseItemTemplate, model.inspection.sectionCounts.itemTemplateCount, model.inspection.sectionArrays.itemTemplates);
    } else if(key == "routines") {
      parseOrCount(model.routines, parseRoutine, model.inspection.sectionCounts.routineCount, model.inspection.sectionArrays.routines);
    } else if(key == "perception_bindings") {
      parseOrCount(model.perceptionBindings, parsePerceptionBinding, model.inspection.sectionCounts.perceptionBindingCount, model.inspection.sectionArrays.perceptionBindings);
    } else if(key == "dialog_infos") {
      parseOrCount(model.dialogInfos, parseDialogInfo, model.inspection.sectionCounts.dialogInfoCount, model.inspection.sectionArrays.dialogInfos);
    } else if(key == "dialog_outputs") {
      parseOrCount(model.dialogOutputs, parseDialogOutput, model.inspection.sectionCounts.dialogOutputCount, model.inspection.sectionArrays.dialogOutputs);
    } else {
      value.skipValue();
    }
  });
  if(!cursor.finished()) {
    throw std::runtime_error("trailing data after runtime read model JSON");
  }
  if(!hasAllFlags(model.inspection.summaryFields)) {
    model.inspection.warnings.emplace_back("summary does not contain all known section counters");
  }
  if(!hasAllFlags(model.inspection.sectionArrays)) {
    model.inspection.warnings.emplace_back("read model does not contain all known section arrays");
  }
  if(materialize) {
    buildIndexes(model);
  }
  return model;
}

template<class Records, class KeyFn, class Map>
[[nodiscard]] bool firstIndexed(const Records& records, KeyFn&& keyFn, const Map& map) {
  for(const auto& record : records) {
    auto key = keyFn(record);
    if(!key.empty()) {
      return map.find(key) != map.end();
    }
  }
  return false;
}

} // namespace

ReadModelInspection inspectRuntimeReadModel(const std::filesystem::path& path) {
  return parseRuntimeReadModel(path, false).inspection;
}

std::vector<std::string> validateRuntimeReadModel(const ReadModelInspection& model) {
  std::vector<std::string> errors;
  if(model.schema != ExpectedSchema) {
    errors.push_back("schema must be " + std::string(ExpectedSchema));
  }
  if(model.contentRevisionKey.empty()) {
    errors.emplace_back("content_revision_key is missing");
  }
  if(model.gameCode.empty()) {
    errors.emplace_back("game_code is missing");
  }
  if(!isHexSha256(model.payloadSha256)) {
    errors.emplace_back("payload_sha256 must be 64 lowercase hex characters");
  }
  addMissingFlagErrors(model.summaryFields, "missing summary field for", errors);
  addMissingFlagErrors(model.sectionArrays, "missing section array for", errors);
  addCountMismatch(errors, "world_zen_entities", model.summary.worldZenEntityCount, model.sectionCounts.worldZenEntityCount);
  addCountMismatch(errors, "waypoint_edges", model.summary.waypointEdgeCount, model.sectionCounts.waypointEdgeCount);
  addCountMismatch(errors, "npc_templates", model.summary.npcTemplateCount, model.sectionCounts.npcTemplateCount);
  addCountMismatch(errors, "item_templates", model.summary.itemTemplateCount, model.sectionCounts.itemTemplateCount);
  addCountMismatch(errors, "routines", model.summary.routineCount, model.sectionCounts.routineCount);
  addCountMismatch(errors, "perception_bindings", model.summary.perceptionBindingCount, model.sectionCounts.perceptionBindingCount);
  addCountMismatch(errors, "dialog_infos", model.summary.dialogInfoCount, model.sectionCounts.dialogInfoCount);
  addCountMismatch(errors, "dialog_outputs", model.summary.dialogOutputCount, model.sectionCounts.dialogOutputCount);
  return errors;
}

RuntimeReadModel loadRuntimeReadModel(const std::filesystem::path& path) {
  return parseRuntimeReadModel(path, true);
}

RuntimeIndexCounts indexCounts(const RuntimeReadModel& model) noexcept {
  return {
    model.worldZenEntityByKey.size(),
    model.waypointEdgeByRoute.size(),
    model.npcTemplateByInstance.size(),
    model.itemTemplateByInstance.size(),
    model.routinesByNpcInstance.size(),
    model.routinesBySymbol.size(),
    model.perceptionBindingsByKind.size(),
    model.perceptionBindingsByOwner.size(),
    model.dialogInfoBySymbol.size(),
    model.dialogOutputByName.size(),
  };
}

RuntimeLookupChecks runDeterministicLookupChecks(const RuntimeReadModel& model) {
  RuntimeLookupChecks checks;
  checks.firstWorldZenEntityByKey = firstIndexed(model.worldZenEntities, [](const WorldZenEntityRecord& record) {
    return makeWorldZenEntityIndexKey(record.worldName, record.entityKind, record.entityKey);
  }, model.worldZenEntityByKey);
  checks.firstWaypointEdgeByRoute = firstIndexed(model.waypointEdges, [](const WaypointEdgeRecord& record) {
    return makeWaypointEdgeRouteIndexKey(record.worldName, record.fromWaypointKey, record.toWaypointKey);
  }, model.waypointEdgeByRoute);
  checks.firstNpcTemplateByInstance = firstIndexed(model.npcTemplates, [](const NpcTemplateRecord& record) {
    return record.npcInstance;
  }, model.npcTemplateByInstance);
  checks.firstItemTemplateByInstance = firstIndexed(model.itemTemplates, [](const ItemTemplateRecord& record) {
    return record.itemInstance;
  }, model.itemTemplateByInstance);
  checks.firstRoutineByNpcInstance = firstIndexed(model.routines, [](const RoutineRecord& record) {
    return record.npcInstance;
  }, model.routinesByNpcInstance);
  checks.firstRoutineBySymbol = firstIndexed(model.routines, [](const RoutineRecord& record) {
    return record.routineSymbol;
  }, model.routinesBySymbol);
  checks.firstPerceptionBindingByKind = firstIndexed(model.perceptionBindings, [](const PerceptionBindingRecord& record) {
    return record.perceptionKind;
  }, model.perceptionBindingsByKind);
  checks.firstPerceptionBindingByOwner = firstIndexed(model.perceptionBindings, [](const PerceptionBindingRecord& record) {
    return record.ownerSymbol;
  }, model.perceptionBindingsByOwner);
  checks.firstDialogInfoBySymbol = firstIndexed(model.dialogInfos, [](const DialogInfoRecord& record) {
    return record.infoSymbol;
  }, model.dialogInfoBySymbol);
  checks.firstDialogOutputByName = firstIndexed(model.dialogOutputs, [](const DialogOutputRecord& record) {
    return record.outputName;
  }, model.dialogOutputByName);
  return checks;
}

} // namespace Mmo::RuntimeReadModel
