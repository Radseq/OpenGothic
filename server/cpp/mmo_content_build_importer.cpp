#include "mmo_content_build_loader.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

namespace fs = std::filesystem;

[[nodiscard]] const char* usageText() {
  return "usage: mmo_content_build_importer --content-revision-key KEY --output PATH "
         "[--game-code gothic2-notr] [--source-root-label LABEL] [--manifest-hash SHA256] "
         "[--world-name NAME --world-zen PATH --world-zen-logical-path PATH --world-zen-sha256 SHA256] "
         "[--scripts-dat PATH --scripts-dat-logical-path PATH --scripts-dat-sha256 SHA256] "
         "[--dialog-ou PATH --dialog-ou-logical-path PATH --dialog-ou-sha256 SHA256]";
}

[[noreturn]] void usageError() {
  throw std::runtime_error(usageText());
}

[[nodiscard]] std::string nextArg(int& index, int argc, char** argv, std::string_view name) {
  if(index + 1 >= argc) {
    throw std::runtime_error("missing value for " + std::string(name));
  }
  ++index;
  return argv[index];
}

void setHostPath(Mmo::ContentBuild::SourceFile& source, std::string value) {
  source.hostPath = fs::path(std::move(value));
}

} // namespace

int main(int argc, char** argv) {
  try {
    Mmo::ContentBuild::SnapshotOptions options;
    fs::path outputPath;

    options.sources.worldZen.sourceKey = "world_zen";
    options.sources.scriptsDat.sourceKey = "scripts_dat";
    options.sources.dialogOu.sourceKey = "dialog_ou";

    for(int i = 1; i < argc; ++i) {
      const std::string_view arg = argv[i];
      if(arg == "--help" || arg == "-h") {
        std::cout << "mmo_content_build_importer writes parser_snapshot JSON for mmo_content_build.\n"
                  << usageText() << "\n";
        return 0;
      } else if(arg == "--content-revision-key") {
        options.contentRevisionKey = nextArg(i, argc, argv, arg);
      } else if(arg == "--game-code") {
        options.gameCode = nextArg(i, argc, argv, arg);
      } else if(arg == "--source-root-label") {
        options.sourceRootLabel = nextArg(i, argc, argv, arg);
      } else if(arg == "--manifest-hash") {
        options.manifestHash = nextArg(i, argc, argv, arg);
      } else if(arg == "--world-name") {
        options.worldName = nextArg(i, argc, argv, arg);
      } else if(arg == "--world-zen") {
        setHostPath(options.sources.worldZen, nextArg(i, argc, argv, arg));
      } else if(arg == "--world-zen-logical-path") {
        options.sources.worldZen.logicalPath = nextArg(i, argc, argv, arg);
      } else if(arg == "--world-zen-sha256") {
        options.sources.worldZen.sha256 = nextArg(i, argc, argv, arg);
      } else if(arg == "--scripts-dat") {
        setHostPath(options.sources.scriptsDat, nextArg(i, argc, argv, arg));
      } else if(arg == "--scripts-dat-logical-path") {
        options.sources.scriptsDat.logicalPath = nextArg(i, argc, argv, arg);
      } else if(arg == "--scripts-dat-sha256") {
        options.sources.scriptsDat.sha256 = nextArg(i, argc, argv, arg);
      } else if(arg == "--dialog-ou") {
        setHostPath(options.sources.dialogOu, nextArg(i, argc, argv, arg));
      } else if(arg == "--dialog-ou-logical-path") {
        options.sources.dialogOu.logicalPath = nextArg(i, argc, argv, arg);
      } else if(arg == "--dialog-ou-sha256") {
        options.sources.dialogOu.sha256 = nextArg(i, argc, argv, arg);
      } else if(arg == "--output") {
        outputPath = fs::path(nextArg(i, argc, argv, arg));
      } else {
        throw std::runtime_error("unknown argument: " + std::string(arg));
      }
    }

    if(options.contentRevisionKey.empty() || outputPath.empty()) {
      usageError();
    }

    if(outputPath.has_parent_path()) {
      fs::create_directories(outputPath.parent_path());
    }
    std::ofstream out(outputPath, std::ios::binary);
    if(!out) {
      throw std::runtime_error("failed to open output: " + outputPath.generic_string());
    }

    const auto summary = Mmo::ContentBuild::writeParserSnapshot(out, options);
    std::cout << "content_build_snapshot output=" << outputPath.generic_string()
              << " sources=" << summary.sourceCount
              << " zen_entities=" << summary.zenEntityCount
              << " waypoint_edges=" << summary.waypointEdgeCount
              << " daedalus_symbols=" << summary.daedalusSymbolCount
              << " npc_templates=" << summary.npcTemplateCount
              << " item_templates=" << summary.itemTemplateCount
              << " routines=" << summary.routineCount
              << " perception_bindings=" << summary.perceptionBindingCount
              << " dialog_infos=" << summary.dialogInfoCount
              << " dialog_outputs=" << summary.dialogOutputCount
              << " parser_errors=" << summary.parserErrorCount << "\n";
    return 0;
  } catch(const std::exception& e) {
    std::cerr << "mmo_content_build_importer failed: " << e.what() << "\n";
    return 2;
  }
}



