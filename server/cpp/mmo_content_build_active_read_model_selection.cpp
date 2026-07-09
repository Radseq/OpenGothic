#include "mmo_content_build_active_read_model_selection.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace Mmo::ContentBuild {
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

[[nodiscard]] bool blank(std::string_view text) {
  return trimCopy(text).empty();
}

[[nodiscard]] bool sameText(std::string_view lhs, std::string_view rhs) noexcept {
  return lhs == rhs;
}

[[nodiscard]] bool betterCandidate(
    const RuntimeReadModelExportCandidate& lhs,
    const RuntimeReadModelExportCandidate& rhs) noexcept {
  if(lhs.exportSequence != rhs.exportSequence) {
    return lhs.exportSequence > rhs.exportSequence;
  }
  if(lhs.exportedAtUnix != rhs.exportedAtUnix) {
    return lhs.exportedAtUnix > rhs.exportedAtUnix;
  }
  return lhs.rowVersion > rhs.rowVersion;
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
        if(c < 0x20U) {
          out.push_back(' ');
        } else {
          out.push_back(static_cast<char>(c));
        }
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

} // namespace

bool isEligibleRuntimeReadModelExport(
    const ActiveReadModelSelectionRequest& request,
    const RuntimeReadModelExportCandidate& candidate,
    std::vector<std::string>* issues) {
  auto addIssue = [&](std::string issue) {
    if(issues != nullptr) {
      issues->push_back(std::move(issue));
    }
  };

  bool eligible = true;
  if(blank(candidate.runtimeReadModelPath)) {
    addIssue("candidate_missing_runtime_read_model_path");
    eligible = false;
  }
  if(!blank(request.contentRevisionKey) && !sameText(candidate.contentRevisionKey, request.contentRevisionKey)) {
    addIssue("candidate_content_revision_mismatch");
    eligible = false;
  }
  if(!blank(request.requiredSchema) && !sameText(candidate.schema, request.requiredSchema)) {
    addIssue("candidate_schema_mismatch");
    eligible = false;
  }
  if(request.requireActiveExport && !candidate.active) {
    addIssue("candidate_not_active");
    eligible = false;
  }
  return eligible;
}

ActiveReadModelSelectionResult planActiveReadModelSelection(
    const ActiveReadModelSelectionRequest& request) {
  ActiveReadModelSelectionResult out;
  out.contentRevisionKey = request.contentRevisionKey;
  out.worldInstanceKey = request.worldInstanceKey;
  out.worldName = request.worldName;

  if(request.preferExplicitPath && !blank(request.explicitRuntimeReadModelPath)) {
    out.selected = true;
    out.explicitPathUsed = true;
    out.status = "explicit_runtime_read_model_path_selected";
    out.selectedPath = trimCopy(request.explicitRuntimeReadModelPath);
    return out;
  }

  if(!request.allowActiveExportLookup) {
    out.status = "runtime_read_model_selection_disabled";
    out.issues.emplace_back("no_explicit_runtime_read_model_path");
    return out;
  }

  if(blank(request.contentRevisionKey)) {
    out.status = "active_export_lookup_missing_content_revision_key";
    out.dbLookupRequired = true;
    out.issues.emplace_back("missing_content_revision_key");
    return out;
  }

  out.status = "active_export_lookup_required_no_db_query_executed";
  out.dbLookupRequired = true;
  return out;
}

ActiveReadModelSelectionResult selectActiveReadModelExport(
    const ActiveReadModelSelectionRequest& request,
    std::span<const RuntimeReadModelExportCandidate> candidates) {
  ActiveReadModelSelectionResult out = planActiveReadModelSelection(request);
  if(out.selected || !out.dbLookupRequired) {
    return out;
  }

  const RuntimeReadModelExportCandidate* best = nullptr;
  for(const auto& candidate : candidates) {
    std::vector<std::string> candidateIssues;
    if(!isEligibleRuntimeReadModelExport(request, candidate, &candidateIssues)) {
      continue;
    }
    if(best == nullptr || betterCandidate(candidate, *best)) {
      best = &candidate;
    }
  }

  if(best == nullptr) {
    out.status = candidates.empty() ? "active_export_candidates_missing" : "active_export_not_found";
    out.issues.emplace_back("no_eligible_active_runtime_read_model_export");
    return out;
  }

  out.selected = true;
  out.dbLookupRequired = false;
  out.status = "active_export_selected";
  out.selectedPath = best->runtimeReadModelPath;
  out.selectedExportUuid = best->exportUuid;
  return out;
}

std::string activeReadModelSelectionSummaryJson(const ActiveReadModelSelectionResult& result) {
  std::string out;
  out.reserve(512 + result.issues.size() * 48);
  out.push_back('{');
  appendField(out, "status", result.status);
  appendBool(out, "selected", result.selected);
  appendBool(out, "explicit_path_used", result.explicitPathUsed);
  appendBool(out, "db_lookup_required", result.dbLookupRequired);
  appendBool(out, "db_mutated", result.dbMutated);
  appendBool(out, "sql_generated", result.sqlGenerated);
  appendBool(out, "server_sql_touched", result.serverSqlTouched);
  appendField(out, "selected_path", result.selectedPath);
  appendField(out, "selected_export_uuid", result.selectedExportUuid);
  appendField(out, "content_revision_key", result.contentRevisionKey);
  appendField(out, "world_instance_key", result.worldInstanceKey);
  appendField(out, "world_name", result.worldName);
  out += ",\"issues\":[";
  for(std::size_t i = 0; i < result.issues.size(); ++i) {
    if(i != 0) {
      out.push_back(',');
    }
    out += jsonEscape(result.issues[i]);
  }
  out += "]}";
  return out;
}

} // namespace Mmo::ContentBuild
