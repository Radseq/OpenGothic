# MySQL Final Read Models

Migration `028_final_read_models.sql` adds read/admin views:

- `v_mmo_character_load_sheet_final`
- `v_mmo_world_state_summary_final`
- `v_mmo_database_final_dashboard`

These views are for admin/server loading and diagnostics. They do not replace stored procedures for mutations. Gameplay writes must still go through semantic write paths with event + projection in one transaction.
