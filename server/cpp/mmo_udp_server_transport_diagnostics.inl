// Internal implementation partition for mmo_udp_server.cpp.
// Kept include-based during the monolith split to preserve behavior.

[[nodiscard]] std::optional<JsonVec3> movementToPosition(std::string_view payload) {
  const auto x = optionalJsonDouble(payload, "to_pos_x", std::numeric_limits<double>::quiet_NaN());
  const auto y = optionalJsonDouble(payload, "to_pos_y", std::numeric_limits<double>::quiet_NaN());
  const auto z = optionalJsonDouble(payload, "to_pos_z", std::numeric_limits<double>::quiet_NaN());
  if(!finiteCoord(x) || !finiteCoord(y) || !finiteCoord(z))
    return std::nullopt;
  return JsonVec3{x, y, z};
}

[[nodiscard]] bool shouldSendLiveWorldSnapshot(LiveWorldSnapshotState& state,
                                              const Mmo::Net::ClientActionPacket& packet,
                                              bool packetAccepted,
                                              const DirectApplyResult& direct) {
  if(packet.kind != Mmo::SemanticActionKind::MovementProposal || !packetAccepted || !direct.handled || !direct.accepted)
    return false;

  const auto pos = movementToPosition(packet.payloadJson);
  if(!pos)
    return false;

  const auto tick = packetServerTick(packet);

  if(!state.initialized) {
    state.initialized = true;
    state.lastX = pos->x;
    state.lastY = pos->y;
    state.lastZ = pos->z;
    state.lastTick = tick;
    return false;
  }

  const double dist = distance3d(pos->x, pos->y, pos->z, state.lastX, state.lastY, state.lastZ);
  const auto elapsed = tick >= state.lastTick ? tick - state.lastTick : 0;
  if(dist < Mmo::Server::LiveWorldItemRefreshDistance &&
     !(elapsed >= Mmo::Server::LiveWorldItemRefreshMaxIntervalMs &&
       dist >= Mmo::Server::LiveWorldItemRefreshMinMoveDistance))
    return false;

  state.lastX = pos->x;
  state.lastY = pos->y;
  state.lastZ = pos->z;
  state.lastTick = tick;
  return true;
}

[[nodiscard]] Mmo::Net::ServerLiveDeltaKind serverLiveDeltaKindForAction(Mmo::SemanticActionKind kind) noexcept {
  switch(kind) {
    case Mmo::SemanticActionKind::MovementProposal:
    case Mmo::SemanticActionKind::CharacterCheckpoint:
      return Mmo::Net::ServerLiveDeltaKind::MovementCorrection;
    case Mmo::SemanticActionKind::ApplyCharacterDamage:
    case Mmo::SemanticActionKind::ApplyCharacterResourceDelta:
    case Mmo::SemanticActionKind::ConsumeMana:
    case Mmo::SemanticActionKind::AdjustProgression:
    case Mmo::SemanticActionKind::ApplyExperienceReward:
      return Mmo::Net::ServerLiveDeltaKind::CharacterStats;
    case Mmo::SemanticActionKind::WalletDelta:
    case Mmo::SemanticActionKind::GrantGold:
    case Mmo::SemanticActionKind::SpendGold:
    case Mmo::SemanticActionKind::PickupWorldItem:
    case Mmo::SemanticActionKind::TransferCharacterItem:
    case Mmo::SemanticActionKind::DropCharacterItem:
    case Mmo::SemanticActionKind::LootNpcInventory:
    case Mmo::SemanticActionKind::TakeContainerItem:
    case Mmo::SemanticActionKind::PutContainerItem:
    case Mmo::SemanticActionKind::TradeBuyFromNpc:
    case Mmo::SemanticActionKind::TradeSellToNpc:
    case Mmo::SemanticActionKind::ConsumeItem:
    case Mmo::SemanticActionKind::SplitItemStack:
    case Mmo::SemanticActionKind::MergeItemStack:
      return Mmo::Net::ServerLiveDeltaKind::Inventory;
    case Mmo::SemanticActionKind::EquipCharacterItem:
    case Mmo::SemanticActionKind::UnequipCharacterItem:
    case Mmo::SemanticActionKind::ReadyWeapon:
    case Mmo::SemanticActionKind::HolsterWeapon:
      return Mmo::Net::ServerLiveDeltaKind::Equipment;
    case Mmo::SemanticActionKind::RemoveWorldItem:
      return Mmo::Net::ServerLiveDeltaKind::WorldItem;
    case Mmo::SemanticActionKind::UseInteractive:
    case Mmo::SemanticActionKind::UpdateInteractiveState:
    case Mmo::SemanticActionKind::TriggerEvent:
    case Mmo::SemanticActionKind::MoverStateChanged:
    case Mmo::SemanticActionKind::RecordTriggerQueueState:
    case Mmo::SemanticActionKind::RecordWorldTransitionState:
    case Mmo::SemanticActionKind::WorldTimeChanged:
      return Mmo::Net::ServerLiveDeltaKind::InteractiveState;
    case Mmo::SemanticActionKind::MarkNpcDead:
    case Mmo::SemanticActionKind::RespawnNpc:
    case Mmo::SemanticActionKind::ApplyWorldEntityDamage:
    case Mmo::SemanticActionKind::RecordCombatIntent:
      return Mmo::Net::ServerLiveDeltaKind::Combat;
    case Mmo::SemanticActionKind::SetScriptInt:
    case Mmo::SemanticActionKind::UpdateQuest:
    case Mmo::SemanticActionKind::SetKnownDialog:
    case Mmo::SemanticActionKind::RecordNpcDialogLine:
    case Mmo::SemanticActionKind::SaveCheckpointManifest:
      return Mmo::Net::ServerLiveDeltaKind::Story;
    case Mmo::SemanticActionKind::ClientBootstrapRequest:
    case Mmo::SemanticActionKind::ClientCorrectionAck:
    case Mmo::SemanticActionKind::RecordNpcRoutineState:
    case Mmo::SemanticActionKind::RecordNpcAiState:
    case Mmo::SemanticActionKind::RecordNpcPathState:
    case Mmo::SemanticActionKind::RecordNpcFightState:
    case Mmo::SemanticActionKind::RecordNpcActionState:
      return Mmo::Net::ServerLiveDeltaKind::Generic;
  }
  return Mmo::Net::ServerLiveDeltaKind::Generic;
}

