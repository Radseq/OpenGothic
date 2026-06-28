# MySQL Event Replay Wallet Class Fix

Fix for the remaining Step 17 replay-contract validation failure.

## Problem

`mmo_validate_event_replay_contract(...)` can still report one error after the NPC lifecycle class fix when historical or smoke-test wallet events exist.

Migration `004_wallet_write_path.sql` emits:

```text
character_wallet_delta  event_class = inventory
```

The first Step 17 registry incorrectly registered `character_wallet_delta` as `character`. The projection target remains `character_wallets`; only the event-class contract must match the actual journal row.

## Fix

`017_event_replay_contract.sql` now registers:

```text
character_wallet_delta  event_class = inventory
projection              = character_wallets
```

The checker also prints rows from `v_event_replay_contract_gaps` when replay validation fails, so the next mismatch is visible immediately.

## Apply

Re-run migration 017, then run `tools/check_mysql_steps_15_18_bridge_replay_parity.py` again.
