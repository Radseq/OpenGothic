# Context Refresh Procedure

Use this when docs become stale or a new agent must prepare a compact context
snapshot.

## Goal

Keep `docs/llm` small, durable and useful for agents. Do not turn it into a log
archive.

## Refresh Order

1. Inspect the latest source files touched by the current task.
2. Inspect the latest relevant tools and SQL checks.
3. Run or read the smallest validation output available.
4. Update `current-state.md` with stable verified facts.
5. Update `next-work.md` with the next concrete edge.
6. Update `api-contracts.md`, `architecture.md` or `decisions.md` only when a durable contract changed.
7. Update `testing.md` only when commands or expected evidence changed.

## What To Include

- exact stable flags/commands;
- exact schema/artifact names;
- exact current counts when they are used as validation evidence;
- known expected warnings;
- current implementation gaps;
- next acceptance checks.

## What To Exclude

- long terminal logs;
- temporary failed experiments;
- generated snapshots;
- private credentials;
- local-only paths unless clearly marked as examples;
- speculative architecture not accepted by code/tests.

## Snapshot Tooling Expectations

Context snapshot tools should generate separate outputs when possible:

- server code snapshot;
- client code snapshot;
- `docs/llm` snapshot;
- tools snapshot;
- MySQL schema-only snapshot.

They should skip:

- `build/`, `.git/`, binaries and object files;
- images/assets unless explicitly relevant;
- generated `wynik*.txt` snapshots;
- live DB data.