[[nodiscard]] bool isLiveDeltaAction(Mmo::SemanticActionKind kind) noexcept {
  if(kind == Mmo::SemanticActionKind::ClientBootstrapRequest ||
     kind == Mmo::SemanticActionKind::ClientCorrectionAck ||
     kind == Mmo::SemanticActionKind::RecordNpcRoutineState ||
     kind == Mmo::SemanticActionKind::RecordNpcAiState ||
     kind == Mmo::SemanticActionKind::RecordNpcPathState ||
     kind == Mmo::SemanticActionKind::RecordNpcFightState ||
     kind == Mmo::SemanticActionKind::RecordNpcActionState)
    return false;
  return true;
}

[[nodiscard]] bool shouldSendLiveDelta(const Mmo::Net::ClientActionPacket& packet,
                                       bool packetAccepted,
                                       const DirectApplyResult& direct) noexcept {
  return packetAccepted && direct.handled && direct.accepted && isLiveDeltaAction(packet.kind);
}

[[nodiscard]] std::int32_t clampLiveDeltaI32(std::int64_t value) noexcept {
  if(value < std::numeric_limits<std::int32_t>::min())
    return std::numeric_limits<std::int32_t>::min();
  if(value > std::numeric_limits<std::int32_t>::max())
    return std::numeric_limits<std::int32_t>::max();
  return static_cast<std::int32_t>(value);
}

[[nodiscard]] bool readLiveDeltaI32(std::string_view payload,
                                    std::string_view key,
                                    std::int32_t& out) {
  if(!jsonNumberTextField(payload, key))
    return false;
  out = clampLiveDeltaI32(optionalJsonI64(payload, key, 0));
  return true;
}

void populateLiveDeltaPosition(const Mmo::Net::ClientActionPacket& packet,
                               Mmo::Net::ServerLiveDeltaPacket& delta) {
  const auto pos = movementToPosition(packet.payloadJson);
  if(!pos)
    return;

  delta.posX = pos->x;
  delta.posY = pos->y;
  delta.posZ = pos->z;
  delta.yaw = optionalJsonDouble(packet.payloadJson,
                                 "to_rotation_yaw",
                                 optionalJsonDouble(packet.payloadJson,
                                                    "rotation_yaw",
                                                    optionalJsonDouble(packet.payloadJson, "yaw", 0.0)));
  delta.flags |= Mmo::Net::ServerLiveDeltaHasPosition;
}

