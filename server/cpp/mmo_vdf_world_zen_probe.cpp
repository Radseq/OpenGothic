#include <zenkit/Stream.hh>
#include <zenkit/Vfs.hh>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace fs = std::filesystem;

struct Options final {
  fs::path gothicRoot;
  fs::path outputRoot = fs::path("runtime/content_build/vfs_extracted");
  fs::path reportPath = fs::path("runtime/content_build/vdf_world_zen_probe.json");
  std::string worldName = "newworld.zen";
  bool extract = false;
};

struct ArchiveInfo final {
  fs::path path;
  std::uintmax_t byteSize = 0;
  bool isMod = false;
};

struct Candidate final {
  std::string vfsPath;
  std::string normalized;
  bool authoritative = false;
  int priority = 10000;
};

[[nodiscard]] std::string lowerAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

[[nodiscard]] bool endsWith(std::string_view text, std::string_view suffix) noexcept {
  return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

[[nodiscard]] bool contains(std::string_view text, std::string_view needle) noexcept {
  return text.find(needle) != std::string_view::npos;
}

[[nodiscard]] std::string filenameOf(std::string_view path) {
  const auto pos = path.find_last_of("/\\");
  if(pos == std::string_view::npos) {
    return std::string(path);
  }
  return std::string(path.substr(pos + 1));
}

[[nodiscard]] std::string jsonEscape(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 8);
  for(unsigned char ch : text) {
    switch(ch) {
      case '\\': out += "\\\\"; break;
      case '"': out += "\\\""; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if(ch < 0x20) {
          constexpr char hex[] = "0123456789abcdef";
          out += "\\u00";
          out.push_back(hex[(ch >> 4) & 0xF]);
          out.push_back(hex[ch & 0xF]);
        } else {
          out.push_back(static_cast<char>(ch));
        }
        break;
    }
  }
  return out;
}

[[nodiscard]] std::string jsonString(std::string_view text) {
  return "\"" + jsonEscape(text) + "\"";
}

[[noreturn]] void usageError() {
  throw std::runtime_error(
      "usage: mmo_vdf_world_zen_probe --gothic-root PATH "
      "[--world-name newworld.zen] [--extract] [--output-root PATH] [--report PATH]");
}

[[nodiscard]] std::string nextArg(int& index, int argc, char** argv, std::string_view name) {
  if(index + 1 >= argc) {
    throw std::runtime_error("missing value for " + std::string(name));
  }
  ++index;
  return argv[index];
}

[[nodiscard]] Options parseArgs(int argc, char** argv) {
  Options options;
  for(int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if(arg == "--help" || arg == "-h") {
      usageError();
    } else if(arg == "--gothic-root") {
      options.gothicRoot = fs::path(nextArg(i, argc, argv, arg));
    } else if(arg == "--world-name") {
      options.worldName = lowerAscii(nextArg(i, argc, argv, arg));
      if(!endsWith(options.worldName, ".zen")) {
        options.worldName += ".zen";
      }
    } else if(arg == "--extract") {
      options.extract = true;
    } else if(arg == "--output-root") {
      options.outputRoot = fs::path(nextArg(i, argc, argv, arg));
    } else if(arg == "--report") {
      options.reportPath = fs::path(nextArg(i, argc, argv, arg));
    } else {
      throw std::runtime_error("unknown argument: " + std::string(arg));
    }
  }
  if(options.gothicRoot.empty()) {
    usageError();
  }
  return options;
}

[[nodiscard]] std::vector<ArchiveInfo> findArchives(const fs::path& gothicRoot) {
  const fs::path dataRoot = gothicRoot / "Data";
  const fs::path scanRoot = fs::exists(dataRoot) ? dataRoot : gothicRoot;
  std::vector<ArchiveInfo> archives;
  for(const auto& entry : fs::recursive_directory_iterator(scanRoot)) {
    if(!entry.is_regular_file()) {
      continue;
    }
    const auto ext = lowerAscii(entry.path().extension().generic_string());
    if(ext != ".vdf" && ext != ".mod") {
      continue;
    }
    ArchiveInfo info;
    info.path = entry.path();
    info.byteSize = entry.file_size();
    info.isMod = ext == ".mod";
    if(info.byteSize > 0) {
      archives.push_back(std::move(info));
    }
  }
  std::stable_sort(archives.begin(), archives.end(), [](const ArchiveInfo& lhs, const ArchiveInfo& rhs) {
    if(lhs.isMod != rhs.isMod) {
      return lhs.isMod > rhs.isMod;
    }
    return lhs.path.generic_string() < rhs.path.generic_string();
  });
  return archives;
}

