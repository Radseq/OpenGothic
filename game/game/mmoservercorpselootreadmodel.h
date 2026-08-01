#pragma once

#include "mmoserverpresentationevents.h"
#include "../../../shared/net/mmo/mmo_protocol_v2.h"
#include "../../../shared/net/mmo/mmo_protocol_v2_live_presentation.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace Mmo::ClientPresentation {

inline constexpr std::uint32_t ServerCorpseLootAvailable =
    Mmo::ProtocolV2::LootAvailable;
inline constexpr std::uint32_t ServerCorpseLootExclusive =
    Mmo::ProtocolV2::LootExclusive;
inline constexpr std::uint32_t ServerCorpseLootLooterPresent =
    Mmo::ProtocolV2::LootLooterPresent;
inline constexpr std::uint32_t KnownServerCorpseLootAvailabilityFlags =
    Mmo::ProtocolV2::KnownLootAvailabilityFlags;

struct ServerCorpseLootAvailabilityChanged final {
  ServerPresentationEventHeader header{};
  ClientEntityHandle corpse{};
  ClientEntityHandle looter{};
  std::uint64_t lootRevision = 0U;
  std::uint32_t flags = 0U;

  [[nodiscard]] constexpr bool valid() const noexcept {
    const bool looterPresent =
        (flags & ServerCorpseLootLooterPresent) != 0U;
    const bool available = (flags & ServerCorpseLootAvailable) != 0U;
    const bool exclusive = (flags & ServerCorpseLootExclusive) != 0U;
    return header.valid() && corpse.valid() && lootRevision != 0U &&
           (flags & ~KnownServerCorpseLootAvailabilityFlags) == 0U &&
           (looterPresent ? looter.valid()
                          : looter.worldId == 0U &&
                                looter.worldGeneration == 0U &&
                                looter.id == 0U && looter.generation == 0U) &&
           (!exclusive || (available && looterPresent)) &&
           corpse.worldId == header.route.world.id &&
           corpse.worldGeneration == header.route.world.generation;
  }
};

enum class ServerCorpseLootStackDeltaKind : std::uint8_t {
  StackAdded,
  StackRemoved,
  StackQuantityChanged,
};

struct ServerCorpseLootSnapshot final {
  ServerPresentationEventHeader header{};
  ClientEntityHandle corpse{};
  std::uint64_t corpseRevision = 0U;
  std::uint64_t inventoryRevision = 0U;
  std::uint64_t snapshotId = 0U;
  std::vector<ServerInventoryStack> stacks;

  [[nodiscard]] bool valid() const noexcept {
    return header.valid() && corpse.valid() && corpseRevision != 0U &&
           inventoryRevision != 0U && snapshotId != 0U &&
           corpse.worldId == header.route.world.id &&
           corpse.worldGeneration == header.route.world.generation;
  }
};

struct ServerCorpseLootStackDelta final {
  ServerPresentationEventHeader header{};
  ClientEntityHandle corpse{};
  ServerCorpseLootStackDeltaKind kind =
      ServerCorpseLootStackDeltaKind::StackAdded;
  ServerInventoryStack stack{};
  std::uint64_t corpseRevision = 0U;
  std::uint64_t inventoryRevision = 0U;

  [[nodiscard]] constexpr bool valid() const noexcept {
    return header.valid() && corpse.valid() && stack.handle.valid() &&
           corpseRevision != 0U && inventoryRevision != 0U &&
           corpse.worldId == header.route.world.id &&
           corpse.worldGeneration == header.route.world.generation &&
           (kind == ServerCorpseLootStackDeltaKind::StackRemoved ||
            stack.valid());
  }
};

enum class ServerCorpseLootSessionCloseReason : std::uint8_t {
  ClientRequested,
  Empty,
  OutOfRange,
  RouteChanged,
  Disconnected,
  ActorDied,
  Decayed,
  AccessDenied,
  ReplacedByResync,
};

struct ServerCorpseLootSessionClosed final {
  ServerPresentationEventHeader header{};
  ClientEntityHandle corpse{};
  ServerCorpseLootSessionCloseReason reason =
      ServerCorpseLootSessionCloseReason::ClientRequested;
  std::uint64_t corpseRevision = 0U;
  std::uint64_t inventoryRevision = 0U;

  [[nodiscard]] constexpr bool valid() const noexcept {
    return header.valid() && corpse.valid() && corpseRevision != 0U &&
           inventoryRevision != 0U &&
           corpse.worldId == header.route.world.id &&
           corpse.worldGeneration == header.route.world.generation;
  }
};

