# Full Client Next Work

Global execution order remains in the root roadmap. When client work is
scheduled, perform the smallest ordered edge below; do not start broad UI or
rendering work before its preceding invariant is closed.

## C1 — Remove the dead bootstrap/restore side channel

Goal: typed facade mailbox bootstrap is the only server-bound restore source.

Acceptance:

- remove production polling, storage and parsing of the unreachable legacy
  string snapshot path;
- keep native save/load behavior unchanged;
- startup and reconnect wait only on typed session/bootstrap state;
- no filesystem, JSON or SQLite fallback can activate gameplay state;
- add a regression test proving route replacement plus typed bootstrap resets
  old projection state without consulting legacy restore code.

## C2 — Activate revision-safe inventory UI

Goal: the normal inventory action opens a server-backed view in server-bound
mode.

Acceptance:

- UI reads only typed inventory/equipment models, never `Npc::inventory`;
- equip, unequip, use, drop, split and merge submit exact stack handles and
  expected revisions;
- pending state is visible; rejection is visible; local quantity/equipment does
  not change before an authoritative update;
- route replacement closes or resets stale UI state;
- native inventory behavior remains unchanged;
- focused tests cover input-to-view/action wiring, followed by graphical smoke.

## C3 — Add a full-client composition seam

Goal: test bridge/session orchestration without Vulkan, audio or private assets.

Acceptance:

- inject or wrap the public facade behind a deterministic fake;
- cover authenticate/resume, roster, enter-world, reconnect and failure without
  native fallback;
- cover mailbox drain order, route reset and command-completion delivery;
- keep transport tests in `client_sandbox`; do not reimplement sockets or wire
  records in the client.

## C4 — Complete missing presentation families

In order: dialog text/audio and bootstrap choice restoration, character
attributes, loot availability, then remaining server-owned UI state. Extend
shared/server/sandbox contracts first when the authoritative payload is absent.
Every UI projection must retain identity and revision and tolerate rejection,
reroute and resync.

## C5 — Retire migration hooks

Classify each semantic hook and sampler as one of: native-only behavior,
server-bound intent, bounded diagnostic or obsolete compatibility. Delete the
last category and prevent the diagnostic category from mutating gameplay.

## Gate for each item

Run static boundary checks, focused tests, a normal client build, the relevant
process gate and a graphical two-client smoke. Record only reproducible command
names and failures; do not copy logs into canonical context.
