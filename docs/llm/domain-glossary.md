# Domain Glossary — Full Client

- **Server replica** — local presentation object whose gameplay state is
  server-owned.
- **Presentation binding** — mapping from server entity handle/stable key to a
  local engine object.
- **Interpolation** — visual smoothing between authoritative transforms.
- **Prediction** — reversible local visual estimate; never a committed gameplay
  result.
- **Reconciliation** — applying server correction to predicted/presented state.
- **Native mode** — original single-player execution without MMO authority.
- **MMO-bound mode** — client adapter plus sandbox facade and server truth.