enum class ServerCorpseLootResyncReason : std::uint8_t {
  MissingSnapshot,
  SnapshotOrderViolation,
  RevisionGap,
  ProjectionConflict,
  CapacityExceeded,
};

struct ServerCorpseLootResync final {
  ClientEntityHandle corpse{};
  std::uint64_t routeEpoch = 0U;
  std::uint64_t expectedCorpseRevision = 0U;
  std::uint64_t expectedInventoryRevision = 0U;
  ServerCorpseLootResyncReason reason =
      ServerCorpseLootResyncReason::MissingSnapshot;
  ClientMmoSubmitStatus submissionStatus = ClientMmoSubmitStatus::Disabled;

  [[nodiscard]] constexpr bool valid() const noexcept {
    return corpse.valid() && expectedCorpseRevision != 0U &&
           expectedInventoryRevision != 0U;
  }
};

enum class ServerCorpseLootPendingKind : std::uint8_t {
  Open,
  TakeStack,
  TakeAll,
  Close,
};

enum class ServerCorpseLootPendingPhase : std::uint8_t {
  Submitted,
  AwaitingAuthoritativeUpdate,
};

struct ServerCorpseLootPendingCommand final {
  ClientMmoCommandToken command{};
  ServerCorpseLootPendingKind kind = ServerCorpseLootPendingKind::Open;
  ClientEntityHandle corpse{};
  ClientItemStackHandle stack{};
  std::uint32_t amount = 0U;
  std::uint64_t expectedCorpseRevision = 0U;
  std::uint64_t expectedInventoryRevision = 0U;
  ServerCorpseLootPendingPhase phase =
      ServerCorpseLootPendingPhase::Submitted;
};

enum class ServerCorpseLootFeedback : std::uint8_t {
  Ready,
  Pending,
  Accepted,
  Busy,
  AccessDenied,
  OutOfRange,
  FullInventory,
  StaleRevision,
  Empty,
  Decayed,
  Disconnected,
  ResyncRequired,
  Rejected,
};

struct ServerCorpseLootFingerprint final {
  std::uint64_t replacementRevision = 0U;
  std::uint64_t corpseRevision = 0U;
  std::uint64_t inventoryRevision = 0U;
  std::uint64_t snapshotId = 0U;
  std::uint64_t changeRevision = 0U;
  std::size_t stackCount = 0U;
  ServerCorpseLootFeedback feedback = ServerCorpseLootFeedback::Ready;

  [[nodiscard]] friend constexpr bool operator==(
      const ServerCorpseLootFingerprint&,
      const ServerCorpseLootFingerprint&) noexcept = default;
};

class ServerCorpseLootPresentationState final {
 public:
  explicit ServerCorpseLootPresentationState(
      const std::size_t corpseCapacity = 4096U,
      const std::size_t stackCapacity = 4096U)
      : corpseCapacity_(std::max<std::size_t>(1U, corpseCapacity)),
        stackCapacity_(std::max<std::size_t>(1U, stackCapacity)) {
    corpses_.reserve(corpseCapacity_);
    stacks_.reserve(stackCapacity_);
  }

  void reset(const ServerCorpseLootFeedback feedback =
                 ServerCorpseLootFeedback::Ready) noexcept {
    corpses_.clear();
    activeCorpse_.reset();
    stacks_.clear();
    pending_.reset();
    recentlyClosedCorpse_.reset();
    recentlyClosedCorpseRevision_ = 0U;
    recentlyClosedInventoryRevision_ = 0U;
    feedback_ = feedback;
    ++replacementRevision_;
    if(replacementRevision_ == 0U)
      replacementRevision_ = 1U;
    bumpChangeRevision();
  }

  void seedReplicatedDeath(const ClientEntityHandle corpse,
                           const bool dead) {
    if(!corpse.valid())
      return;
    auto* record = ensureCorpse(corpse);
    if(record == nullptr || record->deathRevision != 0U ||
       (record->deathKnown && record->dead == dead)) {
      return;
    }
    record->deathKnown = true;
    record->dead = dead;
    if(!dead)
      record->available = false;
    bumpChangeRevision();
  }

