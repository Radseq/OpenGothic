# AI Agent Rules

Use this file as the operating contract for Codex/LLM agents.

## Work Style

- Start from the smallest set of context files listed in `README.md`.
- Inspect actual source files before editing.
- Keep changes focused on the requested goal.
- Prefer explicit names and typed contracts over hidden magic.
- Do not claim validation unless the command was actually run and the output was inspected.
- If validation cannot be run, state what was changed and which command should be run next.

## Safety Boundaries

- Do not move gameplay authority to the client.
- Do not remove native single-player behavior.
- Do not make MMO mode active without explicit flags.
- Do not convert debug JSON/outbox paths into the primary gameplay path.
- Do not introduce destructive DB resets unless the user explicitly requested that.
- Do not rely on display names, labels, `PC_HERO`, `Ja`, array order or position alone as durable MMO identity.

## Preferred Implementation Shape

- Server-side gameplay should be typed, validated and journaled.
- Runtime server state should use indexed structs/tables/projections, not raw hot-path JSON.
- Content parsing/build belongs to content build tooling and `mmo_content_build`.
- Runtime simulation belongs to the C++ server and runtime DB schemas.
- JSON is acceptable for diagnostics, bootstrap materialization and generated read-model artifacts.

## Before Editing

Ask these questions internally:

1. Is this single-player behavior or MMO mode behavior?
2. Which side owns the truth: client, C++ server, MySQL runtime DB, or content build?
3. Is there already a packet/procedure/tool for this domain?
4. Is the change additive and flag-gated?
5. What is the smallest acceptance test?

## After Editing

Update docs only with durable facts:

- new stable contract;
- new command/check;
- changed authority boundary;
- changed next step;
- known limitation that affects future agents.

Do not paste long build logs into these files.
