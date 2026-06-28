# MySQL MMO Readiness Dashboard

Migration `018_mmo_readiness_dashboard.sql` adds a high-level readiness gate.

`mmo_evaluate_mmo_readiness(...)` writes a run with blockers/warnings across:

- MySQL migration presence;
- active/orphan sessions;
- server action outbox failures/dead letters;
- event replay contract gaps;
- restore parity scenario pass coverage.

`v_mmo_remaining_work` gives the current remaining production areas:

1. C++ semantic hooks;
2. server RPC/MySQL adapter;
3. strict replay from baseline + journal;
4. restore parity runs;
5. server authority/network layer.