  void observeDeath(const ClientEntityHandle corpse,
                    const bool dead,
                    const std::uint64_t deathRevision) {
    if(!corpse.valid() || deathRevision == 0U)
      return;
    auto* record = ensureCorpse(corpse);
    if(record == nullptr)
      return;
    if(deathRevision < record->deathRevision)
      return;
    const bool changed = !record->deathKnown ||
                         deathRevision != record->deathRevision ||
                         dead != record->dead;
    record->deathKnown = true;
    record->deathRevision = deathRevision;
    record->dead = dead;
    if(!dead) {
      record->available = false;
      if(activeCorpse_ == corpse ||
         (pending_.has_value() && pending_->corpse == corpse)) {
        closeLocal(ServerCorpseLootFeedback::AccessDenied);
      }
    }
    if(changed)
      bumpChangeRevision();
  }

  void observeAvailability(const ServerCorpseLootAvailabilityChanged& event) {
    if(!event.valid())
      return;
    auto* record = ensureCorpse(event.corpse);
    if(record == nullptr)
      return;
    if(event.lootRevision < record->lootRevision)
      return;
    const bool available = (event.flags & ServerCorpseLootAvailable) != 0U;
    const bool changed = event.lootRevision != record->lootRevision ||
                         event.flags != record->availabilityFlags ||
                         event.looter.id != record->looter.id ||
                         event.looter.generation != record->looter.generation;
    record->lootRevision = event.lootRevision;
    record->availabilityFlags = event.flags;
    record->looter = event.looter;
    record->available = available;
    if(!available &&
       (activeCorpse_ == event.corpse ||
        (pending_.has_value() && pending_->corpse == event.corpse))) {
      closeLocal(ServerCorpseLootFeedback::Empty);
    }
    if(changed)
      bumpChangeRevision();
  }

  void observeDespawn(const ClientEntityHandle corpse) noexcept {
    const auto found = lowerBoundCorpse(corpse);
    if(found != corpses_.end() && found->corpse == corpse)
      corpses_.erase(found);
    if(activeCorpse_ == corpse ||
       (pending_.has_value() && pending_->corpse == corpse)) {
      closeLocal(ServerCorpseLootFeedback::OutOfRange);
    }
  }

  [[nodiscard]] bool replicatedDead(const ClientEntityHandle corpse) const noexcept {
    const auto* record = findCorpse(corpse);
    return record != nullptr && record->dead;
  }

  [[nodiscard]] bool canOpen(const ClientEntityHandle corpse) const noexcept {
    const auto* record = findCorpse(corpse);
    return record != nullptr && record->dead && record->available &&
           record->lootRevision != 0U;
  }

  [[nodiscard]] std::optional<ClientOpenCorpseLootRequest> openRequest(
      const ClientEntityHandle corpse,
      const std::uint64_t inventoryRevision) const noexcept {
    const auto* record = findCorpse(corpse);
    if(record == nullptr || !record->dead || !record->available ||
       record->lootRevision == 0U || inventoryRevision == 0U ||
       pending_.has_value() ||
       (activeCorpse_.has_value() && activeCorpse_ != corpse)) {
      return std::nullopt;
    }
    return ClientOpenCorpseLootRequest{
        .corpse = corpse,
        .expectedCorpseRevision = record->lootRevision,
        .expectedInventoryRevision = inventoryRevision,
    };
  }

  [[nodiscard]] std::optional<ClientTakeCorpseLootStackRequest> takeRequest(
      const ClientItemStackHandle stack,
      const std::uint32_t amount) const noexcept {
    const auto* item = findStack(stack);
    if(!ready() || pending_.has_value() || item == nullptr || amount == 0U ||
       amount > item->quantity) {
      return std::nullopt;
    }
    return ClientTakeCorpseLootStackRequest{
        .corpse = *activeCorpse_,
        .stack = stack,
        .amount = amount,
        .expectedCorpseRevision = corpseRevision_,
        .expectedInventoryRevision = inventoryRevision_,
    };
  }

  [[nodiscard]] std::optional<ClientTakeAllCorpseLootRequest>
  takeAllRequest() const noexcept {
    if(!ready() || pending_.has_value() || stacks_.empty())
      return std::nullopt;
    return ClientTakeAllCorpseLootRequest{
        .corpse = *activeCorpse_,
        .expectedCorpseRevision = corpseRevision_,
        .expectedInventoryRevision = inventoryRevision_,
    };
  }

  [[nodiscard]] std::optional<ClientCloseCorpseLootRequest>
  closeRequest() const noexcept {
    if(activeCorpse_.has_value() && corpseRevision_ != 0U &&
       inventoryRevision_ != 0U) {
      return ClientCloseCorpseLootRequest{
          .corpse = *activeCorpse_,
          .expectedCorpseRevision = corpseRevision_,
          .expectedInventoryRevision = inventoryRevision_,
      };
    }
    if(pending_.has_value() &&
       pending_->kind == ServerCorpseLootPendingKind::Open) {
      return ClientCloseCorpseLootRequest{
          .corpse = pending_->corpse,
          .expectedCorpseRevision = pending_->expectedCorpseRevision,
          .expectedInventoryRevision = pending_->expectedInventoryRevision,
      };
    }
    return std::nullopt;
  }

