#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Mmo::RuntimeReadModel {

struct SectionCounts final {
  std::size_t worldZenEntityCount = 0;
  std::size_t waypointEdgeCount = 0;
  std::size_t npcTemplateCount = 0;
  std::size_t itemTemplateCount = 0;
  std::size_t routineCount = 0;
  std::size_t perceptionBindingCount = 0;
  std::size_t dialogInfoCount = 0;
  std::size_t dialogOutputCount = 0;
};

struct SectionFlags final {
  bool worldZenEntities = false;
  bool waypointEdges = false;
  bool npcTemplates = false;
  bool itemTemplates = false;
  bool routines = false;
  bool perceptionBindings = false;
  bool dialogInfos = false;
  bool dialogOutputs = false;
};

struct ReadModelInspection final {
  std::filesystem::path sourcePath;
  std::string schema;
  std::string contentRevisionKey;
  std::string gameCode;
  std::string payloadSha256;
  std::string sourceKind;
  std::string snapshotPath;
  SectionCounts summary;
  SectionCounts sectionCounts;
  SectionFlags summaryFields;
  SectionFlags sectionArrays;
  std::vector<std::string> warnings;
};

struct WorldZenEntityRecord final {
  std::string worldName;
  std::string entityKind;
  std::string entityKey;
  std::string name;
};

struct WaypointEdgeRecord final {
  std::string worldName;
  std::string fromWaypointKey;
  std::string toWaypointKey;
  double travelCost = 0.0;
};

struct NpcTemplateRecord final {
  std::string npcInstance;
  std::string displayName;
  std::string guild;
  std::string routineSymbol;
  std::string perceptionSymbol;
};

struct ItemTemplateRecord final {
  std::string itemInstance;
  std::string displayName;
  std::string itemCategory;
};

struct RoutineRecord final {
  std::string npcInstance;
  std::string routineSymbol;
  std::string actionSymbol;
  std::string targetPointKey;
};

struct PerceptionBindingRecord final {
  std::string ownerSymbol;
  std::string ownerKind;
  std::string perceptionKind;
  std::string functionSymbol;
};

struct DialogInfoRecord final {
  std::string infoSymbol;
  std::string npcInstance;
  std::string conditionSymbol;
  std::string informationSymbol;
  bool permanent = false;
  bool important = false;
  bool trade = false;
};

struct DialogOutputRecord final {
  std::string outputName;
  std::string text;
  std::string audioRef;
};

struct RuntimeIndexCounts final {
  std::size_t worldZenEntityByKey = 0;
  std::size_t waypointEdgeByRoute = 0;
  std::size_t npcTemplateByInstance = 0;
  std::size_t itemTemplateByInstance = 0;
  std::size_t routineByNpcInstance = 0;
  std::size_t routineBySymbol = 0;
  std::size_t perceptionBindingByKind = 0;
  std::size_t perceptionBindingByOwner = 0;
  std::size_t dialogInfoBySymbol = 0;
  std::size_t dialogOutputByName = 0;
};

struct RuntimeLookupChecks final {
  bool firstWorldZenEntityByKey = false;
  bool firstWaypointEdgeByRoute = false;
  bool firstNpcTemplateByInstance = false;
  bool firstItemTemplateByInstance = false;
  bool firstRoutineByNpcInstance = false;
  bool firstRoutineBySymbol = false;
  bool firstPerceptionBindingByKind = false;
  bool firstPerceptionBindingByOwner = false;
  bool firstDialogInfoBySymbol = false;
  bool firstDialogOutputByName = false;
};

struct RuntimeReadModel final {
  ReadModelInspection inspection;
  std::vector<WorldZenEntityRecord> worldZenEntities;
  std::vector<WaypointEdgeRecord> waypointEdges;
  std::vector<NpcTemplateRecord> npcTemplates;
  std::vector<ItemTemplateRecord> itemTemplates;
  std::vector<RoutineRecord> routines;
  std::vector<PerceptionBindingRecord> perceptionBindings;
  std::vector<DialogInfoRecord> dialogInfos;
  std::vector<DialogOutputRecord> dialogOutputs;

  std::unordered_map<std::string, std::size_t> worldZenEntityByKey;
  std::unordered_map<std::string, std::size_t> waypointEdgeByRoute;
  std::unordered_map<std::string, std::size_t> npcTemplateByInstance;
  std::unordered_map<std::string, std::size_t> itemTemplateByInstance;
  std::unordered_map<std::string, std::vector<std::size_t>> routinesByNpcInstance;
  std::unordered_map<std::string, std::vector<std::size_t>> routinesBySymbol;
  std::unordered_map<std::string, std::vector<std::size_t>> perceptionBindingsByKind;
  std::unordered_map<std::string, std::vector<std::size_t>> perceptionBindingsByOwner;
  std::unordered_map<std::string, std::size_t> dialogInfoBySymbol;
  std::unordered_map<std::string, std::size_t> dialogOutputByName;
};

[[nodiscard]] ReadModelInspection inspectRuntimeReadModel(const std::filesystem::path& path);
[[nodiscard]] std::vector<std::string> validateRuntimeReadModel(const ReadModelInspection& model);
[[nodiscard]] RuntimeReadModel loadRuntimeReadModel(const std::filesystem::path& path);
[[nodiscard]] RuntimeIndexCounts indexCounts(const RuntimeReadModel& model) noexcept;
[[nodiscard]] RuntimeLookupChecks runDeterministicLookupChecks(const RuntimeReadModel& model);
[[nodiscard]] std::string makeWorldZenEntityIndexKey(
    std::string_view worldName,
    std::string_view entityKind,
    std::string_view entityKey);
[[nodiscard]] std::string makeWaypointEdgeRouteIndexKey(
    std::string_view worldName,
    std::string_view fromWaypointKey,
    std::string_view toWaypointKey);

} // namespace Mmo::RuntimeReadModel
