#include "mmo_server_waypoint_graph.h"

#include <algorithm>

namespace Mmo::Server::Waypoint {

bool Graph::empty() const noexcept {
  return nodes_.empty();
}

GraphStats Graph::stats() const noexcept {
  return {
    .nodes = nodes_.size(),
    .directedEdges = directedEdges_,
  };
}

std::optional<std::size_t> Graph::indexOf(std::string_view key) const noexcept {
  const auto it = indexByKey_.find(key);
  if(it == indexByKey_.end())
    return std::nullopt;
  return it->second;
}

const GraphNode* Graph::findNode(std::string_view key) const noexcept {
  const auto index = indexOf(key);
  if(!index)
    return nullptr;
  return &nodes_[*index];
}

bool Graph::hasDirectedEdge(std::string_view fromKey, std::string_view toKey) const noexcept {
  const auto from = indexOf(fromKey);
  const auto to = indexOf(toKey);
  if(!from || !to)
    return false;

  const auto& edges = adjacency_[*from];
  return std::find_if(edges.begin(), edges.end(), [to](const EdgeRef& edge) {
    return edge.to == *to;
  }) != edges.end();
}

bool Graph::areAdjacent(std::string_view aKey, std::string_view bKey) const noexcept {
  return hasDirectedEdge(aKey, bKey) || hasDirectedEdge(bKey, aKey);
}

std::optional<NearestWaypoint> Graph::nearest(Gameplay::Vec3 position, double maxDistance) const noexcept {
  if(!Gameplay::finitePosition(position) || maxDistance < 0.0)
    return std::nullopt;

  const double maxDistanceSq = maxDistance * maxDistance;
  std::optional<NearestWaypoint> best;
  for(const auto& node : nodes_) {
    if(!node.hasPosition || !Gameplay::finitePosition(node.position))
      continue;
    const double distSq = Gameplay::distanceSq3d(position, node.position);
    if(distSq > maxDistanceSq)
      continue;
    if(!best || distSq < best->distanceSq)
      best = NearestWaypoint{.node = &node, .distanceSq = distSq};
  }
  return best;
}

PathStepValidation Graph::validatePathStep(std::string_view currentKey,
                                           std::string_view nextKey,
                                           std::string_view targetKey) const noexcept {
  if(currentKey.empty() && nextKey.empty() && targetKey.empty())
    return {};
  if(!currentKey.empty() && findNode(currentKey) == nullptr)
    return {false, "waypoint_current_unknown"};
  if(!nextKey.empty() && findNode(nextKey) == nullptr)
    return {false, "waypoint_next_unknown"};
  if(!targetKey.empty() && findNode(targetKey) == nullptr)
    return {false, "waypoint_target_unknown"};
  if(!currentKey.empty() && !nextKey.empty() && !hasDirectedEdge(currentKey, nextKey))
    return {false, "waypoint_step_not_adjacent"};
  return {};
}

void Graph::clear() {
  nodes_.clear();
  adjacency_.clear();
  indexByKey_.clear();
  directedEdges_ = 0;
}

void Graph::addOrUpdateNode(GraphNode node) {
  if(node.key.empty())
    return;

  const auto existing = indexOf(node.key);
  if(existing) {
    nodes_[*existing] = std::move(node);
    return;
  }

  const std::size_t index = nodes_.size();
  indexByKey_.emplace(node.key, index);
  nodes_.push_back(std::move(node));
  adjacency_.emplace_back();
}

bool Graph::addDirectedEdge(GraphEdge edge) {
  const auto from = indexOf(edge.fromKey);
  const auto to = indexOf(edge.toKey);
  if(!from || !to || *from == *to)
    return false;
  if(hasDirectedEdge(edge.fromKey, edge.toKey))
    return true;

  adjacency_[*from].push_back(EdgeRef{.to = *to, .cost = edge.cost});
  ++directedEdges_;
  return true;
}

bool Graph::addUndirectedEdge(GraphEdge edge) {
  const bool forward = addDirectedEdge(edge);
  std::swap(edge.fromKey, edge.toKey);
  const bool backward = addDirectedEdge(std::move(edge));
  return forward && backward;
}

} // namespace Mmo::Server::Waypoint
