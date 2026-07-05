# MMO server NPC actions and dialog sync foundation - step 146

## Client finding

- Dialog text/audio is currently local-client driven.
- `GameScript::exec(...)` runs the selected dialog script function.
- Scripted `AI_Output(...)` queues an `AiQueue::aiOutput(...)`.
- `Npc::AI_Output` calls the active `AiOuputPipe`.
- `DialogMenu::aiOutput(...)` resolves:
  - subtitle text through `Gothic::inst().messageByName(output_name)`;
  - duration through `Gothic::inst().messageTime(output_name)`;
  - sound through `<output_name>.wav`.

This means MMO dialog synchronization cannot rely on each client independently executing scripts and choosing output lines. The server needs one accepted dialog/conversation event, then all observing clients should replay the same `output_name` and timing.

## What changed

- Added `server/cpp/mmo_server_npc_action_authority.h/.cpp`.
- Added semantic action kinds:
  - `record_npc_action_state`;
  - `record_npc_dialog_line`.
- `record_npc_action_state` captures general durable NPC actions such as:
  - `talking`;
  - `sleeping`;
  - `using_mob`;
  - `moving`;
  - `combat`;
  - `routine`;
  - `idle`.
- `record_npc_dialog_line` captures a deterministic dialogue line contract:
  - `conversation_key`;
  - `speaker_key`;
  - `listener_key`;
  - `output_name`;
  - optional `subtitle_text`;
  - `line_duration_ms`.
- Added client hook `Mmo::Hooks::onNpcDialogLineQueued(...)`.
- `GameScript::ai_output(...)` now emits `RecordNpcDialogLine` before queuing local `AI_Output`.
- `onObservedNpcAuthorityState(...)` now emits `RecordNpcActionState` alongside routine/AI/path/fight observations.
- Direct server apply validates these new actions through `NpcActionAuthority`; accepted events currently fall through to outbox fallback because the DB schema is temporary.

## Durable decision

For MMO, dialogue must be synchronized by a server-accepted output/event stream. The stable replay key is `output_name`, because current OpenGothic resolves both subtitle and WAV from that name.

Later clients should not independently choose dialog lines for world-visible conversations. They should receive the accepted `record_npc_dialog_line`/broadcast event and replay exactly that line.

## Verification

- `cmake --build build --target Gothic2Notr --config Debug`
- `cmake --build build\mmo_cpp_server --target mmo_udp_server --config Debug`
- Result: both success.

## Next good steps

- Add a server-side conversation/session state module to serialize dialog lines per `conversation_key`.
- Add a client receiver/replay path for remote NPC dialog lines.
- Add persistence/broadcast projection for accepted NPC action/dialog events after the DB bridge is replaced or isolated behind a repository.
