# MMO server conversation authority - step 147

## Clarification

The target MMO architecture is server-authoritative:

- the server decides that two NPCs are in a conversation;
- the server decides which line is currently active;
- clients only replay the accepted line/event;
- a player entering range mid-conversation must receive the current conversation snapshot with elapsed/remaining time, not start the conversation from the beginning.

The previous client hook is only a compatibility/capture bridge for current OpenGothic single-player behavior.

## What changed

- Added `server/cpp/mmo_server_conversation_authority.h/.cpp`.
- Added `Mmo::Server::Conversation::Registry`.
- The registry stores active conversations keyed by `conversation_key`.
- Each active line stores:
  - speaker;
  - listener;
  - `output_name`;
  - subtitle text;
  - server start tick in ms;
  - duration in ms.
- `snapshot(conversation_key, now)` and `activeSnapshots(now)` return elapsed/remaining line time.
- `expire(now)` removes conversations after a short grace period.
- `RecordNpcDialogLine` now updates the server-side conversation registry before falling through to outbox persistence.

## Durable decision

Dialog replay must be driven by server conversation snapshots. A newly arriving observer should receive the current active line plus elapsed/remaining time and should not restart the dialogue script locally.

## Verification

- `cmake --build build\mmo_cpp_server --target mmo_udp_server --config Debug`
- Result: success.

## Next good steps

- Add range-based broadcast/replay packets for `Conversation::Snapshot`.
- Add client receiver that can play a remote dialog line from `output_name` starting at an offset.
- Later, move `Conversation::Registry` into a world-state service instead of the temporary UDP server global.
