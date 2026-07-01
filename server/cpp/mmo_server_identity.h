#pragma once

#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Mmo::Server::Identity {

inline constexpr std::string_view NpcHookPrefix = "npc:";
inline constexpr std::string_view CreatureHookPrefix = "creature:";
inline constexpr std::string_view NpcMalformedDotPrefix = "npc.";
inline constexpr std::string_view WorldItemHookPrefix = "world-item:";
inline constexpr std::string_view WorldItemMalformedDotPrefix = "world-item.";
inline constexpr std::string_view WorldItemDbPrefix = "world_item:";

[[nodiscard]] constexpr bool startsWith(std::string_view text, std::string_view prefix) noexcept {
  return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

[[nodiscard]] constexpr bool looksLikeNpcKey(std::string_view text) noexcept {
  return startsWith(text, NpcHookPrefix) || startsWith(text, CreatureHookPrefix) || startsWith(text, NpcMalformedDotPrefix);
}

[[nodiscard]] constexpr bool looksLikeWorldItemKey(std::string_view text) noexcept {
  return startsWith(text, WorldItemHookPrefix) || startsWith(text, WorldItemMalformedDotPrefix) || startsWith(text, WorldItemDbPrefix);
}

[[nodiscard]] inline std::optional<std::int64_t> parseI64(std::string_view text) noexcept {
  if(text.empty())
    return std::nullopt;
  std::int64_t value = 0;
  const auto r = std::from_chars(text.data(), text.data() + text.size(), value);
  if(r.ec != std::errc{} || r.ptr != text.data() + text.size())
    return std::nullopt;
  return value;
}

[[nodiscard]] inline std::string canonicalNpcHookKey(std::string_view world, std::int64_t persistentId, std::int64_t symbol) {
  std::string out;
  out.reserve(world.size() + 32);
  out += "npc:";
  out += world;
  out += ":pid:";
  out += std::to_string(persistentId);
  out += ":sym:";
  out += std::to_string(symbol);
  return out;
}

[[nodiscard]] inline std::string canonicalCreatureHookKey(std::string_view world, std::int64_t persistentId, std::int64_t symbol) {
  std::string out;
  out.reserve(world.size() + 37);
  out += "creature:";
  out += world;
  out += ":pid:";
  out += std::to_string(persistentId);
  out += ":sym:";
  out += std::to_string(symbol);
  return out;
}

[[nodiscard]] inline std::string canonicalNpcLegacyLike(std::string_view world, std::int64_t persistentId, std::int64_t symbol) {
  std::string out;
  out.reserve(world.size() + 32);
  out += "npc:";
  out += world;
  out += ':';
  out += std::to_string(persistentId);
  out += ':';
  out += std::to_string(symbol);
  out += ":%";
  return out;
}

[[nodiscard]] inline std::string canonicalCreatureLegacyLike(std::string_view world, std::int64_t persistentId, std::int64_t symbol) {
  std::string out;
  out.reserve(world.size() + 37);
  out += "creature:";
  out += world;
  out += ':';
  out += std::to_string(persistentId);
  out += ':';
  out += std::to_string(symbol);
  out += ":%";
  return out;
}

[[nodiscard]] inline std::string anyWorldNpcHookLike(std::int64_t persistentId, std::int64_t symbol) {
  return "npc:%:pid:" + std::to_string(persistentId) + ":sym:" + std::to_string(symbol) + "%";
}

[[nodiscard]] inline std::string anyWorldCreatureHookLike(std::int64_t persistentId, std::int64_t symbol) {
  return "creature:%:pid:" + std::to_string(persistentId) + ":sym:" + std::to_string(symbol) + "%";
}

[[nodiscard]] inline std::string anyWorldNpcLegacyLike(std::int64_t persistentId, std::int64_t symbol) {
  return "npc:%:" + std::to_string(persistentId) + ":" + std::to_string(symbol) + ":%";
}

[[nodiscard]] inline std::string anyWorldCreatureLegacyLike(std::int64_t persistentId, std::int64_t symbol) {
  return "creature:%:" + std::to_string(persistentId) + ":" + std::to_string(symbol) + ":%";
}

[[nodiscard]] inline std::string canonicalWorldItemHookKey(std::string_view world, std::int64_t persistentId, std::int64_t symbol) {
  std::string out;
  out.reserve(world.size() + 40);
  out += "world-item:";
  out += world;
  out += ":pid:";
  out += std::to_string(persistentId);
  out += ":sym:";
  out += std::to_string(symbol);
  return out;
}

[[nodiscard]] inline std::string canonicalWorldItemDbLike(std::string_view world, std::int64_t persistentId, std::int64_t symbol) {
  std::string out;
  out.reserve(world.size() + 40);
  out += "world_item:";
  out += world;
  out += ':';
  out += std::to_string(persistentId);
  out += ':';
  out += std::to_string(symbol);
  out += ":%";
  return out;
}

[[nodiscard]] inline std::string anyWorldItemHookLike(std::int64_t persistentId, std::int64_t symbol) {
  return "world-item:%:pid:" + std::to_string(persistentId) + ":sym:" + std::to_string(symbol) + "%";
}

} // namespace Mmo::Server::Identity
