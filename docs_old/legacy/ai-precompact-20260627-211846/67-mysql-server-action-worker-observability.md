# MySQL Server Action Worker Observability

Migration `020_server_action_worker_observability.sql` adds worker run/result telemetry for the outbox dispatcher.

The dev tool `tools/run_mysql_mmo_action_worker.py` can claim pending actions through `mmo_claim_next_server_action(...)`, execute supported stored procedures, mark the outbox action as applied/failed, and record worker results.

This is deliberately a development bridge, not the final production networking layer. A real MMO server should replace it with an RPC/worker process that validates client intent, applies server authority rules and calls the same stored procedures.
