# API Contracts — Full Client MMO Boundary

The client-facing bridge is `Mmo` functions in `mmoclientbridge.h`. Its
implementation delegates to `ClientRuntimeFacade`.

Stable responsibilities:

- configure/start/stop the facade from command-line MMO mode;
- submit validated client intents and explicit observations;
- drain server live deltas, dialogs, entity transforms, bootstrap snapshots,
  bootstrap ACK/status, diagnostics and faults;
- translate server entity handles into local presentation identities.

Wire schema, endpoint retry and packet sequencing are not client API concerns.
New engine features should consume domain presentation events rather than
including shared wire headers directly where practical.
