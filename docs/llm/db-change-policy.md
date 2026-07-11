# Database Change Policy

- Do not modify or reset a database unless the user explicitly requests it.
- Define typed domain commands, events, projections, revisions and repository
  ports before freezing final SQL.
- Keep content-build data, runtime gameplay state and AI/runtime diagnostics
  separated by ownership and lifetime.
- Prefer ordinary repository code over views/procedures that hide gameplay
  logic; use transactions where the database must enforce atomic durability.
- Do not block the server world tick on database I/O.
- Destructive tools require explicit guard flags and must be reported clearly.
- Client SQLite tooling and the LLM repository index are derived local tools,
  not production MMO persistence.
