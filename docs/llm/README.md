# OpenGothic Graphics Client - LLM Context

Compact, durable context for AI agents working on the full Gothic engine client
in single-player and MMO-bound modes.

The client is a **presentation and input layer**. In MMO mode, it delegates
authority to the server. In native single-player mode, it preserves the original
Gothic engine behavior unchanged.

## Minimal Read Order

For almost every task, read these first:

1. `current-state.md` - active goal, verified state, main gaps.
2. `next-work.md` - recommended implementation path and acceptance checks.
3. `agent-rules.md` - operating rules for safe AI-agent changes.
4. `repo-map.md` - only the section matching the files to be touched.

Then inspect the actual source files before changing code, SQL or tools.

## Conditional Reads

Read only when relevant:

- `architecture.md` - when changing authority boundaries, content flow, client/server flow.
- `api-contracts.md` - flags, UDP/bootstrap, read-model, DB identity and compatibility.
- `decisions.md` - durable decisions that should not be contradicted casually.
- `coding-style.md` - C++/SQL/Python style, safety and performance rules.
- `testing.md` - build/check commands and expected evidence.
- `domain-glossary.md` - project terminology.
- `roadmap.md` - larger staged direction after the immediate step.

## Subsystem Context

The OpenGothic project is layered:

```
Client (this subsystem)  ← presentation, rendering, input, single-player logic
       ↓
Shared (src/shared/)     ← binary packets, codec, game constants, entity types
       ↓
Server (src/server/)     ← authority, simulation, MySQL, MMO state
       ↓
Sandbox (src/client_sandbox/) ← test harness, protocol validator
```

The client depends on shared protocol but NOT on server (loose coupling).
Server and sandbox share identical protocol codecs.

## Ground Rules

- Source code is the source of truth when docs and implementation disagree.
- Preserve native single-player behavior unless explicit MMO flags are used.
- The C++ MMO server is the authority in MMO mode.
- Client hooks may emit intents/evidence; they must not become MMO truth.
- Prefer small, verifiable changes over broad rewrites.
- Keep these docs as stable context, not as a log dump.
- Use `constexpr` where it improves compile-time clarity or enables static validation.
- Prioritize maximum performance without sacrificing correctness.
- Write safe, readable and production-quality code as a senior C++ engineer.
