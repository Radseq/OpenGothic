# OpenGothic LLM Context

Compact context for Codex/LLM work on the OpenGothic MMO branch.

Goal: give the agent enough project memory to act safely without loading the
whole repo or the historical `docs/llm/ai/` archive.

## Minimal Read

For most tasks, start with:

1. `current-state.md` - current goal, completed work and active gap.
2. `next-work.md` - the recommended next implementation path.
3. `repo-map.md` - only the section that matches the touched area.

Then inspect the actual source files before editing.

## Conditional Reads

Read these only when relevant:

- `api-contracts.md` - flags, UDP/bootstrap, JSON/read-model, DB or identity
  compatibility.
- `testing.md` - before building, validating or finishing implementation work.
- `architecture.md` - when changing authority boundaries or data flow.
- `decisions.md` - when a change could contradict established MMO direction.
- `coding-style.md` - before larger C++/SQL/Python edits or new modules.
- `domain-glossary.md` - when terminology is unclear.

## Archive Rule

`ai/*.md` is history. Do not load the archive by default.

Open a specific `ai/NNN-*.md` only when:

- the user mentions that step;
- a regression needs exact historical command/output;
- a current context file points to that step for details.

Useful current references:

- latest summarized roadmap: `ai/114-mmo-authoritative-content-roadmap-expanded.md`;
- Step224 read-model export: `ai/133-mmo-content-build-runtime-read-model-export-step224.md`;
- Step225 C++ read-model probe: `ai/134-mmo-cpp-runtime-read-model-probe-step225.md`;
- Step226 C++ read-model indexes: `ai/135-mmo-cpp-runtime-read-model-indexes-step226.md`.
- Step227 world instance content cache: `ai/136-mmo-world-instance-content-cache-step227.md`.
- Step228 server content cache startup integration:
  `ai/137-mmo-server-content-cache-startup-integration-step228.md`.
- Step229 NPC perception policy candidate pass:
  `ai/138-mmo-npc-perception-policy-candidate-step229.md`.
- Step230 AI runtime perception recording probe:
  `ai/139-mmo-ai-runtime-recording-probe-step230.md`.

## Ground Rules

- Code is the source of truth when docs and implementation disagree.
- Keep native single-player behavior unchanged unless explicit MMO flags select
  server-bound behavior.
- Prefer focused source inspection over broad context loading.
- Avoid updating these docs with long logs; store only durable facts, contracts,
  decisions and next steps.
