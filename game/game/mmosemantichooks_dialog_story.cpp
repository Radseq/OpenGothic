#include "mmosemantichooks_internal.h"

#include <array>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "mmoclientadapter.h"
#include "mmoclientbridge.h"
#include "world/world.h"
#include "world/objects/interactive.h"
#include "world/objects/item.h"
#include "world/objects/npc.h"
#include "world/waypoint.h"
#include "commandline.h"
#include "utils/versioninfo.h"

namespace Mmo::Hooks::Detail {

void onNpcDialogLineQueued(Npc& speaker,
                           Npc& listener,
                           std::string_view outputName,
                           const char* sourceLocation) noexcept {
  if(!isClientMmoDiagnosticsEnabled() || !shouldCaptureWorldAiAction(speaker, &listener) || outputName.empty())
    return;

  auto& world = speaker.world();
  const auto speakerKey = npcTargetKey(&speaker);
  const auto listenerKey = npcTargetKey(&listener);
  std::string conversationKey = speakerKey;
  conversationKey.append(":dialog:");
  conversationKey.append(listenerKey);

  const auto subtitle = world.script().messageByName(outputName);
  const auto duration = world.script().messageTime(outputName);

  std::string payload;
  payload.reserve(1024 + subtitle.size());
  payload.append("{\"source\":"); appendEscaped(payload, sourceLocation);
  payload.append(",\"reason\":\"npc_dialog_line_queued\"");
  payload.append(",\"conversation_key\":"); appendEscaped(payload, conversationKey);
  payload.append(",\"sync_group\":"); appendEscaped(payload, conversationKey);
  payload.append(",\"speaker_key\":"); appendEscaped(payload, speakerKey);
  payload.append(",\"listener_key\":"); appendEscaped(payload, listenerKey);
  payload.append(",\"actor_key\":"); appendEscaped(payload, speakerKey);
  payload.append(",\"target_key\":"); appendEscaped(payload, listenerKey);
  payload.append(",\"output_name\":"); appendEscaped(payload, outputName);
  payload.append(",\"message_name\":"); appendEscaped(payload, outputName);
  payload.append(",\"subtitle_text\":"); appendEscaped(payload, subtitle);
  payload.append(",\"line_duration_ms\":"); appendUInt(payload, duration);
  appendWorld(payload, world);
  appendVec3(payload, "speaker_position", speaker.position());
  appendVec3(payload, "listener_position", listener.position());
  payload.push_back('}');

  submit(SemanticActionKind::RecordNpcDialogLine, std::move(conversationKey), std::move(payload), world.tickCount());
}

void onScriptIntChanged(Npc& actor,
                        std::uint32_t scriptFunctionSymbol,
                        std::string_view scriptFunctionName,
                        std::size_t symbolIndex,
                        std::uint16_t valueIndex,
                        std::string_view symbolName,
                        std::int32_t valueBefore,
                        std::int32_t valueAfter,
                        const char* sourceLocation) noexcept {
  if(!isClientMmoDiagnosticsEnabled() || !shouldCapturePlayerAction(actor) || valueBefore == valueAfter)
    return;
  auto& world = actor.world();
  auto target = scriptKey(symbolIndex, valueIndex);

  std::string payload;
  payload.reserve(640);
  payload.append("{\"source\":"); appendEscaped(payload, sourceLocation);
  payload.append(",\"actor_key\":"); appendEscaped(payload, actorKey(actor));
  appendScriptContext(payload, scriptFunctionSymbol, scriptFunctionName);
  payload.append(",\"script_key\":"); appendEscaped(payload, target);
  payload.append(",\"global_key\":"); appendEscaped(payload, target);
  payload.append(",\"symbol_name\":"); appendEscaped(payload, symbolName);
  payload.append(",\"symbol_index\":"); appendUInt(payload, symbolIndex);
  payload.append(",\"value_index\":"); appendUInt(payload, valueIndex);
  payload.append(",\"value_before\":"); appendInt(payload, static_cast<std::int64_t>(valueBefore));
  payload.append(",\"value_after\":"); appendInt(payload, static_cast<std::int64_t>(valueAfter));
  payload.append(",\"reason\":\"script_int_changed\"");
  appendWorld(payload, world);
  appendVec3(payload, "actor_position", actor.position());
  payload.push_back('}');

  submit(SemanticActionKind::SetScriptInt, std::move(target), std::move(payload), world.tickCount());
}

void onKnownDialogChanged(Npc& actor,
                          std::uint32_t scriptFunctionSymbol,
                          std::string_view scriptFunctionName,
                          std::size_t npcSymbol,
                          std::string_view npcSymbolName,
                          std::size_t infoSymbol,
                          std::string_view infoSymbolName,
                          bool known,
                          const char* sourceLocation) noexcept {
  if(!isClientMmoDiagnosticsEnabled() || !shouldCapturePlayerAction(actor))
    return;
  auto& world = actor.world();
  auto target = symbolKey("dialog-info", infoSymbol);
  auto npcKey = symbolKey("npc-symbol", npcSymbol);

  std::string payload;
  payload.reserve(640);
  payload.append("{\"source\":"); appendEscaped(payload, sourceLocation);
  payload.append(",\"actor_key\":"); appendEscaped(payload, actorKey(actor));
  appendScriptContext(payload, scriptFunctionSymbol, scriptFunctionName);
  payload.append(",\"npc_key\":"); appendEscaped(payload, npcKey);
  payload.append(",\"npc_symbol_name\":"); appendEscaped(payload, npcSymbolName);
  payload.append(",\"npc_symbol\":"); appendUInt(payload, npcSymbol);
  payload.append(",\"info_key\":"); appendEscaped(payload, target);
  payload.append(",\"info_symbol_name\":"); appendEscaped(payload, infoSymbolName);
  payload.append(",\"info_symbol\":"); appendUInt(payload, infoSymbol);
  payload.append(",\"known\":"); payload.append(known ? "true" : "false");
  payload.append(",\"removed\":false");
  payload.append(",\"reason\":\"script_dialog_known\"");
  appendWorld(payload, world);
  payload.push_back('}');

  submit(SemanticActionKind::SetKnownDialog, std::move(target), std::move(payload), world.tickCount());
}

void onQuestChanged(Npc& actor,
                    std::uint32_t scriptFunctionSymbol,
                    std::string_view scriptFunctionName,
                    std::string_view questKey,
                    std::string_view status,
                    std::size_t entryCount,
                    const char* sourceLocation) noexcept {
  if(!isClientMmoDiagnosticsEnabled() || !shouldCapturePlayerAction(actor) || questKey.empty())
    return;
  auto& world = actor.world();
  std::string target = "quest:";
  target.append(questKey);

  std::string payload;
  payload.reserve(640);
  payload.append("{\"source\":"); appendEscaped(payload, sourceLocation);
  payload.append(",\"actor_key\":"); appendEscaped(payload, actorKey(actor));
  appendScriptContext(payload, scriptFunctionSymbol, scriptFunctionName);
  payload.append(",\"quest_key\":"); appendEscaped(payload, questKey);
  payload.append(",\"quest_name\":"); appendEscaped(payload, questKey);
  payload.append(",\"status\":"); appendEscaped(payload, status);
  payload.append(",\"entry_count\":"); appendUInt(payload, entryCount);
  payload.append(",\"entries\":[]");
  payload.append(",\"reason\":\"script_quest_changed\"");
  appendWorld(payload, world);
  payload.push_back('}');

  submit(SemanticActionKind::UpdateQuest, std::move(target), std::move(payload), world.tickCount());
}

} // namespace Mmo::Hooks::Detail
