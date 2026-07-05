// Internal implementation partition for mmo_udp_server.cpp.
// Kept include-based during the monolith split to preserve behavior.

void enqueueOutbox(const MySqlTarget& target,
                   std::string_view sessionUuid,
                   const Mmo::Net::ClientActionPacket& packet,
                   std::string_view dbPayload,
                   int priority,
                   int maxAttempts) {
  const auto* def = Mmo::findSemanticAction(packet.kind);
  const std::string_view actionName = def ? def->actionKind : std::string_view("unknown");
  const Mmo::Server::OutboxActionRecord record {
    .sessionUuid = sessionUuid,
    .actionName = actionName,
    .targetKey = packet.targetKey,
    .dbPayload = dbPayload,
    .idempotencyKey = packet.idempotencyKey,
    .priority = priority,
    .maxAttempts = maxAttempts,
  };
  Mmo::Server::enqueueOutboxAction(target, record);
}

void applyCharacterCheckpoint(const MySqlTarget& target,
                              std::string_view sessionUuid,
                              const Mmo::Net::ClientActionPacket& packet,
                              std::string_view dbPayload) {
  const std::string_view payload = packet.payloadJson;
  const auto serverTick = packetServerTick(packet);
  const auto waypoint = jsonStringField(payload, "current_waypoint_key").value_or("");
  const Mmo::Server::CharacterCheckpointRecord record {
    .sessionUuid = sessionUuid,
    .serverTick = serverTick,
    .posX = requiredJsonDouble(payload, "pos_x"),
    .posY = requiredJsonDouble(payload, "pos_y"),
    .posZ = requiredJsonDouble(payload, "pos_z"),
    .rotationYaw = requiredJsonDouble(payload, "rotation_yaw"),
    .waypoint = waypoint,
    .level = requiredJsonI64(payload, "level"),
    .experience = requiredJsonI64(payload, "experience"),
    .experienceNext = requiredJsonI64(payload, "experience_next"),
    .learningPoints = requiredJsonI64(payload, "learning_points"),
    .healthCurrent = requiredJsonI64(payload, "health_current"),
    .healthMax = requiredJsonI64(payload, "health_max"),
    .manaCurrent = requiredJsonI64(payload, "mana_current"),
    .manaMax = requiredJsonI64(payload, "mana_max"),
    .strength = requiredJsonI64(payload, "strength"),
    .dexterity = requiredJsonI64(payload, "dexterity"),
    .guild = optionalJsonI64(payload, "guild", 0),
    .trueGuild = optionalJsonI64(payload, "true_guild", 0),
    .permanentAttitude = optionalJsonI64(payload, "permanent_attitude", 0),
    .temporaryAttitude = optionalJsonI64(payload, "temporary_attitude", 0),
    .dbPayload = dbPayload,
    .idempotencyKey = packet.idempotencyKey,
  };
  Mmo::Server::recordCharacterCheckpoint(target, record);
}


void applySaveCheckpointManifest(const MySqlTarget& target,
                                 std::string_view sessionUuid,
                                 const Mmo::Net::ClientActionPacket& packet,
                                 std::string_view dbPayload) {
  const std::string_view payload = packet.payloadJson;
  const auto serverTick = packetServerTick(packet);
  const auto manifestKey = optionalJsonString(payload, "manifest_key", packet.targetKey);
  const auto checkpointKind = optionalJsonString(payload, "checkpoint_kind", "native_save");
  const auto reason = optionalJsonString(payload, "reason", "save_checkpoint_manifest");
  const Mmo::Server::SaveCheckpointManifestRecord record {
    .sessionUuid = sessionUuid,
    .manifestKey = manifestKey,
    .checkpointKind = checkpointKind,
    .reason = reason,
    .serverTick = serverTick,
    .dbPayload = dbPayload,
    .idempotencyKey = packet.idempotencyKey,
  };
  Mmo::Server::createSaveCheckpointManifest(target, record);
}

