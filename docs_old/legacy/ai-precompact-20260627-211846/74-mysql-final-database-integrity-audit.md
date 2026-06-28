# MySQL Final Database Integrity Audit

Migration `025_final_database_integrity_audit.sql` adds `mmo_run_final_database_integrity_audit(...)`.

It checks final DB invariants:

- wallet balances are non-negative;
- character inventory ownership and quantity match `item_instances`;
- equipped items still exist in inventory;
- world/container inventory ownership and quantity are consistent;
- world entity health is bounded;
- server/test events have idempotency keys;
- replay contract gaps are zero;
- dispatch contracts map to replay contracts;
- outbox has no failed/dead-letter actions;
- projection hash evidence exists;
- restore parity remains warning-only until real game parity runs exist.

Errors block DB completion. Missing real parity is intentionally not converted into a fake DB pass; it remains an external MMO blocker.
