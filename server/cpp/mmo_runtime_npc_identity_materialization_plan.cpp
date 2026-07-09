#include "mmo_runtime_npc_identity_materialization_plan.h"

#include <cctype>
#include <utility>

namespace Mmo::NpcPerceptionRuntime {
namespace {

[[nodiscard]] std::string trimCopy(std::string_view text) {
  while(!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
    text.remove_prefix(1);
  }
  while(!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
    text.remove_suffix(1);
  }
  return std::string(text);
}

[[nodiscard]] std::string lowerAscii(std::string_view value) {
  std::string out;
  out.reserve(value.size());
  for(const unsigned char c : value) {
    out.push_back(static_cast<char>(std::tolower(c)));
  }
  return out;
}

[[nodiscard]] bool weakToken(std::string_view value) {
  const std::string text = lowerAscii(trimCopy(value));
  return text.empty() || text == "null" || text == "none" || text == "undefined" || text == "0" || text == "-1";
}

[[nodiscard]] bool weakEntityKey(std::string_view value) {
  const std::string text = lowerAscii(trimCopy(value));
  return weakToken(text) || text == "npc:none" || text == "creature:none" ||
         text == "npc:null" || text == "creature:null" ||
         text.starts_with("npc:pid:-1") || text.starts_with("creature:pid:-1");
}

[[nodiscard]] std::string sanitizeComponent(std::string value) {
  for(char& ch : value) {
    const unsigned char c = static_cast<unsigned char>(ch);
    if(std::isalnum(c) == 0 && ch != '_' && ch != '-' && ch != '.') {
      ch = '_';
    }
  }
  return value;
}

[[nodiscard]] std::pair<std::string, std::string> stableNpcInstance(
    const RuntimeNpcIdentityMaterializationCandidate& candidate) {
  const std::pair<std::string_view, std::string_view> fields[] = {
      {candidate.stateNpcInstance, "state_npc_instance"},
      {candidate.scriptName, "script_name"},
      {candidate.stateSymbolName, "state_symbol_name"},
      {candidate.stateInstanceName, "state_instance_name"},
      {candidate.creatureTemplateKey, "creature_template_key"},
      {candidate.templateKey, "template_key"},
  };
  for(const auto& [value, source] : fields) {
    if(!weakToken(value)) {
      return {trimCopy(value), std::string(source)};
    }
  }
  return {};
}

[[nodiscard]] std::string plannedEntityKey(
    const RuntimeNpcIdentityMaterializationCandidate& candidate,
    std::string_view stableInstance) {
  if(weakToken(candidate.worldEntityStateUuid) || weakToken(stableInstance)) {
    return {};
  }
  const std::string kind = weakToken(candidate.entityKind) ? "npc" : sanitizeComponent(trimCopy(candidate.entityKind));
  std::string out;
  out.reserve(kind.size() + stableInstance.size() + candidate.worldEntityStateUuid.size() + 24);
  out += kind;
  out += ":template:";
  out += sanitizeComponent(std::string(stableInstance));
  out += ":state:";
  out += sanitizeComponent(trimCopy(candidate.worldEntityStateUuid));
  return out;
}

[[nodiscard]] std::string jsonEscape(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 8);
  out.push_back('"');
  for(const unsigned char c : text) {
    switch(c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        out.push_back(c < 0x20U ? ' ' : static_cast<char>(c));
        break;
    }
  }
  out.push_back('"');
  return out;
}

void appendField(std::string& out, std::string_view key, std::string_view value) {
  if(out.back() != '{') {
    out.push_back(',');
  }
  out += jsonEscape(key);
  out.push_back(':');
  out += jsonEscape(value);
}

void appendBool(std::string& out, std::string_view key, bool value) {
  if(out.back() != '{') {
    out.push_back(',');
  }
  out += jsonEscape(key);
  out.push_back(':');
  out += value ? "true" : "false";
}

void appendCount(std::string& out, std::string_view key, std::size_t value) {
  if(out.back() != '{') {
    out.push_back(',');
  }
  out += jsonEscape(key);
  out.push_back(':');
  out += std::to_string(value);
}

} // namespace

RuntimeNpcIdentityMaterializationPlan buildRuntimeNpcIdentityMaterializationReadinessPlan(
    const WorldInstanceContent::WorldInstanceContentCache& cache) {
  RuntimeNpcIdentityMaterializationPlan out;
  out.built = true;
  out.status = "runtime_rows_required_no_db_query_executed";
  out.readModelNpcTemplates = cache.stats().npcTemplates;
  if(out.readModelNpcTemplates == 0) {
    out.issues.emplace_back("read_model_has_no_npc_templates");
  }
  return out;
}