[[nodiscard]] bool isAuthoritativeWorldZen(std::string_view normalized) {
  const auto name = filenameOf(normalized);
  if(name == "lensflare.zen") {
    return false;
  }
  if(contains(normalized, "/presets/")) {
    return false;
  }
  return name == "newworld.zen" || name == "oldworld.zen" || name == "addonworld.zen" ||
         contains(normalized, "worlds/") || contains(normalized, "/worlds/");
}

[[nodiscard]] int priorityFor(std::string_view normalized, std::string_view wantedWorld) {
  const auto name = filenameOf(normalized);
  if(name == wantedWorld) {
    return 0;
  }
  if(normalized == wantedWorld) {
    return 1;
  }
  if(name == "newworld.zen") {
    return 10;
  }
  if(name == "oldworld.zen") {
    return 20;
  }
  if(name == "addonworld.zen") {
    return 30;
  }
  return 1000;
}

[[nodiscard]] std::vector<Candidate> findZenCandidates(const zenkit::Vfs& vfs, std::string_view wantedWorld) {
  std::vector<Candidate> candidates;
  std::vector<std::string> probes;
  auto addProbe = [&probes](std::string value) {
    if(value.empty()) {
      return;
    }
    if(std::find(probes.begin(), probes.end(), value) == probes.end()) {
      probes.push_back(std::move(value));
    }
  };
  auto addWorldProbeSet = [&addProbe](std::string name) {
    const auto lower = lowerAscii(name);
    auto upper = lower;
    std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) {
      return static_cast<char>(std::toupper(c));
    });
    addProbe(lower);
    addProbe(upper);
    addProbe("worlds/" + lower);
    addProbe("Worlds/" + lower);
    addProbe("WORLDS/" + upper);
    addProbe("data/worlds/" + lower);
    addProbe("Data/Worlds/" + lower);
    addProbe("_work/data/worlds/" + lower);
    addProbe("_work/Data/Worlds/" + lower);
  };

  addWorldProbeSet(std::string(wantedWorld));
  addWorldProbeSet("newworld.zen");
  addWorldProbeSet("oldworld.zen");
  addWorldProbeSet("addonworld.zen");
  addWorldProbeSet("world.zen");

  std::vector<std::string> seenNormalized;
  for(const auto& raw : probes) {
    if(vfs.find(raw) == nullptr) {
      continue;
    }
    Candidate item;
    item.vfsPath = raw;
    item.normalized = lowerAscii(raw);
    std::replace(item.normalized.begin(), item.normalized.end(), '\\', '/');
    if(!endsWith(item.normalized, ".zen")) {
      continue;
    }
    if(std::find(seenNormalized.begin(), seenNormalized.end(), item.normalized) != seenNormalized.end()) {
      continue;
    }
    seenNormalized.push_back(item.normalized);
    item.authoritative = isAuthoritativeWorldZen(item.normalized);
    item.priority = priorityFor(item.normalized, wantedWorld);
    candidates.push_back(std::move(item));
  }
  std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& lhs, const Candidate& rhs) {
    if(lhs.authoritative != rhs.authoritative) {
      return lhs.authoritative > rhs.authoritative;
    }
    if(lhs.priority != rhs.priority) {
      return lhs.priority < rhs.priority;
    }
    return lhs.normalized < rhs.normalized;
  });
  return candidates;
}

