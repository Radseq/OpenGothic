# MMO server script authority - step 143

## What changed

- Added `server/cpp/mmo_server_script_authority.h/.cpp`.
- `SetScriptInt` now passes through `Mmo::Server::Script::buildSetIntCommand(...)`.
- The script authority module owns script-int command decisions:
  - script key fallback order: `script_key`, `global_key`, `symbol_name`, packet target key;
  - deterministic fallback key: `script-int:<symbol_index>:<value_index>`;
  - symbol/value index and int32 value validation via the shared story validation limits.
- Removed old local `scriptKeyFromPayload(...)` and obsolete quest-status normalization from `mmo_udp_server_story_payloads.inl`.

## Durable decision

Script, quest and dialog command building should stay in domain authority modules, not in UDP/direct-DB glue.

The direct apply layer should parse transport payload fields, call an authority module, then pass the accepted command to persistence. This keeps the current temporary DB bridge replaceable.

## Verification

- `cmake --build build\mmo_cpp_server --target mmo_udp_server --config Debug`
- Result: success.

## Next good steps

- Rename or split `mmo_server_story_authority.h`; it is becoming a shared validation/limits header rather than a true domain module.
- Add unit-level tests for script/quest/dialog command builders once a lightweight C++ test target exists.
- Continue moving payload-specific logic out of `mmo_udp_server_*.inl` files into focused modules.