RuntimeNpcIdentityMaterializationPlan buildRuntimeNpcIdentityMaterializationPlan(
    const WorldInstanceContent::WorldInstanceContentCache& cache,
    std::span<const RuntimeNpcIdentityMaterializationCandidate> candidates,
    const RuntimeNpcIdentityMaterializationPlanOptions& options) {
  RuntimeNpcIdentityMaterializationPlan out;
  out.built = true;
  out.status = "planned_no_db_write";
  out.readModelNpcTemplates = cache.stats().npcTemplates;
  out.runtimeRows = candidates.size();
  out.rows.reserve(candidates.size());

  for(const auto& candidate : candidates) {
    RuntimeNpcIdentityMaterializationRowPlan row;
    row.worldEntityStateUuid = candidate.worldEntityStateUuid;
    row.entityKey = candidate.entityKey;

    const auto [stable, source] = stableNpcInstance(candidate);
    row.stableNpcInstance = stable;
    row.stableNpcInstanceSource = source;
    if(weakToken(stable)) {
      row.status = "missing_stable_npc_instance";
      row.issues.emplace_back("missing_stable_npc_instance");
      ++out.missingNpcInstanceRows;
    }

    if(!candidate.positionKnown) {
      row.issues.emplace_back("missing_runtime_position");
    }

    if(!weakToken(stable) && options.requireReadModelTemplate && cache.findNpcTemplate(stable) == nullptr) {
      row.issues.emplace_back("npc_instance_not_found_in_runtime_read_model");
      ++out.missingReadModelTemplateRows;
    }

    if(options.allowEntityKeyRepair && weakEntityKey(candidate.entityKey)) {
      row.plannedEntityKey = plannedEntityKey(candidate, stable);
      row.entityKeyRepairPlanned = !row.plannedEntityKey.empty();
      if(row.entityKeyRepairPlanned) {
        ++out.entityKeyRepairRows;
      } else {
        row.issues.emplace_back("cannot_repair_weak_entity_key");
      }
    }

    row.npcInstanceWritePlanned = weakToken(candidate.stateNpcInstance) && !weakToken(stable);
    row.canMaterialize = row.issues.empty();
    row.dbWriteRequired = row.canMaterialize && (row.npcInstanceWritePlanned || row.entityKeyRepairPlanned);
    if(row.canMaterialize) {
      row.status = row.dbWriteRequired ? "db_write_required_not_executed" : "identity_already_materialized";
      ++out.readyRows;
    }
    if(row.dbWriteRequired) {
      ++out.dbWriteRequiredRows;
    }
    out.rows.push_back(std::move(row));
  }

  if(candidates.empty()) {
    out.status = "runtime_rows_required_no_db_query_executed";
  } else if(out.dbWriteRequiredRows == 0 && out.readyRows == out.runtimeRows) {
    out.status = "all_runtime_npc_identity_rows_ready";
  } else if(out.dbWriteRequiredRows > 0) {
    out.status = "materialization_writes_planned_not_executed";
  }
  return out;
}

std::string runtimeNpcIdentityMaterializationPlanJson(
    const RuntimeNpcIdentityMaterializationPlan& plan) {
  std::string out;
  out.reserve(768 + plan.rows.size() * 256);
  out.push_back('{');
  appendField(out, "status", plan.status);
  appendBool(out, "built", plan.built);
  appendBool(out, "db_mutated", plan.dbMutated);
  appendBool(out, "sql_generated", plan.sqlGenerated);
  appendBool(out, "server_sql_touched", plan.serverSqlTouched);
  appendBool(out, "materialization_executed", plan.materializationExecuted);
  appendCount(out, "read_model_npc_templates", plan.readModelNpcTemplates);
  appendCount(out, "runtime_rows", plan.runtimeRows);
  appendCount(out, "ready_rows", plan.readyRows);
  appendCount(out, "db_write_required_rows", plan.dbWriteRequiredRows);
  appendCount(out, "missing_npc_instance_rows", plan.missingNpcInstanceRows);
  appendCount(out, "missing_read_model_template_rows", plan.missingReadModelTemplateRows);
  appendCount(out, "entity_key_repair_rows", plan.entityKeyRepairRows);
  out += ",\"issues\":[";
  for(std::size_t i = 0; i < plan.issues.size(); ++i) {
    if(i != 0) {
      out.push_back(',');
    }
    out += jsonEscape(plan.issues[i]);
  }
  out += "],\"rows\":[";
  for(std::size_t i = 0; i < plan.rows.size(); ++i) {
    if(i != 0) {
      out.push_back(',');
    }
    const auto& row = plan.rows[i];
    out.push_back('{');
    appendField(out, "status", row.status);
    appendBool(out, "can_materialize", row.canMaterialize);
    appendBool(out, "db_write_required", row.dbWriteRequired);
    appendBool(out, "entity_key_repair_planned", row.entityKeyRepairPlanned);
    appendBool(out, "npc_instance_write_planned", row.npcInstanceWritePlanned);
    appendField(out, "world_entity_state_uuid", row.worldEntityStateUuid);
    appendField(out, "entity_key", row.entityKey);
    appendField(out, "planned_entity_key", row.plannedEntityKey);
    appendField(out, "stable_npc_instance", row.stableNpcInstance);
    appendField(out, "stable_npc_instance_source", row.stableNpcInstanceSource);
    out += ",\"issues\":[";
    for(std::size_t issueIndex = 0; issueIndex < row.issues.size(); ++issueIndex) {
      if(issueIndex != 0) {
        out.push_back(',');
      }
      out += jsonEscape(row.issues[issueIndex]);
    }
    out += "]}";
  }
  out += "]}";
  return out;
}

} // namespace Mmo::NpcPerceptionRuntime
