#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Mmo::ContentBuild {

struct ActiveReadModelSelectionRequest final {
  std::string contentRevisionKey;
  std::string worldInstanceKey;
  std::string worldName;
  std::string explicitRuntimeReadModelPath;
  std::string requiredSchema = "mmo.content_build_runtime_read_model.v1";
  bool preferExplicitPath = true;
  bool allowActiveExportLookup = true;
  bool requireActiveExport = true;
};

struct RuntimeReadModelExportCandidate final {
  std::string exportUuid;
  std::string contentRevisionKey;
  std::string runtimeReadModelPath;
  std::string payloadSha256;
  std::string schema;
  bool active = false;
  std::uint64_t exportSequence = 0;
  std::uint64_t exportedAtUnix = 0;
  std::uint64_t rowVersion = 0;
};

struct ActiveReadModelSelectionResult final {
  bool selected = false;
  bool explicitPathUsed = false;
  bool dbLookupRequired = false;
  bool dbMutated = false;
  bool sqlGenerated = false;
  bool serverSqlTouched = false;

  std::string status = "not_selected";
  std::string selectedPath;
  std::string selectedExportUuid;
  std::string contentRevisionKey;
  std::string worldInstanceKey;
  std::string worldName;
  std::vector<std::string> issues;
};

[[nodiscard]] ActiveReadModelSelectionResult planActiveReadModelSelection(
    const ActiveReadModelSelectionRequest& request);

[[nodiscard]] ActiveReadModelSelectionResult selectActiveReadModelExport(
    const ActiveReadModelSelectionRequest& request,
    std::span<const RuntimeReadModelExportCandidate> candidates);

[[nodiscard]] bool isEligibleRuntimeReadModelExport(
    const ActiveReadModelSelectionRequest& request,
    const RuntimeReadModelExportCandidate& candidate,
    std::vector<std::string>* issues = nullptr);

[[nodiscard]] std::string activeReadModelSelectionSummaryJson(
    const ActiveReadModelSelectionResult& result);

} // namespace Mmo::ContentBuild
