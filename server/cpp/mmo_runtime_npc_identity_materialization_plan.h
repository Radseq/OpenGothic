#pragma once

#include "mmo_world_instance_content_cache.h"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Mmo::NpcPerceptionRuntime {

struct RuntimeNpcIdentityMaterializationCandidate final {
  std::string worldEntityStateUuid;
  std::string entityKey;
  std::string entityKind = "npc";
  std::string templateKey;
  std::string scriptName;
  std::string stateNpcInstance;
  std::string stateSymbolName;
  std::string stateInstanceName;
  std::string creatureTemplateKey;
  bool positionKnown = true;
};

struct RuntimeNpcIdentityMaterializationPlanOptions final {
  bool requireReadModelTemplate = true;
  bool allowEntityKeyRepair = true;
};

struct RuntimeNpcIdentityMaterializationRowPlan final {
  bool canMaterialize = false;
  bool dbWriteRequired = false;
  bool entityKeyRepairPlanned = false;
  bool npcInstanceWritePlanned = false;

  std::string status = "not_planned";
  std::string worldEntityStateUuid;
  std::string entityKey;
  std::string plannedEntityKey;
  std::string stableNpcInstance;
  std::string stableNpcInstanceSource;
  std::vector<std::string> issues;
};

struct RuntimeNpcIdentityMaterializationPlan final {
  bool built = false;
  bool dbMutated = false;
  bool sqlGenerated = false;
  bool serverSqlTouched = false;
  bool materializationExecuted = false;

  std::string status = "not_built";
  std::size_t readModelNpcTemplates = 0;
  std::size_t runtimeRows = 0;
  std::size_t readyRows = 0;
  std::size_t dbWriteRequiredRows = 0;
  std::size_t missingNpcInstanceRows = 0;
  std::size_t missingReadModelTemplateRows = 0;
  std::size_t entityKeyRepairRows = 0;

  std::vector<RuntimeNpcIdentityMaterializationRowPlan> rows;
  std::vector<std::string> issues;
};

[[nodiscard]] RuntimeNpcIdentityMaterializationPlan buildRuntimeNpcIdentityMaterializationReadinessPlan(
    const WorldInstanceContent::WorldInstanceContentCache& cache);

[[nodiscard]] RuntimeNpcIdentityMaterializationPlan buildRuntimeNpcIdentityMaterializationPlan(
    const WorldInstanceContent::WorldInstanceContentCache& cache,
    std::span<const RuntimeNpcIdentityMaterializationCandidate> candidates,
    const RuntimeNpcIdentityMaterializationPlanOptions& options = {});

[[nodiscard]] std::string runtimeNpcIdentityMaterializationPlanJson(
    const RuntimeNpcIdentityMaterializationPlan& plan);

} // namespace Mmo::NpcPerceptionRuntime
