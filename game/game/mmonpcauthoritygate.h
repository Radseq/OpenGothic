#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

class Npc;

namespace Mmo::ClientPresentation {

enum class NpcAuthorityMode : std::uint8_t {
  NativeSinglePlayer,
  ServerReplica,
};

enum class NpcLocalGameplayEntryPoint : std::uint8_t {
  Routine,
  AiQueue,
  Perception,
  TargetSelection,
  Combat,
  AttributeMutation,
  MovementPlanning,
  MovementMutation,
  RotationMutation,
  AnimationGameplayEvent,
  Dialog,
  Interaction,
  Count,
};

struct NpcAuthorityDiagnostics final {
  std::string_view localNpcAi;
  std::string_view routineAuthority;
  std::string_view movementAuthority;
  std::string_view combatAuthority;
  std::string_view dialogAuthority;

  [[nodiscard]] constexpr bool serverAuthoritative() const noexcept {
    return localNpcAi == "disabled" && routineAuthority == "server" &&
           movementAuthority == "server" && combatAuthority == "server" &&
           dialogAuthority == "server";
  }
};

inline constexpr NpcAuthorityDiagnostics ServerReplicaNpcAuthorityDiagnostics{
    .localNpcAi = "disabled",
    .routineAuthority = "server",
    .movementAuthority = "server",
    .combatAuthority = "server",
    .dialogAuthority = "server",
};

inline constexpr NpcAuthorityDiagnostics NativeNpcAuthorityDiagnostics{
    .localNpcAi = "enabled",
    .routineAuthority = "client",
    .movementAuthority = "client",
    .combatAuthority = "client",
    .dialogAuthority = "client",
};

inline constexpr std::string_view ServerReplicaNpcAuthorityDiagnosticLine =
    "local_npc_ai=disabled routine_authority=server "
    "movement_authority=server combat_authority=server "
    "dialog_authority=server";

class NpcAuthorityGate final {
  public:
    constexpr void setMode(const NpcAuthorityMode mode) noexcept { mode_ = mode; }
    [[nodiscard]] constexpr NpcAuthorityMode mode() const noexcept {
      return mode_;
    }
    [[nodiscard]] constexpr bool serverReplica() const noexcept {
      return mode_ == NpcAuthorityMode::ServerReplica;
    }
    [[nodiscard]] constexpr bool allowsLocalGameplay() const noexcept {
      return mode_ == NpcAuthorityMode::NativeSinglePlayer;
    }
    [[nodiscard]] constexpr bool serverPlayerPositionAuthority() const noexcept {
      return serverPlayerPositionAuthority_;
    }
    constexpr void setServerPlayerPositionAuthority(const bool value) noexcept {
      serverPlayerPositionAuthority_ = value;
    }
    [[nodiscard]] constexpr NpcAuthorityDiagnostics diagnostics() const noexcept {
      return serverReplica() ? ServerReplicaNpcAuthorityDiagnostics
                             : NativeNpcAuthorityDiagnostics;
    }

    [[nodiscard]] bool rejectLocalGameplay(
        const NpcLocalGameplayEntryPoint entryPoint) noexcept {
      if(applyingServerPresentation())
        return false;
      // The local player may still turn and play local input animations, but
      // its world position must come only from the server.
      if(serverPlayerPositionAuthority_ &&
         entryPoint != NpcLocalGameplayEntryPoint::MovementMutation)
        return false;
      if(allowsLocalGameplay())
        return false;
      const auto index = static_cast<std::size_t>(entryPoint);
      if(index >= rejected_.size())
        return true;
      auto& value = rejected_[index];
      if(value != std::numeric_limits<std::uint32_t>::max())
        ++value;
      if(totalRejected_ != std::numeric_limits<std::uint32_t>::max())
        ++totalRejected_;
      return true;
    }

    [[nodiscard]] std::uint32_t rejectedLocalGameplayCount() const noexcept {
      return totalRejected_;
    }

    [[nodiscard]] std::uint32_t rejectedLocalGameplayCount(
        const NpcLocalGameplayEntryPoint entryPoint) const noexcept {
      const auto index = static_cast<std::size_t>(entryPoint);
      return index < rejected_.size() ? rejected_[index] : 0U;
    }

    void clearDiagnostics() noexcept {
      rejected_.fill(0U);
      totalRejected_ = 0U;
    }

  private:
    friend class ::Npc;

    class ServerPresentationScope final {
      public:
        ServerPresentationScope(const ServerPresentationScope&) = delete;
        ServerPresentationScope& operator=(const ServerPresentationScope&) =
            delete;
        ServerPresentationScope(ServerPresentationScope&& other) noexcept
            : owner_(other.owner_) {
          other.owner_ = nullptr;
        }
        ServerPresentationScope& operator=(ServerPresentationScope&&) = delete;

        ~ServerPresentationScope() {
          if(owner_ != nullptr)
            owner_->leaveServerPresentation();
        }

      private:
        friend class NpcAuthorityGate;
        explicit ServerPresentationScope(NpcAuthorityGate& owner) noexcept
            : owner_(owner.enterServerPresentation() ? &owner : nullptr) {
        }

        NpcAuthorityGate* owner_ = nullptr;
    };

    [[nodiscard]] bool applyingServerPresentation() const noexcept {
      return serverPresentationDepth_ != 0U;
    }

    [[nodiscard]] ServerPresentationScope serverPresentationScope() noexcept {
      return ServerPresentationScope{*this};
    }

    [[nodiscard]] bool enterServerPresentation() noexcept {
      if(serverPresentationDepth_ ==
         std::numeric_limits<std::uint16_t>::max()) {
        return false;
      }
      ++serverPresentationDepth_;
      return true;
    }

    void leaveServerPresentation() noexcept {
      if(serverPresentationDepth_ != 0U)
        --serverPresentationDepth_;
    }

    static constexpr auto EntryPointCount =
        static_cast<std::size_t>(NpcLocalGameplayEntryPoint::Count);
    NpcAuthorityMode mode_ = NpcAuthorityMode::NativeSinglePlayer;
    bool serverPlayerPositionAuthority_ = false;
    std::array<std::uint32_t, EntryPointCount> rejected_{};
    std::uint32_t totalRejected_ = 0U;
    std::uint16_t serverPresentationDepth_ = 0U;
};

} // namespace Mmo::ClientPresentation
