# MMO server combat intent correlation - step 163

## What changed

Added a runtime combat intent correlation registry:

- `FightIntent::Registry`

It tracks the last `proposed` combat intent per actor and correlates later
`accepted` events.

## Behavior

For `RecordCombatIntent`:

- `proposed` stores pending actor/action/target,
- `accepted` must match the pending proposal,
- `rejected` clears the pending proposal,
- old proposals expire after `ProposedIntentTimeoutMs`.

Diagnostic logs:

- `[combat_intent_correlation_mismatch]`
- `[combat_intent_proposal_expired]`

Examples of mismatches:

- `accepted_without_proposed`
- `accepted_action_mismatch`
- `accepted_target_mismatch`

## Integration

`mmo_udp_server.cpp` owns:

```cpp
Mmo::Server::FightIntent::Registry gFightIntentRegistry;
```

`mmo_udp_server_direct_combat_apply.inl` applies correlation before running the
existing soft `FightIntent::evaluate(...)` validation.

## Why

This is a small runtime version of the eventual ACK/NACK state machine.

It gives the server enough memory to reason about the sequence:

```text
proposed -> accepted
```

instead of treating each combat intent event as isolated.

## Boundary

Still runtime-only and soft-authoritative:

- no DB writes,
- no hard NACK,
- no client rollback,
- no server-owned animation start yet.

This should remain in memory until the final replication/ACK path is designed.

## Verified

Built successfully:

```text
cmake --build build\mmo_cpp_server --target mmo_udp_server --config Debug
```

## Next good step

Use this registry to create explicit ACK/NACK packets or correction outbox
records once the client has rollback hooks for combat animation prediction.
