# MySQL External Integration Gates

Migration `029_external_integration_gates.sql` adds explicit external gates:

- C++ hooks for world items/NPC lifecycle;
- C++ hooks for inventory/equipment;
- C++ hooks for trade;
- C++ hooks for combat/spells/ammunition;
- C++ hooks for interactives;
- C++ hooks for quest/dialog/script;
- production RPC/server worker;
- deterministic replay executor;
- restore parity runner;
- MMO server authority/network layer.

The table lets the DB dashboard say: database layer is complete, but full MMO readiness is still blocked by source/server work.
