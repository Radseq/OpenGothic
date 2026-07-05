// Internal implementation partition for mmo_udp_server.cpp.
// Kept include-based during the monolith split to preserve behavior.

[[nodiscard]] bool startsWith(std::string_view text, std::string_view prefix) noexcept {
  return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

[[nodiscard]] std::optional<int> parseInt(std::string_view text) noexcept {
  int value = 0;
  auto r = std::from_chars(text.data(), text.data() + text.size(), value);
  if(r.ec != std::errc{} || r.ptr != text.data() + text.size())
    return std::nullopt;
  return value;
}

[[nodiscard]] std::optional<std::int64_t> parseI64(std::string_view text) noexcept {
  std::int64_t value = 0;
  auto r = std::from_chars(text.data(), text.data() + text.size(), value);
  if(r.ec != std::errc{} || r.ptr != text.data() + text.size())
    return std::nullopt;
  return value;
}

[[nodiscard]] std::optional<double> parseDouble(std::string_view text) noexcept {
  char* end = nullptr;
  const std::string copy(text);
  const double value = std::strtod(copy.c_str(), &end);
  if(end == nullptr || *end != '\0')
    return std::nullopt;
  return value;
}

[[nodiscard]] bool finiteCoord(double value) noexcept {
  return std::isfinite(value) && std::abs(value) <= 10000000.0;
}

[[nodiscard]] double distanceSquared3d(double ax, double ay, double az,
                                       double bx, double by, double bz) noexcept {
  const double dx = ax - bx;
  const double dy = ay - by;
  const double dz = az - bz;
  return dx * dx + dy * dy + dz * dz;
}

[[nodiscard]] double distance3d(double ax, double ay, double az,
                                double bx, double by, double bz) noexcept {
  return std::sqrt(distanceSquared3d(ax, ay, az, bx, by, bz));
}

[[nodiscard]] double finiteOrThrow(double value, std::string_view field) {
  if(!std::isfinite(value))
    throw std::runtime_error("non-finite numeric payload field: " + std::string(field));
  return value;
}

[[nodiscard]] std::string trim(std::string_view text) {
  while(!text.empty() && static_cast<unsigned char>(text.front()) <= ' ')
    text.remove_prefix(1);
  while(!text.empty() && static_cast<unsigned char>(text.back()) <= ' ')
    text.remove_suffix(1);
  return std::string(text);
}

[[nodiscard]] std::string jsonEscape(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 8);
  out.push_back('"');
  for(char ch : text) {
    switch(ch) {
      case '\\': out += "\\\\"; break;
      case '"': out += "\\\""; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if(static_cast<unsigned char>(ch) < 0x20)
          out.push_back(' ');
        else
          out.push_back(ch);
        break;
    }
  }
  out.push_back('"');
  return out;
}

[[nodiscard]] std::optional<std::size_t> findFieldValueStart(std::string_view json, std::string_view key) {
  const auto needle = jsonEscape(key);
  auto pos = json.find(needle);
  while(pos != std::string_view::npos) {
    pos += needle.size();
    while(pos < json.size() && static_cast<unsigned char>(json[pos]) <= ' ')
      ++pos;
    if(pos < json.size() && json[pos] == ':') {
      ++pos;
      while(pos < json.size() && static_cast<unsigned char>(json[pos]) <= ' ')
        ++pos;
      return pos;
    }
    pos = json.find(needle, pos);
  }
  return std::nullopt;
}


[[nodiscard]] int hexNibble(char c) noexcept {
  if(c >= '0' && c <= '9')
    return c - '0';
  if(c >= 'a' && c <= 'f')
    return 10 + c - 'a';
  if(c >= 'A' && c <= 'F')
    return 10 + c - 'A';
  return -1;
}

[[nodiscard]] bool readJsonHex4(std::string_view text, std::size_t pos, std::uint32_t& out) noexcept {
  if(pos + 4 > text.size())
    return false;
  std::uint32_t value = 0;
  for(std::size_t i = 0; i < 4; ++i) {
    const int nibble = hexNibble(text[pos + i]);
    if(nibble < 0)
      return false;
    value = (value << 4U) | static_cast<std::uint32_t>(nibble);
  }
  out = value;
  return true;
}