void populateLiveDeltaStats(const Mmo::Net::ClientActionPacket& packet,
                            Mmo::Net::ServerLiveDeltaPacket& delta) {
  if(packet.kind != Mmo::SemanticActionKind::CharacterCheckpoint &&
     packet.kind != Mmo::SemanticActionKind::MovementProposal) {
    return;
  }

  bool complete = true;
  complete = readLiveDeltaI32(packet.payloadJson, "level", delta.level) && complete;
  complete = readLiveDeltaI32(packet.payloadJson, "experience", delta.experience) && complete;
  complete = readLiveDeltaI32(packet.payloadJson, "experience_next", delta.experienceNext) && complete;
  complete = readLiveDeltaI32(packet.payloadJson, "learning_points", delta.learningPoints) && complete;
  complete = readLiveDeltaI32(packet.payloadJson, "health_current", delta.healthCurrent) && complete;
  complete = readLiveDeltaI32(packet.payloadJson, "health_max", delta.healthMax) && complete;
  complete = readLiveDeltaI32(packet.payloadJson, "mana_current", delta.manaCurrent) && complete;
  complete = readLiveDeltaI32(packet.payloadJson, "mana_max", delta.manaMax) && complete;
  complete = readLiveDeltaI32(packet.payloadJson, "strength", delta.strength) && complete;
  complete = readLiveDeltaI32(packet.payloadJson, "dexterity", delta.dexterity) && complete;
  complete = readLiveDeltaI32(packet.payloadJson, "guild", delta.guild) && complete;
  complete = readLiveDeltaI32(packet.payloadJson, "true_guild", delta.trueGuild) && complete;

  if(complete)
    delta.flags |= Mmo::Net::ServerLiveDeltaHasStats;
}

[[nodiscard]] bool liveDeltaRequiresSnapshotRefresh(Mmo::Net::ServerLiveDeltaKind kind,
                                                    std::uint32_t flags) noexcept {
  if(kind == Mmo::Net::ServerLiveDeltaKind::Inventory ||
     kind == Mmo::Net::ServerLiveDeltaKind::Equipment ||
     kind == Mmo::Net::ServerLiveDeltaKind::WorldItem ||
     kind == Mmo::Net::ServerLiveDeltaKind::InteractiveState ||
     kind == Mmo::Net::ServerLiveDeltaKind::Story)
    return true;
  if(kind == Mmo::Net::ServerLiveDeltaKind::CharacterStats &&
     (flags & Mmo::Net::ServerLiveDeltaHasStats) == 0)
    return true;
  if(kind == Mmo::Net::ServerLiveDeltaKind::Combat)
    return true;
  return false;
}

[[nodiscard]] Mmo::Net::ServerLiveDeltaPacket buildServerLiveDeltaPacket(const Mmo::Net::ClientActionPacket& packet,
                                                                         std::string_view actionName) {
  Mmo::Net::ServerLiveDeltaPacket delta;
  delta.packetSequence = packet.packetSequence;
  delta.localSequence = packet.localSequence;
  delta.serverTick = packetServerTick(packet);
  delta.kind = serverLiveDeltaKindForAction(packet.kind);
  delta.actionKind = std::string(actionName);

  populateLiveDeltaPosition(packet, delta);
  populateLiveDeltaStats(packet, delta);
  if(liveDeltaRequiresSnapshotRefresh(delta.kind, delta.flags))
    delta.flags |= Mmo::Net::ServerLiveDeltaRequiresSnapshotRefresh;

  return delta;
}

[[nodiscard]] bool sendPreparedServerLiveDelta(asio::ip::udp::socket& socket,
                                               const asio::ip::udp::endpoint& remote,
                                               const Mmo::Net::ServerLiveDeltaPacket& delta) noexcept {
  try {
    const auto encoded = Mmo::Net::encodeServerLiveDeltaPacket(delta);
    if(encoded.empty()) {
      std::cerr << "[live_delta_encode_failed] action=" << delta.actionKind << "\n";
      return false;
    }

    asio::error_code ec;
    socket.send_to(asio::buffer(encoded), remote, 0, ec);
    if(ec) {
      std::cerr << "[live_delta_send_failed] action=" << delta.actionKind
                << " error=" << ec.message() << "\n";
      return false;
    }
    return true;
  } catch(const std::exception& exc) {
    std::cerr << "[live_delta_failed] action=" << delta.actionKind
              << " error=" << exc.what() << "\n";
  } catch(...) {
    std::cerr << "[live_delta_failed] action=" << delta.actionKind
              << " error=unknown\n";
  }
  return false;
}

