#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace Mmo::ClientPresentation {

[[nodiscard]] constexpr bool isAsciiWorldObjectSpace(
    const unsigned char value) noexcept {
  return value == static_cast<unsigned char>(' ') ||
         value == static_cast<unsigned char>('\t') ||
         value == static_cast<unsigned char>('\r') ||
         value == static_cast<unsigned char>('\n');
}

[[nodiscard]] constexpr unsigned char lowerAsciiWorldObjectCharacter(
    const unsigned char value) noexcept {
  return value >= static_cast<unsigned char>('A') &&
                 value <= static_cast<unsigned char>('Z')
             ? static_cast<unsigned char>(
                   value + static_cast<unsigned char>('a' - 'A'))
             : value;
}

inline constexpr std::uint64_t StableWorldObjectFnvOffset =
    14695981039346656037ULL;
inline constexpr std::uint64_t StableWorldObjectFnvPrime =
    1099511628211ULL;

[[nodiscard]] inline std::string canonicalWorldObjectWorldName(
    const std::string_view value) {
  std::size_t begin = 0U;
  while(begin < value.size() &&
        isAsciiWorldObjectSpace(static_cast<unsigned char>(value[begin]))) {
    ++begin;
  }
  std::size_t end = value.size();
  while(end > begin &&
        isAsciiWorldObjectSpace(static_cast<unsigned char>(value[end - 1U]))) {
    --end;
  }

  std::string normalized;
  normalized.reserve(end - begin + 4U);
  for(std::size_t index = begin; index < end; ++index) {
    const auto raw = static_cast<unsigned char>(value[index]);
    const auto normalizedRaw = raw == static_cast<unsigned char>('\\')
                                   ? static_cast<unsigned char>('/')
                                   : raw;
    normalized.push_back(static_cast<char>(
        lowerAsciiWorldObjectCharacter(normalizedRaw)));
  }
  if(const auto slash = normalized.find_last_of('/');
     slash != std::string::npos) {
    normalized.erase(0U, slash + 1U);
  }
  if(normalized.empty() || normalized == "." || normalized == "..")
    return {};
  if(!normalized.ends_with(".zen"))
    normalized += ".zen";
  return normalized;
}

[[nodiscard]] constexpr std::uint64_t appendStableWorldObjectHash(
    std::uint64_t hash,
    const std::string_view value) noexcept {
  for(const char raw : value) {
    hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(raw));
    hash *= StableWorldObjectFnvPrime;
  }
  return hash;
}

[[nodiscard]] inline std::uint64_t makeStableWorldObjectId(
    const std::string_view worldName,
    const std::string_view sourceClass,
    const std::string_view treePath,
    const std::uint32_t archiveId) {
  const auto canonicalWorld = canonicalWorldObjectWorldName(worldName);
  if(canonicalWorld.empty() || sourceClass.empty() || treePath.empty())
    return 0U;

  std::string identity;
  identity.reserve(canonicalWorld.size() + sourceClass.size() +
                   treePath.size() + 48U);
  identity += canonicalWorld;
  identity += ":vob/";
  identity += treePath;
  identity += "/";
  identity += sourceClass;
  identity += "#";
  identity += std::to_string(archiveId);

  auto hash = appendStableWorldObjectHash(
      StableWorldObjectFnvOffset, "entity");
  hash ^= 0U;
  hash *= StableWorldObjectFnvPrime;
  hash = appendStableWorldObjectHash(hash, identity);
  return hash == 0U ? 1U : hash;
}

} // namespace Mmo::ClientPresentation
