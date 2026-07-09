# Step257 - AI Dialog Intent Runtime Delivery State

Scope: C++ only, no DB/tools mutation.

Added `server/cpp/mmo_outbound_gameplay_delivery_state.h` and wired the
diagnostic `ServerNpcDialogIntent` send/ACK path into in-memory pending,
ACK/NACK, duplicate, conflict and timeout classification.

Client-side dialog-intent ACK replay remains gated by MMO flags and has no
UI/audio side effects. Durable retry, dead-letter and `mark_applied` are
deferred to later DB work.
