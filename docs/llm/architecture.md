# Architecture — Full Client

## Native mode

OpenGothic retains its original local simulation, scripts, saves and UI.

## MMO mode

```text
engine input -> thin client adapter -> client_sandbox facade -> server
server output -> facade mailboxes -> presentation registry/UI/audio/animation
```

Prediction/interpolation may improve visual responsiveness. Reconciliation and
all gameplay outcomes remain server-owned. A server replica never executes
local authoritative NPC logic.

The bridge must not expose ASIO, sockets or packet codecs to engine systems.
