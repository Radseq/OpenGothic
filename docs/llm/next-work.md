# Next Work — Full OpenGothic Client

Last updated: 2026-07-11.

The global roadmap currently prioritizes Protocol V2, typed replication and
binary bootstrap before broad MMO UX. Client changes should prepare that
boundary without inventing a second transport.

## 1. Thin domain adapter over the sandbox facade

- replace wire-family knowledge in engine code with domain-level request and
  presentation types;
- keep packet encoding/versioning private to sandbox/shared;
- centralize entity handle to local-object binding and stale-generation checks;
- preserve native single-player behavior behind existing mode checks.

## 2. Classify semantic hooks

For each callback in `mmosemantichooks.*`, choose:

- valid client intent;
- non-authoritative observation/diagnostic;
- native single-player-only behavior;
- obsolete migration hook to delete.

Do not send before/after stats, damage, wallet, quest, NPC death or world-state
results as MMO truth.

## 3. Typed bootstrap and live presentation

After shared/sandbox contracts exist:

- consume typed bootstrap sections directly from facade mailboxes;
- create/despawn presentation objects from entity lifecycle events;
- apply transform interpolation and corrections;
- project inventory/equipment/dialog/quest/world events into UI;
- handle bounded resync/reconnect without local save files.

## 4. MMO character UX

- New Game creates a server character;
- Continue/Load lists server characters without selecting local save files;
- Save is disabled/replaced in MMO mode;
- remove hardcoded development character/session identities;
- world transitions use server approval and loading presentation.

## Acceptance

- no full-client MMO socket, codec, retry or bootstrap assembler;
- no local gameplay result can be submitted as authority;
- replicated NPCs never run local authoritative AI;
- the same behavior is proven first in the headless sandbox;
- native single-player remains unchanged when MMO mode is off.
