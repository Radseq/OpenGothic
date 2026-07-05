// Internal implementation partition for mmo_udp_server.cpp.
// Owns process setup and the UDP receive/apply/ACK loop.

[[nodiscard]] std::pair<std::string, std::string> parseBind(std::string_view value) {
  const auto colon = value.rfind(':');
  if(colon == std::string_view::npos || colon == 0 || colon + 1 >= value.size())
    throw std::runtime_error("expected --bind host:port");
  return {std::string(value.substr(0, colon)), std::string(value.substr(colon + 1))};
}

Options parseArgs(int argc, char** argv) {
  Options opt;
  auto need = [&](int& i, std::string_view flag) -> std::string {
    if(i + 1 >= argc)
      throw std::runtime_error(std::string(flag) + " requires value");
    return argv[++i];
  };

  for(int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if(arg == "--bind") opt.bind = need(i, arg);
    else if(arg == "--mysql-url" || arg == "--url") opt.mysqlUrl = need(i, arg);
    else if(arg == "--account-name") opt.accountName = need(i, arg);
    else if(arg == "--character-key") opt.characterKey = need(i, arg);
    else if(arg == "--character-name" || arg == "--character-display-name") opt.characterDisplayName = need(i, arg);
    else if(arg == "--session-key") opt.sessionKey = need(i, arg);
    else if(arg == "--db-session-uuid") opt.dbSessionUuid = need(i, arg);
    else if(arg == "--outbox-priority") opt.outboxPriority = parseInt(need(i, arg)).value_or(opt.outboxPriority);
    else if(arg == "--outbox-max-attempts") opt.outboxMaxAttempts = parseInt(need(i, arg)).value_or(opt.outboxMaxAttempts);
    else if(arg == "--max-packets") opt.maxPackets = parseInt(need(i, arg)).value_or(0);
    else if(arg == "--direct-db") opt.directDb = true;
    else if(arg == "--no-direct-db") opt.directDb = false;
    else if(arg == "--enqueue-outbox") opt.enqueueOutbox = true;
    else if(arg == "--no-enqueue-outbox") opt.enqueueOutbox = false;
    else if(arg == "--forward-bootstrap-outbox") opt.forwardBootstrapOutbox = true;
    else if(arg == "--require-db-save-checkpoint-restore") opt.requireDbSaveCheckpointRestore = true;
    else if(arg == "--no-require-db-save-checkpoint-restore") opt.requireDbSaveCheckpointRestore = false;
    else if(arg == "--help" || arg == "-h") {
      std::cout << "Usage: mmo_udp_server --bind 127.0.0.1:29777 --mysql-url mysql://user:pass@host:3306/db [--session-key local-dev-PC_HERO_TEST] [--enqueue-outbox] [--no-direct-db] [--require-db-save-checkpoint-restore]\n";
      std::exit(0);
    } else {
      throw std::runtime_error("unknown argument: " + std::string(arg));
    }
  }
  return opt;
}

} // namespace

