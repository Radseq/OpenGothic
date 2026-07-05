// Internal implementation partition for mmo_udp_server.cpp.
// Implements the current direct-DB authority bridge behind DirectApplyRequest.

[[nodiscard]] DirectApplyResult applyDirectDb(const Mmo::Server::DirectApplyRequest& request) {
  const MySqlTarget& target = request.target;
  const std::string_view sessionUuid = request.sessionUuid;
  const Mmo::Net::ClientActionPacket& packet = request.packet;
  const std::string_view dbPayload = request.dbPayload;

  if(packet.kind == Mmo::SemanticActionKind::ClientBootstrapRequest)
    return {false, true, false, "bootstrap"};
  if(packet.kind == Mmo::SemanticActionKind::CharacterCheckpoint) {
    applyCharacterCheckpoint(target, sessionUuid, packet, dbPayload);
    return {true, true, true, "character_checkpoint"};
  }
  if(packet.kind == Mmo::SemanticActionKind::SaveCheckpointManifest) {
    applySaveCheckpointManifest(target, sessionUuid, packet, dbPayload);
    return {true, true, true, "save_checkpoint_manifest"};
  }
  if(packet.kind == Mmo::SemanticActionKind::MovementProposal)
    return applyMovementProposal(target, sessionUuid, packet, dbPayload);



  constexpr std::array<Mmo::Server::DirectApplyHandler, 5> handlers {
    applyStoryDirectDb,
    applyCombatDirectDb,
    applyWorldStateDirectDb,
    applyInventoryDirectDb,
    applyInteractiveDirectDb,
  };

  for(const auto handler : handlers) {
    const auto result = handler(request);
    if(result.handled)
      return result;
  }

  return {false, true, false, "unhandled"};
}



