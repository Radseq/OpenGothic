#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace Mmo::Server::Perception {

enum class Channel : std::uint8_t {
  ActiveScan,
  PassiveEvent,
};

enum class Domain : std::uint8_t {
  Social,
  Combat,
  Crime,
  Sound,
  Magic,
  Movement,
  Item,
  Room,
  Command,
};

enum class ServerNeed : std::uint8_t {
  ObserveOnly,
  QueueScriptHandler,
  InterruptAction,
  ChangeHostility,
  StartCombat,
  CrimeReaction,
};

struct PerceptionDef final {
  std::uint8_t id = 0;
  std::string_view constant;
  std::string_view eventName;
  Channel channel = Channel::PassiveEvent;
  Domain domain = Domain::Social;
  ServerNeed need = ServerNeed::ObserveOnly;
  bool needsWitness = false;
  bool needsVictim = false;
  bool canInterruptRoutine = false;
};

inline constexpr std::uint8_t MaxPerceptionId = 32;

inline constexpr std::array<PerceptionDef, MaxPerceptionId> Gothic2Perceptions {{
  {1,  "PERC_ASSESSPLAYER",       "assess_player",        Channel::ActiveScan,   Domain::Social,   ServerNeed::QueueScriptHandler, false, false, true},
  {2,  "PERC_ASSESSENEMY",        "assess_enemy",         Channel::ActiveScan,   Domain::Combat,   ServerNeed::StartCombat,        false, false, true},
  {3,  "PERC_ASSESSFIGHTER",      "assess_fighter",       Channel::ActiveScan,   Domain::Combat,   ServerNeed::QueueScriptHandler, true,  true,  true},
  {4,  "PERC_ASSESSBODY",         "assess_body",          Channel::ActiveScan,   Domain::Crime,    ServerNeed::QueueScriptHandler, false, true,  true},
  {5,  "PERC_ASSESSITEM",         "assess_item",          Channel::ActiveScan,   Domain::Item,     ServerNeed::QueueScriptHandler, false, false, false},
  {6,  "PERC_ASSESSMURDER",       "assess_murder",        Channel::PassiveEvent, Domain::Crime,    ServerNeed::CrimeReaction,      true,  true,  true},
  {7,  "PERC_ASSESSDEFEAT",       "assess_defeat",        Channel::PassiveEvent, Domain::Combat,   ServerNeed::QueueScriptHandler, true,  true,  true},
  {8,  "PERC_ASSESSDAMAGE",       "assess_damage",        Channel::PassiveEvent, Domain::Combat,   ServerNeed::InterruptAction,    false, true,  true},
  {9,  "PERC_ASSESSOTHERSDAMAGE", "assess_others_damage", Channel::PassiveEvent, Domain::Combat,   ServerNeed::CrimeReaction,      true,  true,  true},
  {10, "PERC_ASSESSTHREAT",       "assess_threat",        Channel::PassiveEvent, Domain::Combat,   ServerNeed::InterruptAction,    true,  true,  true},
  {11, "PERC_ASSESSREMOVEWEAPON", "assess_remove_weapon", Channel::PassiveEvent, Domain::Combat,   ServerNeed::InterruptAction,    true,  false, true},
  {12, "PERC_OBSERVEINTRUDER",    "observe_intruder",     Channel::PassiveEvent, Domain::Crime,    ServerNeed::CrimeReaction,      true,  false, true},
  {13, "PERC_ASSESSFIGHTSOUND",   "assess_fight_sound",   Channel::PassiveEvent, Domain::Sound,    ServerNeed::QueueScriptHandler, true,  true,  true},
  {14, "PERC_ASSESSQUIETSOUND",   "assess_quiet_sound",   Channel::PassiveEvent, Domain::Sound,    ServerNeed::QueueScriptHandler, true,  false, true},
  {15, "PERC_ASSESSWARN",         "assess_warn",          Channel::PassiveEvent, Domain::Social,   ServerNeed::InterruptAction,    true,  true,  true},
  {16, "PERC_CATCHTHIEF",         "catch_thief",          Channel::PassiveEvent, Domain::Crime,    ServerNeed::CrimeReaction,      true,  false, true},
  {17, "PERC_ASSESSTHEFT",        "assess_theft",         Channel::PassiveEvent, Domain::Crime,    ServerNeed::CrimeReaction,      true,  false, true},
  {18, "PERC_ASSESSCALL",         "assess_call",          Channel::PassiveEvent, Domain::Social,   ServerNeed::QueueScriptHandler, true,  true,  true},
  {19, "PERC_ASSESSTALK",         "assess_talk",          Channel::PassiveEvent, Domain::Social,   ServerNeed::QueueScriptHandler, false, false, true},
  {20, "PERC_ASSESSGIVENITEM",    "assess_given_item",    Channel::PassiveEvent, Domain::Item,     ServerNeed::QueueScriptHandler, false, false, false},
  {21, "PERC_ASSESSFAKEGUILD",    "assess_fake_guild",    Channel::PassiveEvent, Domain::Crime,    ServerNeed::CrimeReaction,      true,  false, true},
  {22, "PERC_MOVEMOB",            "move_mob",             Channel::PassiveEvent, Domain::Movement, ServerNeed::QueueScriptHandler, true,  false, false},
  {23, "PERC_MOVENPC",            "move_npc",             Channel::PassiveEvent, Domain::Movement, ServerNeed::QueueScriptHandler, true,  true,  false},
  {24, "PERC_DRAWWEAPON",         "draw_weapon",          Channel::PassiveEvent, Domain::Combat,   ServerNeed::InterruptAction,    true,  false, true},
  {25, "PERC_OBSERVESUSPECT",     "observe_suspect",      Channel::PassiveEvent, Domain::Crime,    ServerNeed::CrimeReaction,      true,  false, true},
  {26, "PERC_NPCCOMMAND",         "npc_command",          Channel::PassiveEvent, Domain::Command,  ServerNeed::QueueScriptHandler, false, true,  true},
  {27, "PERC_ASSESSMAGIC",        "assess_magic",         Channel::PassiveEvent, Domain::Magic,    ServerNeed::InterruptAction,    true,  true,  true},
  {28, "PERC_ASSESSSTOPMAGIC",    "assess_stop_magic",    Channel::PassiveEvent, Domain::Magic,    ServerNeed::QueueScriptHandler, true,  true,  true},
  {29, "PERC_ASSESSCASTER",       "assess_caster",        Channel::PassiveEvent, Domain::Magic,    ServerNeed::InterruptAction,    true,  false, true},
  {30, "PERC_ASSESSSURPRISE",     "assess_surprise",      Channel::PassiveEvent, Domain::Social,   ServerNeed::InterruptAction,    true,  false, true},
  {31, "PERC_ASSESSENTERROOM",    "assess_enter_room",    Channel::PassiveEvent, Domain::Room,     ServerNeed::CrimeReaction,      true,  false, true},
  {32, "PERC_ASSESSUSEMOB",       "assess_use_mob",       Channel::PassiveEvent, Domain::Crime,    ServerNeed::CrimeReaction,      true,  false, true},
}};

[[nodiscard]] constexpr std::optional<PerceptionDef> byId(std::uint8_t id) noexcept {
  if(id == 0 || id > MaxPerceptionId)
    return std::nullopt;
  return Gothic2Perceptions[id - 1];
}

[[nodiscard]] constexpr bool isCrime(Domain domain) noexcept {
  return domain == Domain::Crime || domain == Domain::Room;
}

[[nodiscard]] constexpr bool isCombat(Domain domain) noexcept {
  return domain == Domain::Combat || domain == Domain::Magic;
}

[[nodiscard]] constexpr bool requiresServerDecision(ServerNeed need) noexcept {
  return need != ServerNeed::ObserveOnly;
}

} // namespace Mmo::Server::Perception
