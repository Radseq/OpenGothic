// Internal implementation partition for mmo_udp_server.cpp.
// Handles story/script/dialog/progression direct DB actions.

[[nodiscard]] DirectApplyResult applyStoryDirectDb(const Mmo::Server::DirectApplyRequest& request) {
  const MySqlTarget& target = request.target;
  const std::string_view sessionUuid = request.sessionUuid;
  const Mmo::Net::ClientActionPacket& packet = request.packet;
  const std::string_view dbPayload = request.dbPayload;
  const std::string_view payload = packet.payloadJson;
  const auto tick = packetServerTick(packet);

  if(packet.kind == Mmo::SemanticActionKind::SetScriptInt) {
    const auto command = Mmo::Server::Script::buildSetIntCommand({
      .scriptKey = optionalJsonString(payload, "script_key"),
      .globalKey = optionalJsonString(payload, "global_key"),
      .symbolName = optionalJsonString(payload, "symbol_name"),
      .targetKey = packet.targetKey,
      .symbolIndex = optionalJsonI64(payload, "symbol_index", 0),
      .valueIndex = optionalJsonI64(payload, "value_index", 0),
      .valueAfter = optionalJsonI64(payload, "value_after", optionalJsonI64(payload, "value", 0)),
    });
    if(!command.accepted)
      return {true, false, false, command.reason};
    Mmo::Server::setCharacterScriptInt(target, {
      .sessionUuid = sessionUuid,
      .scriptKey = command.scriptKey,
      .symbolIndex = command.symbolIndex,
      .valueIndex = command.valueIndex,
      .valueAfter = command.valueAfter,
      .serverTick = tick,
      .dbPayload = dbPayload,
      .idempotencyKey = packet.idempotencyKey,
    });
    return {true, true, true, "direct_applied"};
  }

  if(packet.kind == Mmo::SemanticActionKind::UpdateQuest) {
    const auto command = Mmo::Server::Quest::buildUpdateCommand({
      .questKey = optionalJsonString(payload, "quest_key"),
      .topic = optionalJsonString(payload, "topic"),
      .targetKey = packet.targetKey,
      .questName = optionalJsonString(payload, "quest_name", optionalJsonString(payload, "name")),
      .rawStatus = optionalJsonString(payload, "status", "running"),
      .previousStatus = optionalJsonString(payload, "previous_status"),
      .entryCount = optionalJsonI64(payload, "entry_count", 0),
      .allowTerminalReopen = optionalJsonBool(payload, "allow_terminal_reopen", false),
    });
    if(!command.accepted)
      return {true, false, false, command.reason};
    Mmo::Server::updateCharacterQuest(target, {
      .sessionUuid = sessionUuid,
      .questKey = command.questKey,
      .questName = command.questName,
      .status = command.status,
      .entryCount = command.entryCount,
      .serverTick = tick,
      .dbPayload = dbPayload,
      .idempotencyKey = packet.idempotencyKey,
    });
    return {true, true, true, "direct_applied"};
  }

  if(packet.kind == Mmo::SemanticActionKind::SetKnownDialog) {
    const auto command = Mmo::Server::Dialog::buildKnownDialogCommand({
      .npcKey = optionalJsonString(payload, "npc_key"),
      .npcSymbolName = optionalJsonString(payload, "npc_symbol_name"),
      .infoKey = optionalJsonString(payload, "info_key"),
      .infoSymbolName = optionalJsonString(payload, "info_symbol_name"),
      .targetKey = packet.targetKey,
      .availabilityState = optionalJsonString(payload, "availability_state"),
      .known = jsonBoolField(payload, "known"),
      .removed = jsonBoolField(payload, "removed"),
      .permanent = jsonBoolField(payload, "permanent"),
      .repeatable = jsonBoolField(payload, "repeatable"),
    });
    if(!command.accepted)
      return {true, false, false, command.reason};
    Mmo::Server::setCharacterKnownDialog(target, {
      .sessionUuid = sessionUuid,
      .npcKey = command.npcKey,
      .infoKey = command.infoKey,
      .known = command.known,
      .permanent = command.permanent,
      .availability = command.availability,
      .serverTick = tick,
      .dbPayload = dbPayload,
      .idempotencyKey = packet.idempotencyKey,
    });
    return {true, true, true, "direct_applied"};
  }

  if(packet.kind == Mmo::SemanticActionKind::AdjustProgression) {
    const auto xp = optionalJsonI64(payload, "experience_delta", optionalJsonI64(payload, "xp_delta", optionalJsonI64(payload, "delta", 0)));
    const auto lp = optionalJsonI64(payload, "learning_points_delta", optionalJsonI64(payload, "lp_delta", 0));
    const auto reason = optionalJsonString(payload, "reason", "script_progression");
    const auto validation = Mmo::Server::Combat::validateProgressionReward({
      .experienceDelta = xp,
      .learningPointsDelta = lp,
      .reason = reason,
    });
    if(!validation.accepted)
      return {true, false, false, validation.reason};
    Mmo::Server::adjustCharacterProgression(target, {
      .sessionUuid = sessionUuid,
      .experienceDelta = xp,
      .learningPointsDelta = lp,
      .reason = reason,
      .serverTick = tick,
      .dbPayload = dbPayload,
      .idempotencyKey = packet.idempotencyKey,
    });
    return {true, true, true, "direct_applied"};
  }

  if(packet.kind == Mmo::SemanticActionKind::ApplyExperienceReward) {
    const auto xp = optionalJsonI64(payload, "experience_delta", optionalJsonI64(payload, "xp_delta", optionalJsonI64(payload, "delta", 0)));
    const auto reason = optionalJsonString(payload, "reason", "script_experience_reward");
    const auto validation = Mmo::Server::Combat::validateProgressionReward({
      .experienceDelta = xp,
      .learningPointsDelta = 0,
      .reason = reason,
    });
    if(!validation.accepted)
      return {true, false, false, validation.reason};
    Mmo::Server::applyCharacterExperienceReward(target, {
      .sessionUuid = sessionUuid,
      .experienceDelta = xp,
      .reason = reason,
      .serverTick = tick,
      .dbPayload = dbPayload,
      .idempotencyKey = packet.idempotencyKey,
    });
    return {true, true, true, "direct_applied"};
  }

  return {false, true, false, "unhandled"};
}
