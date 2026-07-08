#pragma once

#include <filesystem>
#include <iosfwd>
#include <string>
#include <vector>

namespace Mmo::ContentBuild {

struct SourceFile final {
  std::string sourceKey;
  std::string logicalPath;
  std::filesystem::path hostPath;
  std::string sha256;
  std::uintmax_t byteSize = 0;
  bool requiredForServerAuthority = true;
};

struct SourceOptions final {
  SourceFile worldZen;
  SourceFile scriptsDat;
  SourceFile dialogOu;
};

struct SnapshotOptions final {
  std::string contentRevisionKey;
  std::string gameCode = "gothic2-notr";
  std::string sourceRootLabel;
  std::string manifestHash;
  std::string worldName;
  SourceOptions sources;
};

struct ImportSummary final {
  std::size_t sourceCount = 0;
  std::size_t zenEntityCount = 0;
  std::size_t waypointEdgeCount = 0;
  std::size_t daedalusSymbolCount = 0;
  std::size_t npcTemplateCount = 0;
  std::size_t itemTemplateCount = 0;
  std::size_t routineCount = 0;
  std::size_t perceptionBindingCount = 0;
  std::size_t dialogInfoCount = 0;
  std::size_t dialogOutputCount = 0;
  std::size_t parserErrorCount = 0;
};

[[nodiscard]] ImportSummary writeParserSnapshot(std::ostream& out, const SnapshotOptions& options);

} // namespace Mmo::ContentBuild