void callCheckpoint(const MySqlTarget& target,
                    std::string_view sessionUuid,
                    const Mmo::Net::ClientActionPacket& packet,
                    std::string_view dbPayload,
                    double posX,
                    double posY,
                    double posZ,
                    double rotationYaw) {
  const std::string_view payload = packet.payloadJson;
  const auto serverTick = packetServerTick(packet);
  const auto waypoint = optionalJsonString(payload, "current_waypoint_key");
  const Mmo::Server::CharacterCheckpointRecord record {
    .sessionUuid = sessionUuid,
    .serverTick = serverTick,
    .posX = posX,
    .posY = posY,
    .posZ = posZ,
    .rotationYaw = rotationYaw,
    .waypoint = waypoint,
    .level = optionalJsonI64(payload, "level", 0),
    .experience = optionalJsonI64(payload, "experience", 0),
    .experienceNext = optionalJsonI64(payload, "experience_next", 500),
    .learningPoints = optionalJsonI64(payload, "learning_points", 0),
    .healthCurrent = optionalJsonI64(payload, "health_current", 0),
    .healthMax = optionalJsonI64(payload, "health_max", 0),
    .manaCurrent = optionalJsonI64(payload, "mana_current", 0),
    .manaMax = optionalJsonI64(payload, "mana_max", 0),
    .strength = optionalJsonI64(payload, "strength", 0),
    .dexterity = optionalJsonI64(payload, "dexterity", 0),
    .guild = optionalJsonI64(payload, "guild", 0),
    .trueGuild = optionalJsonI64(payload, "true_guild", 0),
    .permanentAttitude = optionalJsonI64(payload, "permanent_attitude", 0),
    .temporaryAttitude = optionalJsonI64(payload, "temporary_attitude", 0),
    .dbPayload = dbPayload,
    .idempotencyKey = packet.idempotencyKey,
  };
  Mmo::Server::recordCharacterCheckpoint(target, record);
}

[[nodiscard]] DirectApplyResult applyMovementProposal(const MySqlTarget& target,
                                                      std::string_view sessionUuid,
                                                      const Mmo::Net::ClientActionPacket& packet,
                                                      std::string_view dbPayload) {
  const std::string_view payload = packet.payloadJson;
  const double fromX = requiredJsonDouble(payload, "from_pos_x");
  const double fromY = requiredJsonDouble(payload, "from_pos_y");
  const double fromZ = requiredJsonDouble(payload, "from_pos_z");
  const double toX = requiredJsonDouble(payload, "to_pos_x");
  const double toY = requiredJsonDouble(payload, "to_pos_y");
  const double toZ = requiredJsonDouble(payload, "to_pos_z");
  const double yaw = optionalJsonDouble(payload, "to_rotation_yaw", optionalJsonDouble(payload, "rotation_yaw", 0.0));
  const auto fromTick = optionalJsonI64(payload, "from_tick", 0);
  const auto toTick = optionalJsonI64(payload, "to_tick", static_cast<std::int64_t>(packet.clientTick));
  const auto deltaMs = optionalJsonI64(payload, "delta_ms", toTick - fromTick);

  const Mmo::Server::MovementProposalInput movement {
    .fromX = fromX,
    .fromY = fromY,
    .fromZ = fromZ,
    .toX = toX,
    .toY = toY,
    .toZ = toZ,
    .deltaMs = deltaMs,
  };
  const auto validation = Mmo::Server::validateMovementProposal(movement);

  if(!validation.accepted) {
    std::cerr << "[movement_rejected]"
              << " delta_ms=" << deltaMs
              << " total=" << validation.totalDistance
              << " horizontal_speed=" << validation.horizontalSpeed
              << " vertical_delta=" << validation.verticalDelta
              << " vertical_speed=" << validation.verticalSpeed
              << " stale_tiny=" << (validation.staleTinyDelta ? 1 : 0)
              << "\n";
    return {true, false, false, "movement_rejected"};
  }

  if(validation.staleTinyDelta) {
    std::cout << "[movement_stale_delta_accepted]"
              << " delta_ms=" << deltaMs
              << " total=" << validation.totalDistance
              << " vertical_delta=" << validation.verticalDelta
              << "\n";
  }
  callCheckpoint(target, sessionUuid, packet, dbPayload, toX, toY, toZ, yaw);
  return {true, true, true, "movement_checkpoint"};
}

void recordClientActionCorrection(const MySqlTarget& target,
                                  std::string_view sessionUuid,
                                  const Mmo::Net::ClientActionPacket& packet,
                                  std::string_view actionName,
                                  std::string_view reason,
                                  std::string_view dbPayload) {
  const auto tick = packetServerTick(packet);
  std::string idempotency = packet.idempotencyKey;
  idempotency += ":correction";
  const Mmo::Server::ClientActionCorrectionRecord record {
    .sessionUuid = sessionUuid,
    .actionName = actionName,
    .localSequence = packet.localSequence,
    .correctionKind = "rollback_to_authoritative_position",
    .reason = reason,
    .serverTick = tick,
    .dbPayload = dbPayload,
    .idempotencyKey = idempotency,
  };
  Mmo::Server::recordClientActionCorrection(target, record);
}
