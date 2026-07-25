# Full Client Contract Map

Use this file only to select the contract owning the changed boundary. Source
declarations and tests remain canonical.

| Contract | Owns |
|---|---|
| [`intent-submission-contract.md`](intent-submission-contract.md) | Engine input/UI conversion, exact typed requests, submission outcome and native/server-bound separation |
| [`presentation-mailbox-contract.md`](presentation-mailbox-contract.md) | Facade snapshot conversion, rejection accounting, route/bootstrap/event ordering and engine-thread drain |
| [`route-projection-contract.md`](route-projection-contract.md) | Route identity, atomic bootstrap, monotonic live state, exact generations and engine-object projection |
| [`authoritative-ui-contract.md`](authoritative-ui-contract.md) | Revisioned read models, pending commands, rejection, dialog choices and non-optimistic UI |

Durable rationale is indexed in [`../adr/README.md`](../adr/README.md). Current
activation belongs in `current-state.md`; unfinished sequencing belongs in
`next-work.md`.
