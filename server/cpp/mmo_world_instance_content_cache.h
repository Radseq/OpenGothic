#pragma once

#include "mmo_runtime_read_model_loader.h"

#include <cstddef>
#include <filesystem>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace Mmo::WorldInstanceContent {

template<class Record>
class IndexedRecordRange final {
public:
  class Iterator final {
  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = Record;
    using difference_type = std::ptrdiff_t;
    using pointer = const Record*;
    using reference = const Record&;

    constexpr Iterator() noexcept = default;
    constexpr Iterator(const std::vector<Record>* records, const std::size_t* cursor) noexcept
        : records_(records), cursor_(cursor) {}

    [[nodiscard]] reference operator*() const noexcept {
      return (*records_)[*cursor_];
    }

    [[nodiscard]] pointer operator->() const noexcept {
      return &(**this);
    }

    Iterator& operator++() noexcept {
      ++cursor_;
      return *this;
    }

    Iterator operator++(int) noexcept {
      Iterator out = *this;
      ++(*this);
      return out;
    }

    [[nodiscard]] friend bool operator==(const Iterator& lhs, const Iterator& rhs) noexcept {
      return lhs.cursor_ == rhs.cursor_;
    }

    [[nodiscard]] friend bool operator!=(const Iterator& lhs, const Iterator& rhs) noexcept {
      return !(lhs == rhs);
    }

  private:
    const std::vector<Record>* records_ = nullptr;
    const std::size_t* cursor_ = nullptr;
  };

  constexpr IndexedRecordRange() noexcept = default;

  constexpr IndexedRecordRange(const std::vector<Record>& records, const std::vector<std::size_t>& indexes) noexcept
      : records_(&records), indexes_(&indexes) {}

  [[nodiscard]] Iterator begin() const noexcept {
    if(indexes_ == nullptr) {
      return {};
    }
    return Iterator(records_, indexes_->data());
  }

  [[nodiscard]] Iterator end() const noexcept {
    if(indexes_ == nullptr) {
      return {};
    }
    return Iterator(records_, indexes_->data() + indexes_->size());
  }

  [[nodiscard]] std::size_t size() const noexcept {
    return indexes_ == nullptr ? 0U : indexes_->size();
  }

  [[nodiscard]] bool empty() const noexcept {
    return size() == 0U;
  }

private:
  const std::vector<Record>* records_ = nullptr;
  const std::vector<std::size_t>* indexes_ = nullptr;
};

struct WorldInstanceContentCacheOptions final {
  std::filesystem::path runtimeReadModelPath;
  std::string contentRevisionKey;
  std::string worldInstanceKey;
  std::string worldName;
  bool requireContentRevisionMatch = true;
};

struct WorldInstanceContentCacheStats final {
  RuntimeReadModel::RuntimeIndexCounts indexCounts;
  std::size_t worldZenEntitiesInWorld = 0;
  std::size_t waypointEdgesInWorld = 0;
  std::size_t npcTemplates = 0;
  std::size_t itemTemplates = 0;
  std::size_t routines = 0;
  std::size_t perceptionBindings = 0;
  std::size_t dialogInfos = 0;
  std::size_t dialogOutputs = 0;
};

struct WorldInstanceContentCacheLookupChecks final {
  bool firstWorldZenEntityInWorld = false;
  bool firstWaypointEdgeInWorld = false;
  bool firstNpcTemplateByInstance = false;
  bool firstItemTemplateByInstance = false;
  bool firstRoutineByNpcInstance = false;
  bool firstRoutineBySymbol = false;
  bool firstPerceptionBindingByKind = false;
  bool firstPerceptionBindingByOwner = false;
  bool firstDialogInfoBySymbol = false;
  bool firstDialogOutputByName = false;
};

class WorldInstanceContentCache final {
public:
  using RoutineRange = IndexedRecordRange<RuntimeReadModel::RoutineRecord>;
  using PerceptionBindingRange = IndexedRecordRange<RuntimeReadModel::PerceptionBindingRecord>;

  [[nodiscard]] static WorldInstanceContentCache load(const WorldInstanceContentCacheOptions& options);

  WorldInstanceContentCache() = default;

  [[nodiscard]] const std::string& contentRevisionKey() const noexcept;
  [[nodiscard]] const std::string& worldInstanceKey() const noexcept;
  [[nodiscard]] const std::string& worldName() const noexcept;
  [[nodiscard]] const RuntimeReadModel::RuntimeReadModel& readModel() const noexcept;
  [[nodiscard]] const WorldInstanceContentCacheStats& stats() const noexcept;

  [[nodiscard]] const RuntimeReadModel::WorldZenEntityRecord* findWorldZenEntity(
      std::string_view entityKind,
      std::string_view entityKey) const;
  [[nodiscard]] const RuntimeReadModel::WaypointEdgeRecord* findWaypointEdge(
      std::string_view fromWaypointKey,
      std::string_view toWaypointKey) const;
  [[nodiscard]] const RuntimeReadModel::NpcTemplateRecord* findNpcTemplate(std::string_view npcInstance) const;
  [[nodiscard]] const RuntimeReadModel::ItemTemplateRecord* findItemTemplate(std::string_view itemInstance) const;
  [[nodiscard]] RoutineRange routinesByNpcInstance(std::string_view npcInstance) const;
  [[nodiscard]] RoutineRange routinesBySymbol(std::string_view routineSymbol) const;
  [[nodiscard]] PerceptionBindingRange perceptionBindingsByKind(std::string_view perceptionKind) const;
  [[nodiscard]] PerceptionBindingRange perceptionBindingsByOwner(std::string_view ownerSymbol) const;
  [[nodiscard]] const RuntimeReadModel::DialogInfoRecord* findDialogInfo(std::string_view infoSymbol) const;
  [[nodiscard]] const RuntimeReadModel::DialogOutputRecord* findDialogOutput(std::string_view outputName) const;

private:
  WorldInstanceContentCache(
      RuntimeReadModel::RuntimeReadModel model,
      std::string contentRevisionKey,
      std::string worldInstanceKey,
      std::string worldName,
      WorldInstanceContentCacheStats stats);

  RuntimeReadModel::RuntimeReadModel model_;
  std::string contentRevisionKey_;
  std::string worldInstanceKey_;
  std::string worldName_;
  WorldInstanceContentCacheStats stats_;
};

[[nodiscard]] std::vector<std::string> validateWorldInstanceContentCacheOptions(
    const WorldInstanceContentCacheOptions& options,
    const RuntimeReadModel::ReadModelInspection& inspection);
[[nodiscard]] WorldInstanceContentCacheStats computeWorldInstanceContentCacheStats(
    const RuntimeReadModel::RuntimeReadModel& model,
    std::string_view worldName);
[[nodiscard]] WorldInstanceContentCacheLookupChecks runDeterministicLookupChecks(
    const WorldInstanceContentCache& cache);

} // namespace Mmo::WorldInstanceContent
