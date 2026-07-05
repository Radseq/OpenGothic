# MMO server quest authority - step 141

## What changed

- Added `server/cpp/mmo_server_quest_authority.h/.cpp`.
- Quest updates now pass through `Mmo::Server::Quest::buildUpdateCommand(...)` before hitting the persistence bridge.
- The module owns quest-domain decisions:
  - canonical quest status names: `running`, `success`, `failed`, `obsolete`;
  - old/client status aliases like `run`, `in_progress`, `succeeded`, `failure`, `closed`;
  - `quest:` target-key prefix stripping;
  - default quest name fallback;
  - entry-count validation through the existing story validation limits;
  - optional transition guard against reopening terminal quests unless `allow_terminal_reopen` is true.
- `mmo_udp_server_direct_story_apply.inl` now delegates `UpdateQuest` to the quest authority module.
- `mmo_udp_server_payload_mapper.inl` forwards optional `previous_status` and `allow_terminal_reopen`.

## Durable decision

Quest rules must stay outside SQL/persistence. Dialogs, scripts and future server-side quest logic should emit quest inputs into the quest authority module, then persistence should only store the accepted command.

The current DB bridge is temporary, but quest status normalization and transition policy are gameplay-domain rules and should survive a future database rewrite.

## Verification

- `cmake --build build\mmo_cpp_server --target mmo_udp_server --config Debug`
- Result: success.

## Next good steps

- Add a dialogue authority module that can produce quest updates without knowing DB details.
- Add richer quest objectives/steps when the client capture starts sending objective payloads.
- Later, replace direct DB quest writes with a repository interface without changing `mmo_server_quest_authority`.
