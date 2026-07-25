# Full Client Agent Contract

Scope: `src/client`.

The full client owns OpenGothic presentation: input, menus, UI, audio,
animation, rendering and engine-object lifetime. In server-bound mode it is an
intent producer and authoritative-state projector, never a gameplay authority.

## Required context

Read `README.md`, `current-state.md` and `next-work.md`. Select the focused
contract through `api-contracts.md` and read the governing ADR before changing
a durable boundary. Retrieve implementation, production composition, direct
callers and tests through the root repository-index workflow.

## Ownership rules

- Server gameplay truth belongs to `src/server`.
- Binary contracts and authority-neutral identifiers belong to `src/shared`.
- Endpoint parsing, ASIO, codecs, retries, ACKs, reconnect and bootstrap
  assembly belong to `src/client_sandbox`.
- The full client consumes only the public sandbox facade and projects its
  domain output into OpenGothic.
- Native single-player remains functional and must not depend on MMO assets,
  transport or server availability.

## MMO invariants

- Submit intent, never HP, inventory, quest, price, combat-result or world-state
  claims.
- Preserve exact route, world, entity generation, item-stack identity and
  aggregate revision. Missing identity is rejection, not a lookup heuristic.
- Apply route replacement before bootstrap and bootstrap before live events.
- Clear route-scoped bindings, interpolation and pending presentation state on
  replacement.
- Prediction is visual and reversible; receipts, deltas and corrections win.
- No JSON, filesystem or SQLite path may become a production gameplay channel.
- No silent fallback to native gameplay after server-bound mode is selected.

## Change discipline

- Distinguish implemented, focused-tested, composition-integrated and
  production-active behavior.
- Read every edited file in full; retrieve dependencies and tests by symbol.
- Keep engine-facing DTOs protocol-independent and lifetime ownership explicit.
- Avoid per-frame allocation, unbounded queues, duplicate state stores and
  locks around rendering or materialization work.
- Preserve strict warnings and fail closed at every external boundary.
- Update `current-state.md` only for verified facts and `next-work.md` only for
  the smallest remaining client edge.