int main(int argc, char** argv) {
  try {
    const Options opt = parseArgs(argc, argv);
    Options activeOpt = opt;
    std::optional<MySqlTarget> mysql;
    std::string sessionUuid = opt.dbSessionUuid;
    if(opt.directDb || opt.enqueueOutbox) {
      if(opt.mysqlUrl.empty())
        throw std::runtime_error("--mysql-url is required when direct DB or outbox mode is enabled");
      mysql = parseMysqlUrl(opt.mysqlUrl);
      if(sessionUuid.empty())
        sessionUuid = dbLogin(*mysql, activeOpt);
      else
        (void)ensureActiveDbSession(*mysql, activeOpt, sessionUuid, "startup");
      std::cout << "db_session=" << sessionUuid
                << " direct_db=" << (opt.directDb ? "on" : "off")
                << " enqueue_outbox=" << (opt.enqueueOutbox ? "on" : "off")
                << " require_db_save_checkpoint_restore=" << (opt.requireDbSaveCheckpointRestore ? "on" : "off")
                << "\n";
    }

    const auto [bindHost, bindPort] = parseBind(opt.bind);
    asio::io_context io;
    asio::ip::udp::resolver resolver(io);
    asio::error_code ec;
    auto results = resolver.resolve(asio::ip::udp::v4(), bindHost, bindPort, ec);
    if(ec || results.empty())
      throw std::runtime_error("bind resolve failed: " + ec.message());

    asio::ip::udp::socket socket(io, results.begin()->endpoint());
    socket.non_blocking(true);
    std::signal(SIGINT, stopHandler);
    std::signal(SIGTERM, stopHandler);

    std::unordered_set<std::string> seen;
    std::array<char, Mmo::Net::MaxDatagramBytes> buffer {};
    std::uint64_t received = 0;
    std::uint64_t accepted = 0;
    std::uint64_t invalid = 0;
    std::uint64_t duplicate = 0;
    std::uint64_t enqueued = 0;
    std::uint64_t directDb = 0;
    std::uint64_t unhandled = 0;
    std::uint64_t failed = 0;
    std::uint32_t nextSnapshotId = 1;
    ServerPacketLogState logState;
    LiveWorldSnapshotState liveWorldSnapshotState;

    std::cout << "listening udp://" << opt.bind << " binary_protocol=v1\n";
    while(gRunning.load(std::memory_order_relaxed)) {
      if(opt.maxPackets > 0 && static_cast<int>(received) >= opt.maxPackets)
        break;

      asio::ip::udp::endpoint remote;
      const auto n = socket.receive_from(asio::buffer(buffer), remote, 0, ec);
      if(ec) {
        if(ec == asio::error::would_block || ec == asio::error::try_again) {
          std::this_thread::sleep_for(std::chrono::milliseconds(20));
          continue;
        }
        if(ec == asio::error::connection_reset) {
          std::cout << "[udp_receive_ignored] error=connection_reset message=" << ec.message() << "\n";
          continue;
        }
        throw std::runtime_error("receive_from failed: " + ec.message());
      }
      ++received;

      const auto decoded = Mmo::Net::decodeClientActionPacket(std::string_view(buffer.data(), n));
      if(!decoded.ok()) {
        ++invalid;
        const auto bytes = std::string_view(buffer.data(), n);
        std::cout << "[invalid] remote=" << remote << " error=" << Mmo::Net::decodeErrorName(decoded.error);
        if(decoded.error == Mmo::Net::DecodeError::BadActionKind) {
          if(const auto raw = rawClientActionKind(bytes))
            std::cout << " raw_action_kind=" << *raw << " known_actions=" << Mmo::SemanticActionDefs.size();
        }
        std::cout << " datagram_bytes=" << n << "\n";
        continue;
      }

      const auto& packet = decoded.clientAction;
      const auto* def = Mmo::findSemanticAction(packet.kind);
      const bool isBootstrap = packet.kind == Mmo::SemanticActionKind::ClientBootstrapRequest;
      const bool isMovement = packet.kind == Mmo::SemanticActionKind::MovementProposal ||
                              packet.kind == Mmo::SemanticActionKind::CharacterCheckpoint;
      const bool isWeaponState = packet.kind == Mmo::SemanticActionKind::ReadyWeapon ||
                                 packet.kind == Mmo::SemanticActionKind::HolsterWeapon;
      if(!seen.insert(packet.idempotencyKey).second) {
        if(isBootstrap) {
          seen.clear();
          seen.insert(packet.idempotencyKey);
          std::cout << "[bootstrap_restarts_dedupe] session=" << packet.sessionKey << "\n";
        } else {
          ++duplicate;
          continue;
        }
      }

      bool packetAccepted = true;
      bool packetReady = false;
      std::string bootstrapSnapshotJson;
      std::string liveWorldSnapshotJson;
      std::string diagnosticReason;
      std::string diagnosticMessage;
      std::uint16_t diagnosticSeverity = 0;
      const std::string_view actionName = def ? def->actionKind : std::string_view("unknown");
      if(isBootstrap) {
        BootstrapReadiness readiness;
        const std::string characterKey = jsonStringField(packet.payloadJson, "character_key").value_or(activeOpt.characterKey);
        const std::string displayName = jsonStringField(packet.payloadJson, "display_name").value_or(characterKey);
        std::string worldName = jsonStringField(packet.payloadJson, "world").value_or("UNKNOWN");
        if(mysql) {
          try {
            if(characterKey != activeOpt.characterKey) {
              activeOpt.characterKey = characterKey;
              activeOpt.characterDisplayName = displayName.empty() ? characterKey : displayName;
              sessionUuid = dbLogin(*mysql, activeOpt);
              seen.clear();
              seen.insert(packet.idempotencyKey);
              std::cout << "[db_session_character_selected]"
                        << " character=" << activeOpt.characterKey
                        << " session=" << sessionUuid << "\n";
            } else if(ensureActiveDbSession(*mysql, activeOpt, sessionUuid, "bootstrap")) {
              seen.clear();
              seen.insert(packet.idempotencyKey);
            }
            readiness = readBootstrapReadinessWithFallback(*mysql, characterKey, worldName, sessionUuid, worldName);
            packetReady = readiness.ready;
            printBootstrapAck(packet, characterKey, worldName, readiness, true);
            if(packetReady) {
              try {
                bootstrapSnapshotJson = buildBootstrapSnapshotJson(*mysql, sessionUuid, characterKey, worldName, readiness, true, opt.requireDbSaveCheckpointRestore);
              } catch(const std::exception& exc) {
                diagnosticSeverity = 2;
                diagnosticReason = opt.requireDbSaveCheckpointRestore ? "db_save_checkpoint_restore_required" : "bootstrap_snapshot_build_failed";
                diagnosticMessage = exc.what();
                if(opt.requireDbSaveCheckpointRestore) {
                  packetAccepted = false;
                  packetReady = false;
                  ++failed;
                }
                std::cerr << "[bootstrap_snapshot_build_failed] error=" << exc.what()
                          << " strict_db_save_checkpoint_restore=" << (opt.requireDbSaveCheckpointRestore ? 1 : 0) << "\n";
              }
            }
          } catch(const std::exception& exc) {
            packetAccepted = false;
            ++failed;
            diagnosticSeverity = 2;
            diagnosticReason = "bootstrap_failed";
            diagnosticMessage = exc.what();
            std::cerr << "[bootstrap_failed] error=" << exc.what() << "\n";
          }
        } else {
          readiness.ready = true;
          packetReady = true;
          printBootstrapAck(packet, characterKey, worldName, readiness, false);
        }
      }

      const std::string remoteText = remote.address().to_string() + ":" + std::to_string(remote.port());
      const auto dbPayload = mysql ? makeDbPayload(packet, remoteText) : std::string();
      DirectApplyResult direct;
      if(mysql && opt.directDb && !isBootstrap) {
        try {
          if(!isActiveDbSession(*mysql, sessionUuid)) {
            (void)ensureActiveDbSession(*mysql, activeOpt, sessionUuid, "direct_db");
            seen.clear();
            seen.insert(packet.idempotencyKey);
          }
          direct = applyDirectDb(Mmo::Server::DirectApplyRequest {
            .target = *mysql,
            .sessionUuid = sessionUuid,
            .packet = packet,
            .dbPayload = dbPayload,
          });
          if(direct.handled) {
            ++directDb;
            packetAccepted = direct.accepted;
            packetReady = packetReady || direct.ready;
            if(!direct.accepted) {
              diagnosticSeverity = 1;
              diagnosticReason = direct.label;
              diagnosticMessage = "direct DB rejected semantic action";
            }
          }
        } catch(const std::exception& exc) {
          direct.handled = true;
          if(isFailOpenNpcObservationAction(packet.kind)) {
            direct.accepted = true;
            packetAccepted = true;
            std::cerr << "[direct_db_observation_failed_accepted] action=" << actionName
                      << " target=" << packet.targetKey
                      << " error=" << exc.what()
                      << " payload=" << packet.payloadJson
                      << "\n";
          } else {
            packetAccepted = false;
            ++failed;
            direct.accepted = false;
            diagnosticSeverity = 2;
            diagnosticReason = "direct_db_failed";
            diagnosticMessage = exc.what();
            std::cerr << "[direct_db_failed] action=" << actionName
                      << " target=" << packet.targetKey
                      << " error=" << exc.what()
                      << " payload=" << packet.payloadJson
                      << "\n";
          }
        }
      }

      if(mysql && opt.directDb && direct.handled && !direct.accepted) {
        try {
          recordClientActionCorrection(*mysql, sessionUuid, packet, actionName, direct.label, dbPayload);
          const std::string characterKey = jsonStringField(packet.payloadJson, "character_key").value_or(activeOpt.characterKey);
          std::string worldName = jsonStringField(packet.payloadJson, "world").value_or("UNKNOWN");
          auto readiness = readBootstrapReadinessWithFallback(*mysql, characterKey, worldName, sessionUuid, worldName);
          if(readiness.ready) {
            liveWorldSnapshotJson = buildBootstrapSnapshotJson(*mysql, sessionUuid, characterKey, worldName, readiness, false, false);
            std::cout << "[client_correction_snapshot_queued]"
                      << " action=" << actionName
                      << " reason=" << direct.label
                      << " local_sequence=" << packet.localSequence
                      << " bytes=" << liveWorldSnapshotJson.size()
                      << "\n";
          }
        } catch(const std::exception& exc) {
          std::cerr << "[client_correction_snapshot_failed] action=" << actionName
                    << " reason=" << direct.label
                    << " error=" << exc.what() << "\n";
        }
      }

      if(mysql && opt.directDb && shouldSendLiveWorldSnapshot(liveWorldSnapshotState, packet, packetAccepted, direct)) {
        try {
          const std::string characterKey = jsonStringField(packet.payloadJson, "character_key").value_or(activeOpt.characterKey);
          std::string worldName = jsonStringField(packet.payloadJson, "world").value_or("UNKNOWN");
          auto readiness = readBootstrapReadinessWithFallback(*mysql, characterKey, worldName, sessionUuid, worldName);
          if(readiness.ready) {
            liveWorldSnapshotJson = buildBootstrapSnapshotJson(*mysql, sessionUuid, characterKey, worldName, readiness, false, false);
            if(const auto pos = movementToPosition(packet.payloadJson)) {
              std::cout << "[live_world_item_snapshot_queued] reason=movement_interest"
                        << " x=" << pos->x
                        << " y=" << pos->y
                        << " z=" << pos->z
                        << " bytes=" << liveWorldSnapshotJson.size()
                        << "\n";
            } else {
              std::cout << "[live_world_item_snapshot_queued] reason=movement_interest bytes="
                        << liveWorldSnapshotJson.size() << "\n";
            }
          }
        } catch(const std::exception& exc) {
          std::cerr << "[live_world_item_snapshot_build_failed] action=" << actionName
                    << " error=" << exc.what() << "\n";
        }
      }

      if(mysql && opt.enqueueOutbox && !direct.handled && (!isBootstrap || opt.forwardBootstrapOutbox)) {
        try {
          enqueueOutbox(*mysql, sessionUuid, packet, dbPayload, opt.outboxPriority, opt.outboxMaxAttempts);
          ++enqueued;
        } catch(const std::exception& exc) {
          packetAccepted = false;
          ++failed;
          diagnosticSeverity = 2;
          diagnosticReason = "enqueue_failed";
          diagnosticMessage = exc.what();
          std::cerr << "[enqueue_failed] action=" << actionName << " error=" << exc.what() << "\n";
        }
      } else if(mysql && opt.directDb && !isBootstrap && !direct.handled && !opt.enqueueOutbox) {
        packetAccepted = false;
        ++unhandled;
        diagnosticSeverity = 2;
        diagnosticReason = "direct_db_unhandled";
        diagnosticMessage = "semantic action has no direct C++ DB handler and outbox fallback is disabled";
        std::cerr << "[direct_db_unhandled] action=" << actionName << "\n";
      }

      ++accepted;
      const auto ackKind = isBootstrap ? Mmo::Net::ServerAckKind::Bootstrap :
                           (isMovement ? Mmo::Net::ServerAckKind::Movement : Mmo::Net::ServerAckKind::GenericAction);
      const auto ack = Mmo::Net::encodeServerAckPacket({packet.packetSequence, packet.localSequence, ackKind, packetAccepted, packetReady});
      socket.send_to(asio::buffer(ack), remote, 0, ec);
      if(!diagnosticReason.empty()) {
        sendServerDiagnostic(socket, remote, packet, diagnosticSeverity, actionName, diagnosticReason, diagnosticMessage);
      }
      bool snapshotSent = false;
      if(isBootstrap && packetAccepted && !bootstrapSnapshotJson.empty()) {
        try {
          sendBootstrapSnapshot(socket, remote, packet, nextSnapshotId++, bootstrapSnapshotJson);
          snapshotSent = true;
        } catch(const std::exception& exc) {
          ++failed;
          std::cerr << "[bootstrap_snapshot_send_failed] error=" << exc.what() << "\n";
          sendServerDiagnostic(socket, remote, packet, 2, actionName, "bootstrap_snapshot_send_failed", exc.what());
        }
      }
      if(!isBootstrap && packetAccepted && !liveWorldSnapshotJson.empty()) {
        try {
          sendBootstrapSnapshot(socket, remote, packet, nextSnapshotId++, liveWorldSnapshotJson);
          snapshotSent = true;
        } catch(const std::exception& exc) {
          ++failed;
          std::cerr << "[live_world_item_snapshot_send_failed] error=" << exc.what() << "\n";
          sendServerDiagnostic(socket, remote, packet, 2, actionName, "live_world_item_snapshot_send_failed", exc.what());
        }
      }
      printPacketProgress(logState, accepted, received, invalid, duplicate, enqueued, directDb, unhandled, failed,
                          actionName, packetAccepted, !diagnosticReason.empty(), snapshotSent, isMovement, isWeaponState);
    }

    std::cout << "summary:\n"
              << "received=" << received << "\n"
              << "accepted=" << accepted << "\n"
              << "invalid=" << invalid << "\n"
              << "duplicate=" << duplicate << "\n"
              << "enqueued=" << enqueued << "\n"
              << "direct_db=" << directDb << "\n"
              << "unhandled=" << unhandled << "\n"
              << "failed=" << failed << "\n";
    return invalid == 0 && failed == 0 && unhandled == 0 ? 0 : 2;
  } catch(const std::exception& exc) {
    std::cerr << "ERROR: " << exc.what() << "\n";
    return 2;
  }
}
