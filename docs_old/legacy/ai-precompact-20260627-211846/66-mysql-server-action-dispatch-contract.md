# MySQL Server Action Dispatch Contract

Migration `019_server_action_dispatch_contract.sql` makes the outbox executable by a thin worker/RPC layer.

It adds:

- `mmo_server_action_dispatch_contracts`;
- `mmo_claim_next_server_action(...)`;
- `mmo_requeue_stale_claimed_actions(...)`;
- `mmo_validate_server_action_dispatch_contracts(...)`;
- `v_claimable_server_actions` and `v_server_action_dispatch_gaps`.

The action registry maps stable action kinds to stored procedures, event types, event classes and projection names. This keeps C++ semantic action names, MySQL write-path procedures and replay contracts aligned.

Important class fixes are preserved:

- `wallet_delta` / `grant_gold` / `spend_gold` use `event_class='inventory'` because migration 004 emits wallet deltas that way.
- `mark_npc_dead` and `respawn_npc` use `event_class='combat'` because migration 009 emits NPC lifecycle rows that way.
