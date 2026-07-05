#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace Mmo::Server::NpcAction {

inline constexpr std::size_t MaxNpcActionKeyBytes = 512;
inline constexpr std::size_t MaxNpcActionNameBytes = 128;
inline constexpr std::size_t MaxNpcDialogTextBytes = 1024;
inline constexpr std::uint32_t MaxNpcDialogLineMs = 60000;

struct ValidationResult final {
  bool accepted = true;
  bool shouldPersist = true;
  const char* reason = "ok";
};

struct ActionInput final {
  std::string_view actorKey;
  std::string_view actionKey;
  std::string_view actionState;
  std::string_view targetKey;
  std::string_view syncGroup;
  std::uint64_t serverTick = 0;
};

struct DialogLineInput final {
  std::string_view conversationKey;
  std::string_view speakerKey;
  std::string_view listenerKey;
  std::string_view infoKey;
  std::string_view outputName;
  std::string_view subtitleText;
  std::uint32_t lineDurationMs = 0;
  std::uint64_t serverTick = 0;
};

struct ActionCommand final {
  bool accepted = true;
  bool shouldPersist = true;
  const char* reason = "ok";
  std::string actorKey;
  std::string actionKey;
  std::string actionState;
  std::string targetKey;
  std::string syncGroup;
  std::uint64_t serverTick = 0;
};

struct DialogLineCommand final {
  bool accepted = true;
  bool shouldPersist = true;
  const char* reason = "ok";
  std::string conversationKey;
  std::string speakerKey;
  std::string listenerKey;
  std::string infoKey;
  std::string outputName;
  std::string subtitleText;
  std::uint32_t lineDurationMs = 0;
  std::uint64_t serverTick = 0;
};

[[nodiscard]] ActionCommand buildActionCommand(const ActionInput& input);
[[nodiscard]] DialogLineCommand buildDialogLineCommand(const DialogLineInput& input);

} // namespace Mmo::Server::NpcAction
