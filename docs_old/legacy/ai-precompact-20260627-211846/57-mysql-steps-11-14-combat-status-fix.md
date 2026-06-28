# MySQL Steps 11-14 Combat Status Fix

Fixes the first smoke-test failure in `mmo_apply_character_damage`:

```text
ERROR 1054 (42S22): Unknown column 'c.status' in 'where clause'
```

The production `characters` table uses `lifecycle_state`, not `status`.
The corrected `012_combat_resource_write_path.sql` recreates `mmo_apply_character_damage` with:

```sql
c.lifecycle_state = 'active'
```

The smoke validator was also hardened so it restores the character HP/mana baseline even if a destructive smoke test fails halfway through.

Reapply only migration 012 after copying the fix; migrations 011, 013 and 014 do not need to be re-run for this specific error.
