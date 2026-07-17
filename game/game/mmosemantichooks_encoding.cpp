#include "mmosemantichooks_internal.h"

#include <array>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "mmoclientadapter.h"
#include "mmoclientbridge.h"
#include "world/world.h"
#include "world/objects/interactive.h"
#include "world/objects/item.h"
#include "world/objects/npc.h"
#include "world/waypoint.h"
#include "commandline.h"
#include "utils/versioninfo.h"

namespace Mmo::Hooks::Detail {


void appendUInt(std::string& out, std::uint64_t v) {
  char buf[32] = {};
  auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), v);
  if(ec == std::errc{})
    out.append(buf, ptr);
  else
    out.push_back('0');
}

void appendInt(std::string& out, std::int64_t v) {
  char buf[32] = {};
  auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), v);
  if(ec == std::errc{})
    out.append(buf, ptr);
  else
    out.push_back('0');
}

void appendFloat(std::string& out, float v) {
  char buf[48] = {};
  auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), v);
  if(ec == std::errc{})
    out.append(buf, ptr);
  else
    out.push_back('0');
}

void appendBool(std::string& out, bool v) {
  out.append(v ? "true" : "false");
}

static void appendHex2(std::string& out, unsigned char v) {
  constexpr char hex[] = "0123456789ABCDEF";
  out.push_back(hex[(v >> 4) & 0x0F]);
  out.push_back(hex[v & 0x0F]);
}

void appendEscaped(std::string& out, std::string_view v) {
  // Semantic envelopes are sent over UDP as UTF-8 JSON. Gothic/Zen labels can
  // still contain legacy single-byte text, so raw bytes >=0x80 are escaped.
  // Stable identity is numeric/key-based; display names are diagnostic labels.
  out.push_back('"');
  for(unsigned char ch : v) {
    switch(ch) {
      case '\\': out.append("\\\\"); break;
      case '"':  out.append("\\\""); break;
      case '\n': out.append("\\n");  break;
      case '\r': out.append("\\r");  break;
      case '\t': out.append("\\t");  break;
      default:
        if(ch < 0x20) {
          out.push_back(' ');
          }
        else if(ch < 0x80) {
          out.push_back(static_cast<char>(ch));
          }
        else {
          out.append("\\u00");
          appendHex2(out, ch);
          }
        break;
      }
    }
  out.push_back('"');
}

void appendScriptContext(std::string& out, std::uint32_t scriptFunctionSymbol, std::string_view scriptFunctionName) {
  out.append(",\"script_function_symbol\":");
  appendUInt(out, scriptFunctionSymbol);
  out.append(",\"script_function_name\":");
  appendEscaped(out, scriptFunctionName);
}

void appendWorld(std::string& out, const World& world) {
  out.append(",\"world\":");
  appendEscaped(out, world.name());
  out.append(",\"client_tick\":");
  appendUInt(out, world.tickCount());
}


} // namespace Mmo::Hooks::Detail
