#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Mmo::Server::NpcActivity {

enum class Kind : std::uint8_t {
  Unknown,
  Idle,
  Routine,
  Moving,
  Talking,
  Sleeping,
  UsingMob,
  Alert,
  Combat,
  Down,
  Dead,
};

struct Snapshot final {
  std::string actorKey;
  std::string actionKey;
  Kind kind = Kind::Unknown;
  std::string targetKey;
  std::string syncGroup;
  std::uint64_t startedServerTickMs = 0;
  std::uint64_t updatedServerTickMs = 0;
  std::uint32_t expectedDurationMs = 0;
};

struct ApplyResult final {
  bool accepted = true;
  const char* reason = "ok";
  bool preempted = false;
  std::optional<Snapshot> interrupted;
};

struct ActivityInput final {
  std::string_view actorKey;
  std::string_view actionKey;
  std::string_view actionState;
  std::string_view targetKey;
  std::string_view syncGroup;
  std::uint64_t serverTickMs = 0;
  std::uint32_t expectedDurationMs = 0;
};

struct DialogLockInput final {
  std::string_view conversationKey;
  std::string_view speakerKey;
  std::string_view listenerKey;
  std::uint64_t serverTickMs = 0;
  std::uint32_t lineDurationMs = 0;
};

class Registry final {
public:
  [[nodiscard]] ApplyResult applyActivity(const ActivityInput& input);
  [[nodiscard]] ApplyResult applyDialogLock(const DialogLockInput& input);
  [[nodiscard]] std::optional<Snapshot> snapshot(std::string_view actorKey) const;
  [[nodiscard]] std::vector<Snapshot> snapshots() const;

  void expire(std::uint64_t nowServerTickMs);
  bool clearActorIfKind(std::string_view actorKey, Kind kind);
  void clearSyncGroup(std::string_view syncGroup);
  void clear();

private:
  [[nodiscard]] static std::uint8_t priority(Kind kind) noexcept;
  [[nodiscard]] static bool isPreemption(const Snapshot& current,
                                         Kind nextKind,
                                         std::string_view nextSyncGroup,
                                         std::uint64_t nowServerTickMs) noexcept;
  [[nodiscard]] static bool canReplace(const Snapshot& current,
                                       Kind nextKind,
                                       std::string_view nextSyncGroup,
                                       std::uint64_t nowServerTickMs) noexcept;
  void setActivity(ActivityInput input, Kind kind);

  std::unordered_map<std::string, Snapshot> states_;
};

[[nodiscard]] Kind parseKind(std::string_view actionKey) noexcept;
[[nodiscard]] std::string_view kindName(Kind kind) noexcept;
[[nodiscard]] bool isExclusive(Kind kind) noexcept;

} // namespace Mmo::Server::NpcActivity
