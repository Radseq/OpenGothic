# MySQL Restore Parity Artifacts

Migration `022_restore_parity_artifacts.sql` adds artifact hashes for parity proof.

For each parity scenario, a real run should record:

- `native_sav` hash;
- `sqlite_snapshot` or `runtime_sqlite` hash;
- `mysql_projection` hash.

`v_restore_parity_artifact_comparison` reports whether all three match. `mmo_materialize_restore_parity_artifact_results(...)` writes those comparisons into the Step 16 parity result table so the readiness dashboard can consume them.

A scenario is only production-green when all required durable components match or every difference is explicitly classified as transient presentation/runtime noise.