  [[nodiscard]] bool markPending(ServerCorpseLootPendingCommand command) {
    if(!validPending(command) ||
       (pending_.has_value() &&
        command.kind != ServerCorpseLootPendingKind::Close))
      return false;
    feedback_ = ServerCorpseLootFeedback::Pending;
    pending_ = std::move(command);
    bumpChangeRevision();
    return true;
  }

  void rejectSubmission(const ClientMmoSubmitStatus status) noexcept {
    if(status == ClientMmoSubmitStatus::Accepted)
      return;
    feedback_ = status == ClientMmoSubmitStatus::QueueFull
                    ? ServerCorpseLootFeedback::Busy
                    : status == ClientMmoSubmitStatus::TransportError
                          ? ServerCorpseLootFeedback::Disconnected
                          : ServerCorpseLootFeedback::Rejected;
    bumpChangeRevision();
  }

  void complete(const ClientMmoCommandCompletion& completion) noexcept {
    if(!pending_.has_value() || pending_->command != completion.command)
      return;
    if(completion.status == ClientMmoCommandCompletionStatus::Applied) {
      pending_->phase = ServerCorpseLootPendingPhase::AwaitingAuthoritativeUpdate;
      if(authoritativeUpdateObserved(*pending_)) {
        pending_.reset();
        feedback_ = stacks_.empty() ? ServerCorpseLootFeedback::Empty
                                    : ServerCorpseLootFeedback::Ready;
      } else {
        feedback_ = ServerCorpseLootFeedback::Accepted;
      }
      bumpChangeRevision();
      return;
    }

    feedback_ = feedbackForCompletion(completion);
    pending_.reset();
    bumpChangeRevision();
  }

  void installSnapshot(ServerCorpseLootSnapshot snapshot) {
    if(!snapshot.valid())
      return;
    const bool expected = activeCorpse_ == snapshot.corpse ||
                          (pending_.has_value() &&
                           pending_->kind == ServerCorpseLootPendingKind::Open &&
                           pending_->corpse == snapshot.corpse);
    if(!expected)
      return;
    if(!canOpen(snapshot.corpse) ||
       snapshot.stacks.size() > stackCapacity_ ||
       !normalizeAndValidate(snapshot.stacks)) {
      feedback_ = ServerCorpseLootFeedback::ResyncRequired;
      bumpChangeRevision();
      return;
    }
    const auto* corpse = findCorpse(snapshot.corpse);
    if(corpse == nullptr || snapshot.corpseRevision < corpse->lootRevision) {
      feedback_ = ServerCorpseLootFeedback::StaleRevision;
      bumpChangeRevision();
      return;
    }
    if(activeCorpse_ == snapshot.corpse &&
       snapshot.corpseRevision < corpseRevision_) {
      return;
    }
    activeCorpse_ = snapshot.corpse;
    recentlyClosedCorpse_.reset();
    recentlyClosedCorpseRevision_ = 0U;
    recentlyClosedInventoryRevision_ = 0U;
    corpseRevision_ = snapshot.corpseRevision;
    inventoryRevision_ = snapshot.inventoryRevision;
    snapshotId_ = snapshot.snapshotId;
    stacks_ = std::move(snapshot.stacks);
    reconcilePending();
    feedback_ = stacks_.empty() ? ServerCorpseLootFeedback::Empty
                                : pendingFeedbackOrReady();
    bumpChangeRevision();
  }

