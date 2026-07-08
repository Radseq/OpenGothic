# OpenGothic LLM Context

Compact context for Codex/LLM work on the OpenGothic MMO branch.

Goal: give the agent enough project memory to act safely without loading the
whole repo or the historical `docs/llm/ai/` archive.

## Minimal Read

Start with:

1. `current-state.md` - current goal, completed work and active gap.
2. `next-work.md` - the recommended next implementation path.
3. `repo-map.md` - only the section that matches the touched area.

Then inspect source files before editing.

## Conditional Reads

- `api-contracts.md` - flags, protocols, DB/read-model contracts.
- `testing.md` - build/probe commands and current expected results.
- `architecture.md` - authority boundary changes.
- `decisions.md` - durable MMO decisions.
- `coding-style.md` - C++/SQL/Python implementation rules.
- `domain-glossary.md` - terminology.

## Archive Rule

`ai/*.md` is history. Do not load the archive by default.

Open a specific archive entry only when:

- the user mentions that step;
- a regression needs exact historical command/output;
- a compact context file points to that step.

Current archive range:

- roadmap: `ai/114-mmo-authoritative-content-roadmap-expanded.md`
- active content/AI steps: `ai/133` through `ai/151`

## Ground Rules

- Code is source of truth when docs and implementation disagree.
- Native single-player behavior must remain unchanged unless explicit MMO flags
  select server-bound behavior.
- Keep compact docs small. Store long command logs outside default LLM context.
