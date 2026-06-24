#pragma once

#include <string_view>

class GameSession;

class WorldStateExporter final {
  public:
    static bool exportInitialState(GameSession& game, std::string_view directory);
    static bool exportSaveState   (GameSession& game, std::string_view directory);
  };