  void applyDelta(const ServerCorpseLootStackDelta& delta) {
    if(!delta.valid() || !activeCorpse_.has_value() ||
       delta.corpse != *activeCorpse_) {
      return;
    }
    if(delta.corpseRevision <= corpseRevision_ ||
       delta.inventoryRevision < inventoryRevision_) {
      return;
    }
    auto found = lowerBound(stacks_, delta.stack.handle);
    const bool exists = found != stacks_.end() &&
                        found->handle == delta.stack.handle;
    switch(delta.kind) {
      case ServerCorpseLootStackDeltaKind::StackAdded:
        if(exists || !delta.stack.valid() || stacks_.size() >= stackCapacity_) {
          feedback_ = ServerCorpseLootFeedback::ResyncRequired;
          bumpChangeRevision();
          return;
        }
        stacks_.insert(found, delta.stack);
        break;
      case ServerCorpseLootStackDeltaKind::StackRemoved:
        if(!exists) {
          feedback_ = ServerCorpseLootFeedback::ResyncRequired;
          bumpChangeRevision();
          return;
        }
        stacks_.erase(found);
        break;
      case ServerCorpseLootStackDeltaKind::StackQuantityChanged:
        if(!exists || !delta.stack.valid()) {
          feedback_ = ServerCorpseLootFeedback::ResyncRequired;
          bumpChangeRevision();
          return;
        }
        *found = delta.stack;
        break;
    }
    corpseRevision_ = delta.corpseRevision;
    inventoryRevision_ = delta.inventoryRevision;
    reconcilePending();
    feedback_ = stacks_.empty() ? ServerCorpseLootFeedback::Empty
                                : pendingFeedbackOrReady();
    bumpChangeRevision();
  }

  void sessionClosed(const ServerCorpseLootSessionClosed& event) noexcept {
    if(!event.valid())
      return;
    const bool current = activeCorpse_ == event.corpse ||
                         (pending_.has_value() &&
                          pending_->corpse == event.corpse);
    const bool recentlyClosed = recentlyClosedCorpse_ == event.corpse;
    if(!current && !recentlyClosed)
      return;

    const auto minimumCorpseRevision = current
                                           ? corpseRevision_
                                           : recentlyClosedCorpseRevision_;
    const auto minimumInventoryRevision = current
                                              ? inventoryRevision_
                                              : recentlyClosedInventoryRevision_;
    if(event.corpseRevision < minimumCorpseRevision ||
       event.inventoryRevision < minimumInventoryRevision) {
      return;
    }

    if(current) {
      corpseRevision_ = event.corpseRevision;
      inventoryRevision_ = event.inventoryRevision;
      closeLocal(feedbackForClose(event.reason));
      return;
    }

    recentlyClosedCorpseRevision_ = event.corpseRevision;
    recentlyClosedInventoryRevision_ = event.inventoryRevision;
    feedback_ = feedbackForClose(event.reason);
    bumpChangeRevision();
  }

  void resyncRequired(const ServerCorpseLootResync& event) noexcept {
    if(!event.valid())
      return;
    if(activeCorpse_ != event.corpse &&
       (!pending_.has_value() || pending_->corpse != event.corpse)) {
      return;
    }
    closeLocal(ServerCorpseLootFeedback::ResyncRequired);
  }

  [[nodiscard]] bool ready() const noexcept {
    return activeCorpse_.has_value() && corpseRevision_ != 0U &&
           inventoryRevision_ != 0U &&
           feedback_ != ServerCorpseLootFeedback::StaleRevision &&
           feedback_ != ServerCorpseLootFeedback::ResyncRequired;
  }
  [[nodiscard]] bool active() const noexcept {
    return activeCorpse_.has_value();
  }
  [[nodiscard]] bool pending() const noexcept { return pending_.has_value(); }
  [[nodiscard]] bool actionsEnabled() const noexcept {
    return ready() && !pending_.has_value() && !stacks_.empty();
  }
  [[nodiscard]] std::optional<ClientEntityHandle> activeCorpse() const noexcept {
    return activeCorpse_;
  }
  [[nodiscard]] std::uint64_t corpseRevision() const noexcept {
    return corpseRevision_;
  }
  [[nodiscard]] std::uint64_t inventoryRevision() const noexcept {
    return inventoryRevision_;
  }
  [[nodiscard]] std::uint64_t snapshotId() const noexcept { return snapshotId_; }
  [[nodiscard]] std::span<const ServerInventoryStack> stacks() const noexcept {
    return stacks_;
  }
  [[nodiscard]] const ServerInventoryStack* stackAt(
      const std::size_t index) const noexcept {
    return index < stacks_.size() ? &stacks_[index] : nullptr;
  }
  [[nodiscard]] const ServerInventoryStack* findStack(
      const ClientItemStackHandle handle) const noexcept {
    const auto found = lowerBound(stacks_, handle);
    return found != stacks_.end() && found->handle == handle ? &*found : nullptr;
  }
  [[nodiscard]] std::optional<ServerCorpseLootPendingPhase> pendingPhase(
      const ClientItemStackHandle handle) const noexcept {
    if(!pending_.has_value() || pending_->stack != handle)
      return std::nullopt;
    return pending_->phase;
  }
  [[nodiscard]] const std::optional<ServerCorpseLootPendingCommand>&
  pendingCommand() const noexcept {
    return pending_;
  }
  [[nodiscard]] ServerCorpseLootFeedback feedback() const noexcept {
    return feedback_;
  }
  [[nodiscard]] constexpr std::string_view feedbackMessage() const noexcept {
    switch(feedback_) {
      case ServerCorpseLootFeedback::Ready: return {};
      case ServerCorpseLootFeedback::Pending: return "Pending: awaiting server receipt";
      case ServerCorpseLootFeedback::Accepted:
        return "Accepted: awaiting authoritative loot update";
      case ServerCorpseLootFeedback::Busy: return "Corpse is busy";
      case ServerCorpseLootFeedback::AccessDenied: return "Loot access denied";
      case ServerCorpseLootFeedback::OutOfRange: return "Corpse is out of range";
      case ServerCorpseLootFeedback::FullInventory: return "Inventory is full";
      case ServerCorpseLootFeedback::StaleRevision: return "Corpse changed; reopen it";
      case ServerCorpseLootFeedback::Empty: return "Corpse is empty";
      case ServerCorpseLootFeedback::Decayed: return "Corpse has decayed";
      case ServerCorpseLootFeedback::Disconnected: return "Disconnected from server";
      case ServerCorpseLootFeedback::ResyncRequired: return "Loot state requires resynchronization";
      case ServerCorpseLootFeedback::Rejected: return "Server rejected the loot command";
    }
    return {};
  }
  [[nodiscard]] ServerCorpseLootFingerprint fingerprint() const noexcept {
    return {
        .replacementRevision = replacementRevision_,
        .corpseRevision = corpseRevision_,
        .inventoryRevision = inventoryRevision_,
        .snapshotId = snapshotId_,
        .changeRevision = changeRevision_,
        .stackCount = stacks_.size(),
        .feedback = feedback_,
    };
  }

