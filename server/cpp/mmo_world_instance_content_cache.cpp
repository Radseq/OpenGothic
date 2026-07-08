#include "mmo_world_instance_content_cache.h"

#include <sstream>
#include <stdexcept>
#include <utility>

namespace Mmo::WorldInstanceContent {
namespace {

[[nodiscard]] std::string joinErrors(const std::vector<std::string>& errors) {
  std::ostringstream out;
  for(std::size_t i = 0; i < errors.size(); ++i) {
    if(i != 0) {
      out << "; ";
    }
    out << errors[i];
  }
  return out.str();
}

template<class Record, class Index>
[[nodiscard]] const Record* findUniqueRecord(
    const std::vector<Record>& records,
    const Index& index,
    std::string key) noexcept {
  const auto it = index.find(key);
  if(it == index.end() || it->second >= records.size()) {
    return nullptr;
  }
  return &records[it->second];
}

template<class Record, class Index>
[[nodiscard]] IndexedRecordRange<Record> findRecordRange(
    const std::vector<Record>& records,
    const Index& index,
    std::string key) noexcept {
  const auto it = index.find(key);
  if(it == index.end()) {
    return {};
  }
  return IndexedRecordRange<Record>(records, it->second);
}

template<class Record, class Predicate>
[[nodiscard]] std::size_t countIf(const std::vector<Record>& records, Predicate&& predicate) {
  std::size_t count = 0;
  for(const auto& record : records) {
    if(predicate(record)) {
      ++count;
    }
  }
  return count;
}

} // namespace

WorldInstanceContentCache::WorldInstanceContentCache(
    RuntimeReadModel::RuntimeReadModel model,
    std::string contentRevisionKey,
    std::string worldInstanceKey,
    std::string worldName,
    WorldInstanceContentCacheStats stats)
    : model_(std::move(model)),
      contentRevisionKey_(std::move(contentRevisionKey)),
      worldInstanceKey_(std::move(worldInstanceKey)),
      worldName_(std::move(worldName)),
      stats_(stats) {}

WorldInstanceContentCache WorldInstanceContentCache::load(const WorldInstanceContentCacheOptions& options) {
  if(options.runtimeReadModelPath.empty()) {
    throw std::invalid_argument("runtime read-model path is required");
  }

  RuntimeReadModel::RuntimeReadModel model = RuntimeReadModel::loadRuntimeReadModel(options.runtimeReadModelPath);
  const auto errors = validateWorldInstanceContentCacheOptions(options, model.inspection);
  if(!errors.empty()) {
    throw std::invalid_argument("invalid world_instance content cache options: " + joinErrors(errors));
  }

  const auto stats = computeWorldInstanceContentCacheStats(model, options.worldName);
  return WorldInstanceContentCache(
      std::move(model),
      options.contentRevisionKey,
      options.worldInstanceKey,
      options.worldName,
      stats);
}

const std::string& WorldInstanceContentCache::contentRevisionKey() const noexcept {
  return contentRevisionKey_;
}

const std::string& WorldInstanceContentCache::worldInstanceKey() const noexcept {
  return worldInstanceKey_;
}

const std::string& WorldInstanceContentCache::worldName() const noexcept {
  return worldName_;
}

const RuntimeReadModel::RuntimeReadModel& WorldInstanceContentCache::readModel() const noexcept {
  return model_;
}

const WorldInstanceContentCacheStats& WorldInstanceContentCache::stats() const noexcept {
  return stats_;
}

const RuntimeReadModel::WorldZenEntityRecord* WorldInstanceContentCache::findWorldZenEntity(
    std::string_view entityKind,
    std::string_view entityKey) const {
  return findUniqueRecord(
      model_.worldZenEntities,
      model_.worldZenEntityByKey,
      RuntimeReadModel::makeWorldZenEntityIndexKey(worldName_, entityKind, entityKey));
}

const RuntimeReadModel::WaypointEdgeRecord* WorldInstanceContentCache::findWaypointEdge(
    std::string_view fromWaypointKey,
    std::string_view toWaypointKey) const {
  return findUniqueRecord(
      model_.waypointEdges,
      model_.waypointEdgeByRoute,
      RuntimeReadModel::makeWaypointEdgeRouteIndexKey(worldName_, fromWaypointKey, toWaypointKey));
}

const RuntimeReadModel::NpcTemplateRecord* WorldInstanceContentCache::findNpcTemplate(std::string_view npcInstance) const {
  return findUniqueRecord(model_.npcTemplates, model_.npcTemplateByInstance, std::string(npcInstance));
}

const RuntimeReadModel::ItemTemplateRecord* WorldInstanceContentCache::findItemTemplate(std::string_view itemInstance) const {
  return findUniqueRecord(model_.itemTemplates, model_.itemTemplateByInstance, std::string(itemInstance));
}

WorldInstanceContentCache::RoutineRange WorldInstanceContentCache::routinesByNpcInstance(std::string_view npcInstance) const {
  return findRecordRange(model_.routines, model_.routinesByNpcInstance, std::string(npcInstance));
}

WorldInstanceContentCache::RoutineRange WorldInstanceContentCache::routinesBySymbol(std::string_view routineSymbol) const {
  return findRecordRange(model_.routines, model_.routinesBySymbol, std::string(routineSymbol));
}

WorldInstanceContentCache::PerceptionBindingRange WorldInstanceContentCache::perceptionBindingsByKind(
    std::string_view perceptionKind) const {
  return findRecordRange(model_.perceptionBindings, model_.perceptionBindingsByKind, std::string(perceptionKind));
}

WorldInstanceContentCache::PerceptionBindingRange WorldInstanceContentCache::perceptionBindingsByOwner(
    std::string_view ownerSymbol) const {
  return findRecordRange(model_.perceptionBindings, model_.perceptionBindingsByOwner, std::string(ownerSymbol));
}

const RuntimeReadModel::DialogInfoRecord* WorldInstanceContentCache::findDialogInfo(std::string_view infoSymbol) const {
  return findUniqueRecord(model_.dialogInfos, model_.dialogInfoBySymbol, std::string(infoSymbol));
}

const RuntimeReadModel::DialogOutputRecord* WorldInstanceContentCache::findDialogOutput(std::string_view outputName) const {
  return findUniqueRecord(model_.dialogOutputs, model_.dialogOutputByName, std::string(outputName));
}

std::vector<std::string> validateWorldInstanceContentCacheOptions(
    const WorldInstanceContentCacheOptions& options,
    const RuntimeReadModel::ReadModelInspection& inspection) {
  std::vector<std::string> errors = RuntimeReadModel::validateRuntimeReadModel(inspection);
  if(options.contentRevisionKey.empty()) {
    errors.emplace_back("content_revision_key is required");
  }
  if(options.worldInstanceKey.empty()) {
    errors.emplace_back("world_instance_key is required");
  }
  if(options.worldName.empty()) {
    errors.emplace_back("world_name is required");
  }
  if(options.requireContentRevisionMatch && !options.contentRevisionKey.empty()
     && options.contentRevisionKey != inspection.contentRevisionKey) {
    errors.emplace_back(
        "content_revision_key mismatch active=" + options.contentRevisionKey + " read_model=" + inspection.contentRevisionKey);
  }
  return errors;
}

WorldInstanceContentCacheStats computeWorldInstanceContentCacheStats(
    const RuntimeReadModel::RuntimeReadModel& model,
    std::string_view worldName) {
  WorldInstanceContentCacheStats stats;
  stats.indexCounts = RuntimeReadModel::indexCounts(model);
  stats.worldZenEntitiesInWorld = countIf(model.worldZenEntities, [worldName](const auto& record) {
    return record.worldName == worldName;
  });
  stats.waypointEdgesInWorld = countIf(model.waypointEdges, [worldName](const auto& record) {
    return record.worldName == worldName;
  });
  stats.npcTemplates = model.npcTemplates.size();
  stats.itemTemplates = model.itemTemplates.size();
  stats.routines = model.routines.size();
  stats.perceptionBindings = model.perceptionBindings.size();
  stats.dialogInfos = model.dialogInfos.size();
  stats.dialogOutputs = model.dialogOutputs.size();
  return stats;
}

WorldInstanceContentCacheLookupChecks runDeterministicLookupChecks(const WorldInstanceContentCache& cache) {
  WorldInstanceContentCacheLookupChecks checks;
  const auto& model = cache.readModel();

  for(const auto& record : model.worldZenEntities) {
    if(record.worldName == cache.worldName() && !record.entityKind.empty() && !record.entityKey.empty()) {
      checks.firstWorldZenEntityInWorld = cache.findWorldZenEntity(record.entityKind, record.entityKey) == &record;
      break;
    }
  }
  for(const auto& record : model.waypointEdges) {
    if(record.worldName == cache.worldName() && !record.fromWaypointKey.empty() && !record.toWaypointKey.empty()) {
      checks.firstWaypointEdgeInWorld = cache.findWaypointEdge(record.fromWaypointKey, record.toWaypointKey) == &record;
      break;
    }
  }
  for(const auto& record : model.npcTemplates) {
    if(!record.npcInstance.empty()) {
      checks.firstNpcTemplateByInstance = cache.findNpcTemplate(record.npcInstance) == &record;
      break;
    }
  }
  for(const auto& record : model.itemTemplates) {
    if(!record.itemInstance.empty()) {
      checks.firstItemTemplateByInstance = cache.findItemTemplate(record.itemInstance) == &record;
      break;
    }
  }
  for(const auto& record : model.routines) {
    if(!record.npcInstance.empty()) {
      checks.firstRoutineByNpcInstance = !cache.routinesByNpcInstance(record.npcInstance).empty();
      break;
    }
  }
  for(const auto& record : model.routines) {
    if(!record.routineSymbol.empty()) {
      checks.firstRoutineBySymbol = !cache.routinesBySymbol(record.routineSymbol).empty();
      break;
    }
  }
  for(const auto& record : model.perceptionBindings) {
    if(!record.perceptionKind.empty()) {
      checks.firstPerceptionBindingByKind = !cache.perceptionBindingsByKind(record.perceptionKind).empty();
      break;
    }
  }
  for(const auto& record : model.perceptionBindings) {
    if(!record.ownerSymbol.empty()) {
      checks.firstPerceptionBindingByOwner = !cache.perceptionBindingsByOwner(record.ownerSymbol).empty();
      break;
    }
  }
  for(const auto& record : model.dialogInfos) {
    if(!record.infoSymbol.empty()) {
      checks.firstDialogInfoBySymbol = cache.findDialogInfo(record.infoSymbol) == &record;
      break;
    }
  }
  for(const auto& record : model.dialogOutputs) {
    if(!record.outputName.empty()) {
      checks.firstDialogOutputByName = cache.findDialogOutput(record.outputName) == &record;
      break;
    }
  }

  return checks;
}

} // namespace Mmo::WorldInstanceContent