[[nodiscard]] bool sendServerLiveDelta(asio::ip::udp::socket& socket,
                                       const asio::ip::udp::endpoint& remote,
                                       const Mmo::Net::ClientActionPacket& request,
                                       std::string_view actionName,
                                       const DirectApplyResult& direct) noexcept {
  try {
    (void)direct;
    const auto delta = buildServerLiveDeltaPacket(request, actionName);
    return sendPreparedServerLiveDelta(socket, remote, delta);
  } catch(const std::exception& exc) {
    std::cerr << "[live_delta_failed] action=" << actionName
              << " target=" << request.targetKey
              << " error=" << exc.what() << "\n";
  } catch(...) {
    std::cerr << "[live_delta_failed] action=" << actionName
              << " target=" << request.targetKey
              << " error=unknown\n";
  }
  return false;
}

void printPacketProgress(ServerPacketLogState& logState,
                         std::uint64_t accepted,
                         std::uint64_t received,
                         std::uint64_t invalid,
                         std::uint64_t duplicate,
                         std::uint64_t enqueued,
                         std::uint64_t directDb,
                         std::uint64_t unhandled,
                         std::uint64_t failed,
                         std::string_view actionName,
                         bool packetAccepted,
                         bool hasDiagnostic,
                         bool snapshotSent,
                         bool liveDeltaSent,
                         bool isMovement,
                         bool isWeaponState) {
  if(isMovement) {
    if(packetAccepted && !hasDiagnostic) {
      ++logState.suppressedMovementLines;
      if(!snapshotSent && logState.suppressedMovementLines < logState.nextMovementSummaryAt)
        return;
      if(!snapshotSent)
        logState.nextMovementSummaryAt += 100;
      std::cout << "[movement_summary] accepted=" << accepted
                << " received=" << received
                << " movement_lines_suppressed=" << logState.suppressedMovementLines
                << " direct_db=" << directDb
                << " failed=" << failed
                << " snapshot_sent=" << (snapshotSent ? 1 : 0)
                << " live_delta_sent=" << (liveDeltaSent ? 1 : 0)
                << "\n";
      return;
    }

    std::cout << "[movement_result] accepted=" << accepted
              << " received=" << received
              << " invalid=" << invalid
              << " duplicate=" << duplicate
              << " direct_db=" << directDb
              << " failed=" << failed
              << " action=" << actionName
              << " packet_accepted=" << (packetAccepted ? 1 : 0)
              << " diagnostic=" << (hasDiagnostic ? 1 : 0)
              << " live_delta_sent=" << (liveDeltaSent ? 1 : 0)
              << "\n";
    return;
  }

  const bool looksLikeWeaponState = isWeaponState ||
                                    actionName == "ready_weapon" ||
                                    actionName == "holster_weapon";
  if(looksLikeWeaponState && packetAccepted && !hasDiagnostic && !snapshotSent) {
    ++logState.suppressedWeaponStateLines;
    if(logState.suppressedWeaponStateLines < logState.nextWeaponStateSummaryAt)
      return;
    logState.nextWeaponStateSummaryAt += 25;
    std::cout << "[weapon_state_summary] accepted=" << accepted
              << " received=" << received
              << " weapon_state_lines_suppressed=" << logState.suppressedWeaponStateLines
              << " direct_db=" << directDb
              << " failed=" << failed
              << "\n";
    return;
  }

  std::cout << "accepted=" << accepted
            << " received=" << received
            << " invalid=" << invalid
            << " duplicate=" << duplicate
            << " enqueued=" << enqueued
            << " direct_db=" << directDb
            << " unhandled=" << unhandled
            << " failed=" << failed;
  if(liveDeltaSent)
    std::cout << " live_delta_sent=1";
  if(logState.suppressedMovementLines != 0)
    std::cout << " movement_lines_suppressed=" << logState.suppressedMovementLines;
  if(logState.suppressedWeaponStateLines != 0)
    std::cout << " weapon_state_lines_suppressed=" << logState.suppressedWeaponStateLines;
  std::cout << " last=" << actionName << "\n";
}

