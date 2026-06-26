#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "game/gamescript.h"

class GameSession;
class Npc;

class MmoRuntimeSqlite final {
  public:
    MmoRuntimeSqlite(std::string path, uint64_t intervalMs, bool restoreState,
                     bool captureBaseline, std::string saveSlotPath);
    ~MmoRuntimeSqlite();

    MmoRuntimeSqlite(const MmoRuntimeSqlite&) = delete;
    MmoRuntimeSqlite& operator=(const MmoRuntimeSqlite&) = delete;

    bool open(GameSession& game);
    void tick(GameSession& game, uint64_t dt);
    void flush(GameSession& game);
    void recordSaveSlot(GameSession& game, std::string_view slotPath, std::string_view displayName);
    void recordDialogChoices(GameSession& game, Npc& player, Npc& npc,
                             const std::vector<GameScript::DlgChoice>& choices,
                             std::string_view phase, bool includeImportant);
    void recordDialogSelection(GameSession& game, Npc& player, Npc& npc,
                               const GameScript::DlgChoice& choice,
                               std::string_view phase);

  private:
    void flush(GameSession& game, bool materializeCurrent);

    struct Impl;
    std::unique_ptr<Impl> impl;
  };
