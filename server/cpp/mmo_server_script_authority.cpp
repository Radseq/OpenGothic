#include "mmo_server_script_authority.h"

#include "mmo_server_story_authority.h"

#include <initializer_list>

namespace Mmo::Server::Script {

namespace {

[[nodiscard]] std::string firstNonEmpty(std::initializer_list<std::string_view> values) {
  for(const auto value : values) {
    if(!value.empty())
      return std::string(value);
  }
  return {};
}

[[nodiscard]] std::string fallbackScriptIntKey(std::int64_t symbolIndex, std::int64_t valueIndex) {
  return "script-int:" + std::to_string(symbolIndex) + ":" + std::to_string(valueIndex);
}

} // namespace

SetIntCommand buildSetIntCommand(const SetIntInput& input) {
  SetIntCommand out;
  out.scriptKey = firstNonEmpty({input.scriptKey, input.globalKey, input.symbolName, input.targetKey});
  if(out.scriptKey.empty())
    out.scriptKey = fallbackScriptIntKey(input.symbolIndex, input.valueIndex);
  out.symbolIndex = input.symbolIndex;
  out.valueIndex = input.valueIndex;
  out.valueAfter = input.valueAfter;

  const auto validation = Story::validateScriptInt({
    .scriptKey = out.scriptKey,
    .symbolIndex = out.symbolIndex,
    .valueIndex = out.valueIndex,
    .valueAfter = out.valueAfter,
  });
  if(!validation.accepted) {
    out.accepted = false;
    out.reason = validation.reason;
  }
  return out;
}

} // namespace Mmo::Server::Script
