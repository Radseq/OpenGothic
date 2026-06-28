# MySQL Projection Hash Manifest

Migration `024_projection_hash_manifest.sql` adds canonical projection component hashes:

- `mmo_projection_hash_runs`
- `mmo_projection_component_hashes`
- `mmo_materialize_projection_hash_run(...)`
- `v_projection_hash_latest`
- `v_projection_hash_latest_components`

The hash manifest is not a replacement for replay. It is evidence for restore/parity/DB-only load gates. It records deterministic count + checksum hashes over character and world projections such as stats, wallet, inventory, equipment, quest/dialog/script state, world entities, world inventory, world script state and the event journal.