void appendUtf8(std::string& out, std::uint32_t cp) {
  if(cp <= 0x7FU) {
    out.push_back(static_cast<char>(cp));
  } else if(cp <= 0x7FFU) {
    out.push_back(static_cast<char>(0xC0U | (cp >> 6U)));
    out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
  } else if(cp <= 0xFFFFU) {
    out.push_back(static_cast<char>(0xE0U | (cp >> 12U)));
    out.push_back(static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
  } else if(cp <= 0x10FFFFU) {
    out.push_back(static_cast<char>(0xF0U | (cp >> 18U)));
    out.push_back(static_cast<char>(0x80U | ((cp >> 12U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
  }
}

[[nodiscard]] bool appendJsonEscape(std::string& out, std::string_view json, std::size_t& i) {
  if(i + 1 >= json.size())
    return false;
  const char esc = json[++i];
  switch(esc) {
    case '"': out.push_back('"'); return true;
    case '\\': out.push_back('\\'); return true;
    case '/': out.push_back('/'); return true;
    case 'b': out.push_back('\b'); return true;
    case 'f': out.push_back('\f'); return true;
    case 'n': out.push_back('\n'); return true;
    case 'r': out.push_back('\r'); return true;
    case 't': out.push_back('\t'); return true;
    case 'u': {
      std::uint32_t first = 0;
      if(!readJsonHex4(json, i + 1, first))
        return false;
      i += 4;
      std::uint32_t cp = first;
      if(first >= 0xD800U && first <= 0xDBFFU) {
        if(i + 6 >= json.size() || json[i + 1] != '\\' || json[i + 2] != 'u')
          return false;
        std::uint32_t second = 0;
        if(!readJsonHex4(json, i + 3, second) || second < 0xDC00U || second > 0xDFFFU)
          return false;
        i += 6;
        cp = 0x10000U + (((first - 0xD800U) << 10U) | (second - 0xDC00U));
      } else if(first >= 0xDC00U && first <= 0xDFFFU) {
        return false;
      }
      appendUtf8(out, cp);
      return true;
    }
    default:
      return false;
  }
}

[[nodiscard]] std::optional<std::string> jsonStringField(std::string_view json, std::string_view key) {
  auto pos = findFieldValueStart(json, key);
  if(!pos || *pos >= json.size() || json[*pos] != '"')
    return std::nullopt;
  std::string out;
  for(std::size_t i = *pos + 1; i < json.size(); ++i) {
    const char ch = json[i];
    if(ch == '"')
      return out;
    if(ch == '\\') {
      if(!appendJsonEscape(out, json, i))
        return std::nullopt;
      continue;
    }
    out.push_back(ch);
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::string> jsonNumberTextField(std::string_view json, std::string_view key) {
  auto pos = findFieldValueStart(json, key);
  if(!pos || *pos >= json.size())
    return std::nullopt;
  std::size_t end = *pos;
  while(end < json.size()) {
    const char ch = json[end];
    if((ch >= '0' && ch <= '9') || ch == '-' || ch == '+' || ch == '.' || ch == 'e' || ch == 'E')
      ++end;
    else
      break;
  }
  if(end == *pos)
    return std::nullopt;
  return std::string(json.substr(*pos, end - *pos));
}

[[nodiscard]] double requiredJsonDouble(std::string_view json, std::string_view key) {
  auto text = jsonNumberTextField(json, key);
  if(!text)
    throw std::runtime_error("missing numeric payload field: " + std::string(key));
  auto value = parseDouble(*text);
  if(!value)
    throw std::runtime_error("invalid numeric payload field: " + std::string(key));
  return finiteOrThrow(*value, key);
}

[[nodiscard]] double optionalJsonDouble(std::string_view json, std::string_view key, double fallback) {
  auto text = jsonNumberTextField(json, key);
  if(!text)
    return fallback;
  auto value = parseDouble(*text);
  if(!value)
    return fallback;
  return finiteOrThrow(*value, key);
}

[[nodiscard]] std::string optionalJsonDoubleSql(std::string_view json,
                                                std::string_view key0,
                                                std::string_view key1 = {},
                                                std::string_view key2 = {}) {
  const std::array<std::string_view, 3> keys {key0, key1, key2};
  for(const auto key : keys) {
    if(key.empty())
      continue;
    auto text = jsonNumberTextField(json, key);
    if(!text)
      continue;
    auto value = parseDouble(*text);
    if(value)
      return std::to_string(finiteOrThrow(*value, key));
  }
  return "NULL";
}

[[nodiscard]] std::optional<std::string_view> jsonObjectField(std::string_view json, std::string_view key) {
  auto pos = findFieldValueStart(json, key);
  if(!pos || *pos >= json.size() || json[*pos] != '{')
    return std::nullopt;

  bool inString = false;
  bool escaped = false;
  int depth = 0;
  for(std::size_t i = *pos; i < json.size(); ++i) {
    const char ch = json[i];
    if(inString) {
      if(escaped) {
        escaped = false;
      } else if(ch == '\\') {
        escaped = true;
      } else if(ch == '"') {
        inString = false;
      }
      continue;
    }
    if(ch == '"') {
      inString = true;
      continue;
    }
    if(ch == '{') {
      ++depth;
      continue;
    }
    if(ch == '}') {
      --depth;
      if(depth == 0)
        return json.substr(*pos, i - *pos + 1);
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::string optionalJsonPositionDoubleSql(std::string_view json,
                                                        std::string_view nestedKey,
                                                        std::string_view flatKey0,
                                                        std::string_view flatKey1,
                                                        std::string_view flatKey2) {
  if(auto item = jsonObjectField(json, "item_position")) {
    if(auto text = jsonNumberTextField(*item, nestedKey)) {
      if(auto value = parseDouble(*text))
        return std::to_string(finiteOrThrow(*value, nestedKey));
    }
  }
  if(auto actor = jsonObjectField(json, "actor_position")) {
    if(auto text = jsonNumberTextField(*actor, nestedKey)) {
      if(auto value = parseDouble(*text))
        return std::to_string(finiteOrThrow(*value, nestedKey));
    }
  }
  return optionalJsonDoubleSql(json, flatKey0, flatKey1, flatKey2);
}

[[nodiscard]] std::optional<double> optionalJsonPositionDouble(std::string_view json,
                                                               std::string_view nestedKey,
                                                               std::string_view flatKey0,
                                                               std::string_view flatKey1,
                                                               std::string_view flatKey2) {
  if(auto item = jsonObjectField(json, "item_position")) {
    if(auto text = jsonNumberTextField(*item, nestedKey)) {
      if(auto value = parseDouble(*text))
        return finiteOrThrow(*value, nestedKey);
    }
  }
  if(auto actor = jsonObjectField(json, "actor_position")) {
    if(auto text = jsonNumberTextField(*actor, nestedKey)) {
      if(auto value = parseDouble(*text))
        return finiteOrThrow(*value, nestedKey);
    }
  }

  const std::array<std::string_view, 3> keys {flatKey0, flatKey1, flatKey2};
  for(const auto key : keys) {
    if(key.empty())
      continue;
    if(auto text = jsonNumberTextField(json, key)) {
      if(auto value = parseDouble(*text))
        return finiteOrThrow(*value, key);
    }
  }
  return std::nullopt;
}


struct JsonVec3 final {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

[[nodiscard]] std::optional<JsonVec3> optionalJsonVec3(std::string_view json, std::string_view objectKey) {
  const auto object = jsonObjectField(json, objectKey);
  if(!object)
    return std::nullopt;

  const auto xText = jsonNumberTextField(*object, "x");
  const auto yText = jsonNumberTextField(*object, "y");
  const auto zText = jsonNumberTextField(*object, "z");
  if(!xText || !yText || !zText)
    return std::nullopt;

  const auto x = parseDouble(*xText);
  const auto y = parseDouble(*yText);
  const auto z = parseDouble(*zText);
  if(!x || !y || !z)
    return std::nullopt;

  return JsonVec3{finiteOrThrow(*x, "x"), finiteOrThrow(*y, "y"), finiteOrThrow(*z, "z")};
}

[[nodiscard]] std::optional<Mmo::Server::Gameplay::Vec3> optionalFlatGameplayVec3(std::string_view json,
                                                                                  std::string_view xKey,
                                                                                  std::string_view yKey,
                                                                                  std::string_view zKey) {
  const auto xText = jsonNumberTextField(json, xKey);
  const auto yText = jsonNumberTextField(json, yKey);
  const auto zText = jsonNumberTextField(json, zKey);
  if(!xText && !yText && !zText)
    return std::nullopt;
  if(!xText || !yText || !zText)
    return Mmo::Server::Gameplay::Vec3 {
      std::numeric_limits<double>::quiet_NaN(),
      std::numeric_limits<double>::quiet_NaN(),
      std::numeric_limits<double>::quiet_NaN(),
    };

  const auto x = parseDouble(*xText);
  const auto y = parseDouble(*yText);
  const auto z = parseDouble(*zText);
  if(!x || !y || !z)
    return Mmo::Server::Gameplay::Vec3 {
      std::numeric_limits<double>::quiet_NaN(),
      std::numeric_limits<double>::quiet_NaN(),
      std::numeric_limits<double>::quiet_NaN(),
    };

  return Mmo::Server::Gameplay::Vec3 {*x, *y, *z};
}

[[nodiscard]] std::int64_t requiredJsonI64(std::string_view json, std::string_view key) {
  auto text = jsonNumberTextField(json, key);
  if(!text)
    throw std::runtime_error("missing integer payload field: " + std::string(key));
  auto value = parseI64(*text);
  if(!value)
    throw std::runtime_error("invalid integer payload field: " + std::string(key));
  return *value;
}

[[nodiscard]] std::int64_t optionalJsonI64(std::string_view json, std::string_view key, std::int64_t fallback) {
  auto text = jsonNumberTextField(json, key);
  if(!text)
    return fallback;
  auto value = parseI64(*text);
  if(!value)
    return fallback;
  return *value;
}

[[nodiscard]] std::string optionalJsonString(std::string_view json, std::string_view key, std::string fallback = {}) {
  return jsonStringField(json, key).value_or(std::move(fallback));
}

[[nodiscard]] std::uint64_t packetServerTick(const Mmo::Net::ClientActionPacket& packet) {
  const auto payloadServerTick = optionalJsonI64(packet.payloadJson, "server_tick", 0);
  if(packet.clientTick != 0)
    return packet.clientTick;
  return payloadServerTick > 0 ? static_cast<std::uint64_t>(payloadServerTick) : 0u;
}

[[nodiscard]] std::optional<bool> jsonBoolField(std::string_view json, std::string_view key) {
  auto pos = findFieldValueStart(json, key);
  if(!pos)
    return std::nullopt;
  const auto rest = json.substr(*pos);
  if(startsWith(rest, "true"))
    return true;
  if(startsWith(rest, "false"))
    return false;
  return std::nullopt;
}

[[nodiscard]] bool optionalJsonBool(std::string_view json, std::string_view key, bool fallback) {
  return jsonBoolField(json, key).value_or(fallback);
}

void appendJsonField(std::string& out, std::string_view key, std::string_view value) {
  out.push_back(',');
  out += jsonEscape(key);
  out.push_back(':');
  out += jsonEscape(value);
}

void appendJsonRawField(std::string& out, std::string_view key, std::string_view value) {
  out.push_back(',');
  out += jsonEscape(key);
  out.push_back(':');
  out += value.empty() ? "null" : std::string(value);
}

void appendJsonRawFieldBeforeFinalObjectBrace(std::string& out, std::string_view key, std::string_view value) {
  if(out.find(jsonEscape(key)) != std::string::npos)
    return;
  while(!out.empty() && std::isspace(static_cast<unsigned char>(out.back())))
    out.pop_back();
  if(out.empty() || out.back() != '}')
    return;
  out.pop_back();
  appendJsonRawField(out, key, value);
  out.push_back('}');
}

void appendJsonNumberField(std::string& out, std::string_view key, std::uint64_t value) {
  out.push_back(',');
  out += jsonEscape(key);
  out.push_back(':');
  out += std::to_string(value);
}

void appendPayloadStringAlias(std::string& out, std::string_view payload, std::string_view outputKey, std::string_view payloadKey) {
  if(auto v = jsonStringField(payload, payloadKey))
    appendJsonField(out, outputKey, *v);
}

void appendPayloadNumberAlias(std::string& out, std::string_view payload, std::string_view outputKey, std::string_view payloadKey) {
  if(auto v = jsonNumberTextField(payload, payloadKey))
    appendJsonRawField(out, outputKey, *v);
}

void appendPayloadBoolAlias(std::string& out, std::string_view payload, std::string_view outputKey, std::string_view payloadKey) {
  if(auto v = jsonBoolField(payload, payloadKey))
    appendJsonRawField(out, outputKey, *v ? "true" : "false");
}

[[nodiscard]] std::string equipmentSlotName(std::string_view raw) {
  auto slot = parseInt(raw);
  if(!slot)
    return "unknown";
  if(*slot == 1)
    return "weapon_melee";
  if(*slot == 2)
    return "weapon_ranged";
  return "unknown";
}

