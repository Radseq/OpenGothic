// Internal implementation partition for mmo_udp_server.cpp.
// Owns small payload normalizers for story/equipment/world identity.

[[nodiscard]] std::string normalizedEquipmentSlot(std::string_view payload) {
  if(auto slot = jsonStringField(payload, "equipment_slot"); slot && !slot->empty()) {
    if(auto numeric = parseI64(*slot))
      return std::string(Mmo::Server::InventoryAuthority::normalizedNumericEquipmentSlot(*numeric));
    return *slot;
  }
  if(auto slot = jsonStringField(payload, "slot"); slot && !slot->empty()) {
    if(auto numeric = parseI64(*slot))
      return std::string(Mmo::Server::InventoryAuthority::normalizedNumericEquipmentSlot(*numeric));
    return *slot;
  }
  const auto numeric = optionalJsonI64(payload, "slot", 0);
  return std::string(Mmo::Server::InventoryAuthority::normalizedNumericEquipmentSlot(numeric));
}

[[nodiscard]] std::string questStatus(std::string_view payload) {
  std::string value = optionalJsonString(payload, "status", "running");
  for(char& ch : value)
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  if(value == "1" || value == "run" || value == "in_progress")
    return "running";
  if(value == "2" || value == "completed_success" || value == "succeeded")
    return "success";
  if(value == "3" || value == "failure" || value == "completed_failed")
    return "failed";
  if(value == "4" || value == "closed")
    return "obsolete";
  return value.empty() ? "running" : value;
}

[[nodiscard]] std::string scriptKeyFromPayload(const Mmo::Net::ClientActionPacket& packet) {
  const std::string_view payload = packet.payloadJson;
  if(auto key = jsonStringField(payload, "script_key"); key && !key->empty())
    return *key;
  if(auto key = jsonStringField(payload, "global_key"); key && !key->empty())
    return *key;
  if(auto key = jsonStringField(payload, "symbol_name"); key && !key->empty())
    return *key;
  if(!packet.targetKey.empty())
    return packet.targetKey;
  return "script-int:" + std::to_string(optionalJsonI64(payload, "symbol_index", 0)) + ":" +
         std::to_string(optionalJsonI64(payload, "value_index", 0));
}

[[nodiscard]] std::optional<std::int64_t> parseI64Segment(std::string_view text) noexcept {
  if(text.empty())
    return std::nullopt;
  std::int64_t value = 0;
  const auto r = std::from_chars(text.data(), text.data() + text.size(), value);
  if(r.ec != std::errc{} || r.ptr != text.data() + text.size())
    return std::nullopt;
  return value;
}

[[nodiscard]] WorldItemIdentity parseWorldItemIdentity(std::string raw) {
  WorldItemIdentity out;
  out.exact = std::move(raw);
  std::string_view text = out.exact;

  const auto pidMarker = text.find(":pid:");
  const auto symMarker = text.find(":sym:");
  if(pidMarker != std::string_view::npos && symMarker != std::string_view::npos && pidMarker < symMarker) {
    if(Mmo::Server::Identity::startsWith(text, Mmo::Server::Identity::WorldItemHookPrefix)) {
      out.world = std::string(text.substr(Mmo::Server::Identity::WorldItemHookPrefix.size(), pidMarker - Mmo::Server::Identity::WorldItemHookPrefix.size()));
    }
    // Malformed keys such as world-item.zen:pid:... appeared in old local
    // binaries. Do not trust the abbreviated world part; payload.world is more
    // canonical and will fill out.world below in the resolver.
    if(auto pid = parseI64Segment(text.substr(pidMarker + 5, symMarker - (pidMarker + 5))))
      out.persistentId = *pid;
    if(auto sym = parseI64Segment(text.substr(symMarker + 5)))
      out.symbol = *sym;
    return out;
  }

  constexpr std::string_view dbPrefix = "world_item:";
  if(Mmo::Server::Identity::startsWith(text, dbPrefix)) {
    std::string_view rest = text.substr(dbPrefix.size());
    const auto first = rest.find(':');
    if(first != std::string_view::npos) {
      const auto second = rest.find(':', first + 1);
      if(second != std::string_view::npos) {
        out.world = std::string(rest.substr(0, first));
        if(auto pid = parseI64Segment(rest.substr(first + 1, second - first - 1)))
          out.persistentId = *pid;
        const auto third = rest.find(':', second + 1);
        const auto symEnd = third == std::string_view::npos ? rest.size() : third;
        if(auto sym = parseI64Segment(rest.substr(second + 1, symEnd - second - 1)))
          out.symbol = *sym;
      }
    }
  }

  return out;
}


