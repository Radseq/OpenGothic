# Full Client LLM Entry Point

Use the smallest reading set that can answer the task.

## Default path

1. `AGENTS.md` — ownership and non-negotiable invariants.
2. `current-state.md` — verified implementation state and known gaps.
3. `next-work.md` — ordered unfinished client work.
4. Actual source, CMake and tests retrieved through the repository index.

## Read only when needed

- `architecture.md` — runtime data flow and state ownership.
- `api-contracts.md` — engine-facing contract summary.
- `testing.md` — what each validation layer proves.
- `repo-map.md` — stable composition anchors, not a source inventory.
- [`../adr/README.md`](../adr/README.md) — durable decisions and their
  consequences.
- `roadmap.md` — compatibility pointer for client-local history; global ordering
  is owned by the root roadmap.

The remaining files in this directory are short compatibility or vocabulary
entries. They must point to canonical context rather than duplicate it.
