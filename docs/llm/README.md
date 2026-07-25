# Full Client LLM Entry Point

This file is a navigation map for `src/client`. Load only the smallest set that
covers the requested boundary, then verify claims against source, tests and
CMake.

## Start here

1. `AGENTS.md` — ownership, forbidden dependencies and change discipline.
2. `current-state.md` — verified active, integrated and incomplete behavior.
3. `next-work.md` — the nearest ordered client work and acceptance gates.
4. Actual symbols, callers, tests and CMake selected through the repository
   index.

## Focused contracts

- `api-contracts.md` — map of the stable client-facing contracts.
- `intent-submission-contract.md` — input/UI conversion into typed facade
  requests without client-authored outcomes.
- `presentation-mailbox-contract.md` — facade mailbox validation, ordering and
  bounded engine-thread delivery.
- `route-projection-contract.md` — route replacement, atomic bootstrap,
  generation safety and engine projection.
- `authoritative-ui-contract.md` — revisioned inventory, equipment, dialog and
  other server-owned UI state.
- `architecture.md` — end-to-end mode, ownership and data-flow summary.

## State, validation and decisions

- `repo-map.md` — stable ownership seams and composition anchors.
- `testing.md` — validation layers and what each layer proves.
- `domain-glossary.md` — client-specific vocabulary only.
- [`../adr/README.md`](../adr/README.md) — durable client architecture
  decisions.
- `roadmap.md` — compatibility pointer; global ordering belongs to the root
  roadmap.

The short compatibility files in this directory preserve old links. They must
continue to redirect to canonical documents instead of accumulating duplicate
rules or state.