 private:
  struct CorpseRecord final {
    ClientEntityHandle corpse{};
    ClientEntityHandle looter{};
    std::uint64_t deathRevision = 0U;
    std::uint64_t lootRevision = 0U;
    std::uint32_t availabilityFlags = 0U;
    bool deathKnown = false;
    bool dead = false;
    bool available = false;
  };

  [[nodiscard]] static constexpr bool lessEntityHandle(
      const ClientEntityHandle lhs,
      const ClientEntityHandle rhs) noexcept {
    if(lhs.worldId != rhs.worldId)
      return lhs.worldId < rhs.worldId;
    if(lhs.worldGeneration != rhs.worldGeneration)
      return lhs.worldGeneration < rhs.worldGeneration;
    if(lhs.id != rhs.id)
      return lhs.id < rhs.id;
    return lhs.generation < rhs.generation;
  }

  [[nodiscard]] std::vector<CorpseRecord>::iterator lowerBoundCorpse(
      const ClientEntityHandle corpse) noexcept {
    return std::lower_bound(corpses_.begin(), corpses_.end(), corpse,
                            [](const CorpseRecord& lhs,
                               const ClientEntityHandle rhs) {
                              return lessEntityHandle(lhs.corpse, rhs);
                            });
  }
  [[nodiscard]] std::vector<CorpseRecord>::const_iterator lowerBoundCorpse(
      const ClientEntityHandle corpse) const noexcept {
    return std::lower_bound(corpses_.begin(), corpses_.end(), corpse,
                            [](const CorpseRecord& lhs,
                               const ClientEntityHandle rhs) {
                              return lessEntityHandle(lhs.corpse, rhs);
                            });
  }
  [[nodiscard]] CorpseRecord* ensureCorpse(
      const ClientEntityHandle corpse) {
    auto found = lowerBoundCorpse(corpse);
    if(found != corpses_.end() && found->corpse == corpse)
      return &*found;
    if(corpses_.size() >= corpseCapacity_)
      return nullptr;
    found = corpses_.insert(found, CorpseRecord{.corpse = corpse});
    return &*found;
  }
  [[nodiscard]] CorpseRecord* findCorpse(
      const ClientEntityHandle corpse) noexcept {
    const auto found = lowerBoundCorpse(corpse);
    return found != corpses_.end() && found->corpse == corpse ? &*found
                                                              : nullptr;
  }
  [[nodiscard]] const CorpseRecord* findCorpse(
      const ClientEntityHandle corpse) const noexcept {
    const auto found = lowerBoundCorpse(corpse);
    return found != corpses_.end() && found->corpse == corpse ? &*found
                                                              : nullptr;
  }

