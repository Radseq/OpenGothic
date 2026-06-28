# MySQL Strict Replay Journal Audit

Migration `021_strict_replay_journal_audit.sql` adds `mmo_audit_strict_replay_journal(...)`.

It is stronger than the Step 17 contract check, but still not a full replay engine. It verifies that server/test events have registered projection contracts, event classes match, idempotency is present, payloads are JSON objects, projection offsets are not ahead of the journal, applied outbox actions reference journal events, and failed/dead-letter actions are visible as warnings.

The full replay engine still needs to rebuild a clean projection from content baseline plus `world_event_journal` and compare that clean projection against current tables.