void printBootstrapAck(const Mmo::Net::ClientActionPacket& packet,
                       std::string_view characterKey,
                       std::string_view worldName,
                       const BootstrapReadiness& readiness,
                       bool dbChecked) {
  std::cout << "bootstrap_ack"
            << " accepted=1"
            << " ready=" << (readiness.ready ? 1 : 0)
            << " db_checked=" << (dbChecked ? 1 : 0)
            << " session=" << packet.sessionKey
            << " character=" << characterKey
            << " world=" << worldName
            << " meta=" << readiness.metaRows
            << " char=" << readiness.characterRows
            << " world_entities=" << readiness.worldEntityRows
            << " inventory=" << readiness.characterInventoryRows
            << " quests=" << readiness.questRows
            << " dialogs=" << readiness.knownDialogRows
            << " script_ints=" << readiness.scriptIntRows
            << " waypoints=" << readiness.waypointRows
            << " waypoint_edges=" << readiness.waypointEdgeRows
            << " world_inventory=" << readiness.worldInventoryRows
            << " interactives=" << readiness.interactiveRows
            << " clock=" << readiness.worldClockRows
            << "\n";
}

void sendBootstrapSnapshot(asio::ip::udp::socket& socket,
                           const asio::ip::udp::endpoint& remote,
                           const Mmo::Net::ClientActionPacket& request,
                           std::uint32_t snapshotId,
                           std::string_view snapshotJson) {
  constexpr std::size_t ChunkBytes = Mmo::Server::BootstrapSnapshotChunkPayloadBytes;
  if(snapshotJson.empty())
    return;
  const std::size_t chunkCountSize = (snapshotJson.size() + ChunkBytes - 1u) / ChunkBytes;
  if(chunkCountSize == 0 || chunkCountSize > 65535u)
    throw std::runtime_error("bootstrap snapshot too large for UDP chunk envelope");

  const auto chunkCount = static_cast<std::uint16_t>(chunkCountSize);
  for(std::uint16_t index = 0; index != chunkCount; ++index) {
    const std::size_t offset = static_cast<std::size_t>(index) * ChunkBytes;
    const std::size_t count = std::min<std::size_t>(ChunkBytes, snapshotJson.size() - offset);
    Mmo::Net::ServerSnapshotChunkPacket chunk;
    chunk.packetSequence = request.packetSequence;
    chunk.localSequence = request.localSequence;
    chunk.snapshotId = snapshotId;
    chunk.chunkIndex = index;
    chunk.chunkCount = chunkCount;
    chunk.totalBytes = static_cast<std::uint32_t>(snapshotJson.size());
    chunk.payloadJsonFragment = std::string(snapshotJson.substr(offset, count));

    const auto packet = Mmo::Net::encodeServerSnapshotChunkPacket(chunk);
    if(packet.empty())
      throw std::runtime_error("failed to encode bootstrap snapshot chunk");
    asio::error_code ec;
    socket.send_to(asio::buffer(packet), remote, 0, ec);
    if(ec)
      throw std::runtime_error("failed to send bootstrap snapshot chunk: " + ec.message());

    if((index + 1u) % 8u == 0u)
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  std::cout << "bootstrap_snapshot_sent"
            << " id=" << snapshotId
            << " bytes=" << snapshotJson.size()
            << " chunks=" << chunkCount
            << "\n";
}

void sendServerDiagnostic(asio::ip::udp::socket& socket,
                          const asio::ip::udp::endpoint& remote,
                          const Mmo::Net::ClientActionPacket& request,
                          std::uint16_t severity,
                          std::string_view actionKind,
                          std::string_view reason,
                          std::string_view message) noexcept {
  try {
    constexpr std::size_t MaxDiagnosticMessageBytes = 4096;
    Mmo::Net::ServerDiagnosticPacket diag;
    diag.packetSequence = request.packetSequence;
    diag.localSequence = request.localSequence;
    diag.severity = severity;
    diag.actionKind = std::string(actionKind);
    diag.reason = std::string(reason);
    diag.message = std::string(message.substr(0, std::min<std::size_t>(message.size(), MaxDiagnosticMessageBytes)));

    const auto encoded = Mmo::Net::encodeServerDiagnosticPacket(diag);
    if(encoded.empty()) {
      std::cerr << "[diagnostic_encode_failed] action=" << actionKind
                << " reason=" << reason << "\n";
      return;
    }

    asio::error_code ec;
    socket.send_to(asio::buffer(encoded), remote, 0, ec);
    if(ec) {
      std::cerr << "[diagnostic_send_failed] action=" << actionKind
                << " reason=" << reason
                << " error=" << ec.message() << "\n";
    }
  } catch(const std::exception& exc) {
    std::cerr << "[diagnostic_failed] action=" << actionKind
              << " reason=" << reason
              << " error=" << exc.what() << "\n";
  } catch(...) {
    std::cerr << "[diagnostic_failed] action=" << actionKind
              << " reason=" << reason
              << " error=unknown\n";
  }
}