  [[nodiscard]] static constexpr bool lessHandle(
      const ClientItemStackHandle lhs,
      const ClientItemStackHandle rhs) noexcept {
    return lhs.instanceId < rhs.instanceId ||
           (lhs.instanceId == rhs.instanceId && lhs.generation < rhs.generation);
  }
  [[nodiscard]] static std::vector<ServerInventoryStack>::iterator lowerBound(
      std::vector<ServerInventoryStack>& values,
      const ClientItemStackHandle handle) {
    return std::lower_bound(values.begin(), values.end(), handle,
                            [](const ServerInventoryStack& lhs,
                               const ClientItemStackHandle rhs) {
                              return lessHandle(lhs.handle, rhs);
                            });
  }
  [[nodiscard]] static std::vector<ServerInventoryStack>::const_iterator lowerBound(
      const std::vector<ServerInventoryStack>& values,
      const ClientItemStackHandle handle) {
    return std::lower_bound(values.begin(), values.end(), handle,
                            [](const ServerInventoryStack& lhs,
                               const ClientItemStackHandle rhs) {
                              return lessHandle(lhs.handle, rhs);
                            });
  }
  [[nodiscard]] static bool normalizeAndValidate(
      std::vector<ServerInventoryStack>& values) {
    std::sort(values.begin(), values.end(), [](const auto& lhs, const auto& rhs) {
      return lessHandle(lhs.handle, rhs.handle);
    });
    for(std::size_t index = 0U; index < values.size(); ++index) {
      if(!values[index].valid() ||
         (index != 0U &&
          values[index - 1U].handle.instanceId ==
              values[index].handle.instanceId)) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] static constexpr bool validPending(
      const ServerCorpseLootPendingCommand& command) noexcept {
    if(!command.command.valid() ||
       command.command.kind != ClientMmoCommandKind::SelectiveLoot ||
       !command.corpse.valid() || command.expectedCorpseRevision == 0U ||
       command.expectedInventoryRevision == 0U) {
      return false;
    }
    switch(command.kind) {
      case ServerCorpseLootPendingKind::Open:
      case ServerCorpseLootPendingKind::TakeAll:
      case ServerCorpseLootPendingKind::Close:
        return !command.stack.valid() && command.amount == 0U;
      case ServerCorpseLootPendingKind::TakeStack:
        return command.stack.valid() && command.amount != 0U;
    }
    return false;
  }

  [[nodiscard]] static constexpr ServerCorpseLootFeedback feedbackForClose(
      const ServerCorpseLootSessionCloseReason reason) noexcept {
    switch(reason) {
      case ServerCorpseLootSessionCloseReason::ClientRequested:
        return ServerCorpseLootFeedback::Ready;
      case ServerCorpseLootSessionCloseReason::Empty:
        return ServerCorpseLootFeedback::Empty;
      case ServerCorpseLootSessionCloseReason::OutOfRange:
        return ServerCorpseLootFeedback::OutOfRange;
      case ServerCorpseLootSessionCloseReason::RouteChanged:
      case ServerCorpseLootSessionCloseReason::Disconnected:
        return ServerCorpseLootFeedback::Disconnected;
      case ServerCorpseLootSessionCloseReason::ActorDied:
      case ServerCorpseLootSessionCloseReason::AccessDenied:
        return ServerCorpseLootFeedback::AccessDenied;
      case ServerCorpseLootSessionCloseReason::Decayed:
        return ServerCorpseLootFeedback::Decayed;
      case ServerCorpseLootSessionCloseReason::ReplacedByResync:
        return ServerCorpseLootFeedback::ResyncRequired;
    }
    return ServerCorpseLootFeedback::Rejected;
  }

  [[nodiscard]] static constexpr ServerCorpseLootFeedback feedbackForCompletion(
      const ClientMmoCommandCompletion& completion) noexcept {
    if(completion.status == ClientMmoCommandCompletionStatus::ConnectionLost ||
       completion.status == ClientMmoCommandCompletionStatus::TransportClosed ||
       completion.status == ClientMmoCommandCompletionStatus::CancelledByRouteChange) {
      return ServerCorpseLootFeedback::Disconnected;
    }
    if(completion.status == ClientMmoCommandCompletionStatus::TimedOut)
      return ServerCorpseLootFeedback::Busy;
    using Code = Mmo::ProtocolV2::CommandRejectionCode;
    switch(static_cast<Code>(completion.rejectionCode)) {
      case Code::Busy: return ServerCorpseLootFeedback::Busy;
      case Code::OutOfRange: return ServerCorpseLootFeedback::OutOfRange;
      case Code::AdmissionCapacityExceeded:
        return ServerCorpseLootFeedback::FullInventory;
      case Code::AggregateRevisionMismatch:
      case Code::TargetRevisionMismatch:
      case Code::StaleRoute:
        return ServerCorpseLootFeedback::StaleRevision;
      case Code::ResourceNotFound:
        return ServerCorpseLootFeedback::Empty;
      case Code::ActionNotAllowed:
      case Code::DomainRejected:
      case Code::InvalidTargetHandle:
      case Code::TargetOutsideBoundWorld:
        return ServerCorpseLootFeedback::AccessDenied;
      default: return ServerCorpseLootFeedback::Rejected;
    }
  }

  [[nodiscard]] ServerCorpseLootFeedback pendingFeedbackOrReady() const noexcept {
    if(!pending_.has_value())
      return ServerCorpseLootFeedback::Ready;
    return pending_->phase == ServerCorpseLootPendingPhase::Submitted
               ? ServerCorpseLootFeedback::Pending
               : ServerCorpseLootFeedback::Accepted;
  }

  [[nodiscard]] bool authoritativeUpdateObserved(
      const ServerCorpseLootPendingCommand& command) const noexcept {
    switch(command.kind) {
      case ServerCorpseLootPendingKind::Open:
        return activeCorpse_ == command.corpse && snapshotId_ != 0U &&
               corpseRevision_ >= command.expectedCorpseRevision &&
               inventoryRevision_ >= command.expectedInventoryRevision;
      case ServerCorpseLootPendingKind::TakeStack:
      case ServerCorpseLootPendingKind::TakeAll:
        return activeCorpse_ == command.corpse &&
               (corpseRevision_ > command.expectedCorpseRevision ||
                inventoryRevision_ > command.expectedInventoryRevision);
      case ServerCorpseLootPendingKind::Close:
        return !activeCorpse_.has_value();
    }
    return false;
  }

  void reconcilePending() noexcept {
    if(!pending_.has_value() ||
       pending_->phase !=
           ServerCorpseLootPendingPhase::AwaitingAuthoritativeUpdate) {
      return;
    }
    const bool revised = corpseRevision_ > pending_->expectedCorpseRevision ||
                         inventoryRevision_ > pending_->expectedInventoryRevision;
    if(pending_->kind == ServerCorpseLootPendingKind::Open || revised)
      pending_.reset();
  }

  void closeLocal(const ServerCorpseLootFeedback feedback) noexcept {
    if(activeCorpse_.has_value()) {
      recentlyClosedCorpse_ = activeCorpse_;
      recentlyClosedCorpseRevision_ = corpseRevision_;
      recentlyClosedInventoryRevision_ = inventoryRevision_;
    } else if(pending_.has_value()) {
      recentlyClosedCorpse_ = pending_->corpse;
      recentlyClosedCorpseRevision_ = pending_->expectedCorpseRevision;
      recentlyClosedInventoryRevision_ = pending_->expectedInventoryRevision;
    }
    activeCorpse_.reset();
    stacks_.clear();
    corpseRevision_ = 0U;
    inventoryRevision_ = 0U;
    snapshotId_ = 0U;
    pending_.reset();
    feedback_ = feedback;
    bumpChangeRevision();
  }

  void bumpChangeRevision() noexcept {
    ++changeRevision_;
    if(changeRevision_ == 0U)
      changeRevision_ = 1U;
  }

  std::size_t corpseCapacity_ = 4096U;
  std::size_t stackCapacity_ = 4096U;
  std::vector<CorpseRecord> corpses_;
  std::optional<ClientEntityHandle> activeCorpse_;
  std::vector<ServerInventoryStack> stacks_;
  std::optional<ServerCorpseLootPendingCommand> pending_;
  std::optional<ClientEntityHandle> recentlyClosedCorpse_;
  std::uint64_t recentlyClosedCorpseRevision_ = 0U;
  std::uint64_t recentlyClosedInventoryRevision_ = 0U;
  std::uint64_t corpseRevision_ = 0U;
  std::uint64_t inventoryRevision_ = 0U;
  std::uint64_t snapshotId_ = 0U;
  std::uint64_t replacementRevision_ = 1U;
  std::uint64_t changeRevision_ = 1U;
  ServerCorpseLootFeedback feedback_ = ServerCorpseLootFeedback::Ready;
};

} // namespace Mmo::ClientPresentation
