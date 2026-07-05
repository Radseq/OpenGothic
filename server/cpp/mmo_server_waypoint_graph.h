#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "mmo_server_gameplay_authority.h"

namespace Mmo::Server::Waypoint {

struct TransparentStringHash final {
  using is_transparent = void;

  [[nodiscard]] std::size_t operator()(std::string_view value) const noexcept {
    return std::hash<std::string_view>{}(value);
  }

  [[nodiscard]] std::size_t operator()(const std::string& value) const noexcept {
    return std::hash<std::string_view>{}(value);
  }
};

struct GraphNode final {
  std::string key;
  std::string name;
  std::string kind;
  Gameplay::Vec3 position;
  bool hasPosition = false;
};

struct GraphEdge final {
  std::string fromKey;
  std::string toKey;
  double cost = 0.0;
};

struct NearestWaypoint final {
  const GraphNode* node = nullptr;
  double distanceSq = 0.0;
};

struct GraphStats final {
  std::size_t nodes = 0;
  std::size_t directedEdges = 0;
};

struct PathStepValidation final {
  bool accepted = true;
  const char* reason = "ok";
};

class Graph final {
public:
  [[nodiscard]] bool empty() const noexcept;
  [[nodiscard]] GraphStats stats() const noexcept;
  [[nodiscard]] const GraphNode* findNode(std::string_view key) const noexcept;
  [[nodiscard]] bool hasDirectedEdge(std::string_view fromKey, std::string_view toKey) const noexcept;
  [[nodiscard]] bool areAdjacent(std::string_view aKey, std::string_view bKey) const noexcept;
  [[nodiscard]] std::optional<NearestWaypoint> nearest(Gameplay::Vec3 position,
                                                       double maxDistance) const noexcept;
  [[nodiscard]] PathStepValidation validatePathStep(std::string_view currentKey,
                                                    std::string_view nextKey,
                                                    std::string_view targetKey) const noexcept;

  void clear();
  void addOrUpdateNode(GraphNode node);
  [[nodiscard]] bool addDirectedEdge(GraphEdge edge);
  [[nodiscard]] bool addUndirectedEdge(GraphEdge edge);

private:
  struct EdgeRef final {
    std::size_t to = 0;
    double cost = 0.0;
  };

  [[nodiscard]] std::optional<std::size_t> indexOf(std::string_view key) const noexcept;

  std::vector<GraphNode> nodes_;
  std::vector<std::vector<EdgeRef>> adjacency_;
  std::unordered_map<std::string, std::size_t, TransparentStringHash, std::equal_to<>> indexByKey_;
  std::size_t directedEdges_ = 0;
};

} // namespace Mmo::Server::Waypoint