void extractCandidate(const zenkit::Vfs& vfs, const Candidate& selected, const fs::path& outputPath) {
  const auto* entry = vfs.find(selected.vfsPath);
  if(entry == nullptr) {
    throw std::runtime_error("selected VFS path disappeared: " + selected.vfsPath);
  }
  auto reader = entry->open_read();
  if(reader == nullptr) {
    throw std::runtime_error("failed to open selected VFS path: " + selected.vfsPath);
  }
  reader->seek(0, zenkit::Whence::END);
  const auto size = static_cast<std::size_t>(reader->tell());
  reader->seek(0, zenkit::Whence::BEG);
  std::vector<std::uint8_t> bytes(size);
  if(!bytes.empty()) {
    reader->read(bytes.data(), bytes.size());
  }
  fs::create_directories(outputPath.parent_path());
  std::ofstream out(outputPath, std::ios::binary);
  if(!out) {
    throw std::runtime_error("failed to open extraction output: " + outputPath.generic_string());
  }
  out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void writeReport(const Options& options,
                 const std::vector<ArchiveInfo>& archives,
                 const std::vector<Candidate>& candidates,
                 const Candidate* selected,
                 const fs::path& extractedPath,
                 std::string_view status) {
  if(options.reportPath.has_parent_path()) {
    fs::create_directories(options.reportPath.parent_path());
  }
  std::ofstream out(options.reportPath, std::ios::binary);
  if(!out) {
    throw std::runtime_error("failed to open report: " + options.reportPath.generic_string());
  }
  out << "{\n";
  out << "  \"schema\": \"mmo.vdf_world_zen_probe.v1\",\n";
  out << "  \"tool\": \"mmo_vdf_world_zen_probe\",\n";
  out << "  \"status\": " << jsonString(status) << ",\n";
  out << "  \"gothic_root\": " << jsonString(options.gothicRoot.generic_string()) << ",\n";
  out << "  \"world_name\": " << jsonString(options.worldName) << ",\n";
  out << "  \"archive_count\": " << archives.size() << ",\n";
  out << "  \"zen_candidate_count\": " << candidates.size() << ",\n";
  out << "  \"extracted_host_path\": " << jsonString(extractedPath.empty() ? "" : extractedPath.generic_string()) << ",\n";
  out << "  \"selected\": ";
  if(selected == nullptr) {
    out << "null";
  } else {
    out << "{\"vfs_path\":" << jsonString(selected->vfsPath)
        << ",\"normalized\":" << jsonString(selected->normalized)
        << ",\"authoritative\":" << (selected->authoritative ? "true" : "false")
        << ",\"priority\":" << selected->priority << "}";
  }
  out << ",\n";
  out << "  \"archives\": [\n";
  for(std::size_t i = 0; i < archives.size(); ++i) {
    const auto& archive = archives[i];
    out << "    {\"host_path\":" << jsonString(archive.path.generic_string())
        << ",\"byte_size\":" << archive.byteSize
        << ",\"is_mod\":" << (archive.isMod ? "true" : "false") << "}";
    out << (i + 1 == archives.size() ? "\n" : ",\n");
  }
  out << "  ],\n";
  out << "  \"zen_candidates\": [\n";
  for(std::size_t i = 0; i < candidates.size(); ++i) {
    const auto& candidate = candidates[i];
    out << "    {\"vfs_path\":" << jsonString(candidate.vfsPath)
        << ",\"normalized\":" << jsonString(candidate.normalized)
        << ",\"authoritative\":" << (candidate.authoritative ? "true" : "false")
        << ",\"priority\":" << candidate.priority << "}";
    out << (i + 1 == candidates.size() ? "\n" : ",\n");
  }
  out << "  ]\n";
  out << "}\n";
}

} // namespace

int main(int argc, char** argv) {
  try {
    const auto options = parseArgs(argc, argv);
    if(!fs::exists(options.gothicRoot) || !fs::is_directory(options.gothicRoot)) {
      throw std::runtime_error("gothic root does not exist or is not a directory: " + options.gothicRoot.generic_string());
    }

    const auto archives = findArchives(options.gothicRoot);
    zenkit::Vfs vfs;
    for(const auto& archive : archives) {
      try {
        vfs.mount_disk(archive.path, zenkit::VfsOverwriteBehavior::OLDER);
      } catch(const std::exception& e) {
        std::cerr << "warning: failed to mount archive " << archive.path.generic_string() << ": " << e.what() << "\n";
      }
    }

    const auto candidates = findZenCandidates(vfs, options.worldName);
    const Candidate* selected = nullptr;
    for(const auto& candidate : candidates) {
      if(candidate.authoritative) {
        selected = &candidate;
        break;
      }
    }

    fs::path extractedPath;
    std::string status = selected == nullptr ? "no_world_zen_found" : "world_zen_found";
    if(options.extract && selected != nullptr) {
      extractedPath = options.outputRoot / filenameOf(selected->normalized);
      extractCandidate(vfs, *selected, extractedPath);
      status = "world_zen_extracted";
    }

    writeReport(options, archives, candidates, selected, extractedPath, status);
    std::cout << "status=" << status << "\n";
    std::cout << "archive_count=" << archives.size() << "\n";
    std::cout << "zen_candidate_count=" << candidates.size() << "\n";
    if(selected != nullptr) {
      std::cout << "selected=" << selected->vfsPath << "\n";
    }
    if(!extractedPath.empty()) {
      std::cout << "extracted=" << extractedPath.generic_string() << "\n";
    }
    std::cout << "report=" << options.reportPath.generic_string() << "\n";
    return selected == nullptr ? 4 : 0;
  } catch(const std::exception& e) {
    std::cerr << "mmo_vdf_world_zen_probe failed: " << e.what() << "\n";
    return 2;
  }
}
