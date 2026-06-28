# MySQL Restore Parity Gate

Migration `016_restore_parity_gate.sql` adds a concrete restore parity registry and run/result tables.

Required scenarios are seeded for:

1. bookstand/bookshelf script flag + XP;
2. loose item pickup;
3. equip/unequip;
4. container inventory/state;
5. quest progress;
6. dialog consumed;
7. NPC killed;
8. chapter change;
9. save/restart/load comparison.

A smoke run may mark these as `blocked` because it does not actually launch the game, save a `.sav`, restart, restore and compare hashes. A production parity run must compare native `.sav`, SQLite save-slot projection and MySQL projection for each scenario.
