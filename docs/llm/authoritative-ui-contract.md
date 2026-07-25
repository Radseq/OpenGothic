# Full Client Authoritative UI Contract

## Purpose

Define how UI presents server-owned state without becoming a second authority.
The contract covers revisioned read models, pending commands, rejection and
route-safe reset for inventory, equipment, dialog and future server-owned
screens.

Native-mode UI may retain native local mutation. Server-bound UI follows this
contract after mode selection.

## API and contracts

Inventory and equipment presentation use the bounded read models in
`src/client/game/game/mmoserverinventoryreadmodel.h`:

- authoritative snapshots install a complete revision;
- deltas apply only when identity and revision rules succeed;
- item stacks retain exact stack handles;
- equipment bindings retain exact slot and item identity;
- pending commands are presentation state, not inventory truth;
- completion/rejection is recorded separately from resulting authoritative
  state.

A command receipt says whether submission completed. It does not authorize the
UI to invent the resulting quantity, equipment slot or aggregate revision.
Reconciliation occurs when authoritative projection arrives.

Live dialog presentation uses stable server choice IDs and the exact dialog
revision. Selecting a choice submits intent and may show pending state, but the
UI remains open until the authoritative update or end event. Route replacement
or dialog revision change invalidates stale selections.

The same rules apply to combat HUD, character attributes, loot availability,
quests, trade and other server-owned screens:

- render an accepted read model;
- submit exact typed intent;
- show pending/rejection explicitly;
- update values only from authoritative projection;
- reset or close on route replacement when state is no longer valid.

## Data and state

UI may own:

- selection, focus, scroll position and other transient view state;
- bounded pending-command indicators keyed by command token/exact identity;
- last user-visible rejection or recovery message;
- references to immutable or revisioned presentation models.

UI must not own a competing mutable copy of authoritative inventory, HP,
prices, dialog progress, quest state or world ownership.

Inventory presentation separates:

- inventory revision and stacks;
- equipment revision and slot bindings;
- pending command lifecycle;
- last rejection.

Route replacement resets these route-scoped models and pending entries before
new bootstrap activation.

## Dependencies

Upstream contracts:

- `intent-submission-contract.md` for exact requests;
- `presentation-mailbox-contract.md` for accepted delivery;
- `route-projection-contract.md` for route and revision safety.

Implementation owners include:

- `mmoserverinventoryreadmodel.h`;
- `mmoserverpresentationstate.h` dialog state;
- `gamesession_mmo_inventory.cpp` and relevant UI controllers;
- `mmoserverdialogpresentation.h` for bounded dialog presentation validation.

Required decision:

- [ADR 0005](../adr/0005-server-revisioned-ui-has-no-optimistic-authority.md).

When a required authoritative payload does not exist, extend shared, server and
sandbox contracts first. Do not infer the missing value from native engine
containers.

## Examples

### Equip item

```text
UI revision=21, exact stack={instance=9,generation=2}
  -> submit equip with expected inventory/equipment revision
  -> mark command pending
  -> receipt accepted
  -> wait for authoritative inventory/equipment update
  -> reconcile read models and clear pending state
```

A receipt without the resulting state update does not move the item locally.

### Stale dialog selection

```text
UI shows dialog revision=8
server advances dialog to revision=9
user action still carries revision=8
  -> submission or authoritative processing rejects stale revision
  -> UI displays rejection/resync state
  -> no optimistic dialog close or story mutation
```

### Route replacement

```text
old-world inventory/dialog UI is open
  -> newer route replaces the world
  -> close/reset route-scoped UI and pending commands
  -> install new bootstrap read models
```