struct ResolvedWorldNpcEntity final {
  std::string entityKey;
  std::string lifecycleState;
  std::int64_t rowVersion = 0;
};

struct WorldNpcIdentity final {
  std::string exact;
  std::string world;
  std::int64_t persistentId = -1;
  std::int64_t symbol = -1;
};

void fillWorldNpcIdentityFromPayload(WorldNpcIdentity& identity, std::string_view payload) {
  if(identity.persistentId < 0)
    identity.persistentId = optionalJsonI64(payload, "target_npc_persistent_id",
                            optionalJsonI64(payload, "source_npc_persistent_id",
                            optionalJsonI64(payload, "npc_persistent_id",
                            optionalJsonI64(payload, "persistent_id", -1))));
  if(identity.symbol < 0)
    identity.symbol = optionalJsonI64(payload, "target_npc_symbol",
                      optionalJsonI64(payload, "source_npc_symbol",
                      optionalJsonI64(payload, "npc_symbol",
                      optionalJsonI64(payload, "symbol", -1))));
  if(identity.world.empty())
    identity.world = optionalJsonString(payload, "world");
}

[[nodiscard]] WorldNpcIdentity parseWorldNpcIdentity(std::string raw) {
  WorldNpcIdentity out;
  out.exact = std::move(raw);
  const std::string_view text = out.exact;
  if(!Mmo::Server::Identity::looksLikeNpcKey(text))
    return out;

  const auto pidMarker = text.find(":pid:");
  const auto symMarker = text.find(":sym:");
  if(pidMarker != std::string_view::npos && symMarker != std::string_view::npos && pidMarker < symMarker) {
    if(Mmo::Server::Identity::startsWith(text, Mmo::Server::Identity::NpcHookPrefix))
      out.world = std::string(text.substr(Mmo::Server::Identity::NpcHookPrefix.size(), pidMarker - Mmo::Server::Identity::NpcHookPrefix.size()));
    else if(Mmo::Server::Identity::startsWith(text, Mmo::Server::Identity::CreatureHookPrefix))
      out.world = std::string(text.substr(Mmo::Server::Identity::CreatureHookPrefix.size(), pidMarker - Mmo::Server::Identity::CreatureHookPrefix.size()));
    // Old malformed packets can look like npc.zen:pid:...; do not trust that
    // abbreviated world segment because payload.world is the authoritative
    // world instance name used by the server session.
    if(auto pid = parseI64Segment(text.substr(pidMarker + 5, symMarker - (pidMarker + 5))))
      out.persistentId = *pid;
    if(auto sym = parseI64Segment(text.substr(symMarker + 5)))
      out.symbol = *sym;
    return out;
  }

  // Older actor key emitted by lightweight hooks: npc:<persistent_id>:sym:<symbol>.
  if(Mmo::Server::Identity::startsWith(text, Mmo::Server::Identity::NpcHookPrefix) && symMarker != std::string_view::npos && symMarker > 4) {
    if(auto pid = parseI64Segment(text.substr(4, symMarker - 4)))
      out.persistentId = *pid;
    if(auto sym = parseI64Segment(text.substr(symMarker + 5)))
      out.symbol = *sym;
    return out;
  }

  const std::string_view prefix = Mmo::Server::Identity::startsWith(text, Mmo::Server::Identity::CreatureHookPrefix) ?
    Mmo::Server::Identity::CreatureHookPrefix : Mmo::Server::Identity::NpcHookPrefix;
  if(!Mmo::Server::Identity::startsWith(text, prefix))
    return out;

  // Runtime/import key: npc|creature:<world>:<persistent_id>:<symbol>[:script_id].
  std::string_view rest = text.substr(prefix.size());
  const auto first = rest.find(':');
  if(first == std::string_view::npos)
    return out;
  const auto second = rest.find(':', first + 1);
  if(second == std::string_view::npos)
    return out;
  out.world = std::string(rest.substr(0, first));
  if(auto pid = parseI64Segment(rest.substr(first + 1, second - first - 1)))
    out.persistentId = *pid;
  const auto third = rest.find(':', second + 1);
  const auto symbolEnd = third == std::string_view::npos ? rest.size() : third;
  if(auto sym = parseI64Segment(rest.substr(second + 1, symbolEnd - second - 1)))
    out.symbol = *sym;
  return out;
}

