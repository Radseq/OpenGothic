# MySQL Database Completion Registry

Migration `023_database_completion_registry.sql` introduces the final requirement registry and run/result tables:

- `mmo_database_completion_requirements`
- `mmo_database_completion_runs`
- `mmo_database_completion_results`
- `v_mmo_database_completion_requirements`
- `v_mmo_database_completion_latest`
- `v_mmo_database_completion_blockers`

The key design point is separation between DB-layer completion and full MMO readiness. Requirements can be `required_for_db` and/or `required_for_mmo`. C++ hook insertion, production worker implementation, restore parity execution and server authority are external gates. They are not faked by SQL.
