# ADR 0005 — Server-Revisioned UI Has No Optimistic Authority

Status: accepted

## Context

Immediate local mutation makes inventory, equipment, combat and dialog feel
responsive but diverges under rejection, duplicate delivery, reconnect or
concurrent server changes.

## Decision

Server-owned UI reads revisioned snapshots/deltas and submits commands with
exact identity and expected revision. A pending indicator may appear
immediately; authoritative values change only after accepted server projection.
Receipts report command completion but do not replace the resulting state
revision. Visual combat prediction must not mutate replicated HP or death.

## Consequences

- Rejection and stale-revision feedback are first-class UI states.
- Route replacement cancels or invalidates pending UI work.
- Native UI may retain local mutation in native mode.
- New server-owned screens need read-model, intent, receipt and resync behavior,
  not direct access to engine gameplay containers.
