# MMO server dialog authority - step 142

## What changed

- Added `server/cpp/mmo_server_dialog_authority.h/.cpp`.
- `SetKnownDialog` now passes through `Mmo::Server::Dialog::buildKnownDialogCommand(...)` before persistence.
- The dialog authority module owns known-dialog decisions:
  - NPC/info key fallback from explicit keys, symbol names and target key;
  - stripping `npc-symbol:` and `dialog-info:` prefixes for canonical DB-facing keys;
  - `known`, `removed`, `permanent` and `repeatable` policy;
  - availability aliases such as `repeatable`, `consumed`, `visible`, `hidden`;
  - final validation through existing story dialog limits.
- `mmo_udp_server_payload_mapper.inl` now forwards optional dialog fields:
  `npc_symbol_name`, `info_symbol_name`, `availability_state`, `permanent`, `repeatable`.

## Durable decision

Dialog rules must stay outside persistence. Scripts and future dialogue selection/execution logic should produce dialog-domain inputs, then the persistence bridge should only store accepted commands.

This is currently a known-dialog authority layer, not the full dialog runtime. It prepares the boundary for later server-authoritative dialog choices, conditions, rewards and quest emissions.

## Verification

- `cmake --build build\mmo_cpp_server --target mmo_udp_server --config Debug`
- Result: success.

## Next good steps

- Add dialog-choice/selection authority when the semantic stream contains choice execution data.
- Let dialogue authority emit quest authority inputs for quest-start/quest-complete consequences.
- Keep DB-specific known-dialog storage behind persistence so it can be replaced with the future MMO database.
