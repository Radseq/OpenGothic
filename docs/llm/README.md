# OpenGothic LLM Context

Compact context for Codex/LLM work on the OpenGothic MMO branch.

## Minimal Read

Start with:

1. `current-state.md`
2. `next-work.md`
3. `repo-map.md`

Then inspect source before editing.

## Conditional Reads

- `api-contracts.md` - flags, protocols, DB/read-model contracts.
- `testing.md` - focused build/probe commands.
- `architecture.md`, `decisions.md`, `coding-style.md`, `domain-glossary.md`
  - stable design and implementation rules.

## Archive Rule

`ai/*.md` is history. Open a specific entry only when the user mentions a step,
a regression needs archaeology, or a compact context file points to it.

Current archive range:

- roadmap: `ai/114-mmo-authoritative-content-roadmap-expanded.md`
- active content/AI steps: `ai/133` through `ai/181`

## Ground Rules

- Code is source of truth when docs and implementation disagree.
- Native single-player behavior must remain unchanged unless explicit MMO flags
  select server-bound behavior.
- Keep compact docs small; do not paste long command logs here.
- `docs/llm/llm_db_changes/` is a historical planned-only ledger for the
  paused DB period. Executable DB work resumes in `server/sql/step273...`.
