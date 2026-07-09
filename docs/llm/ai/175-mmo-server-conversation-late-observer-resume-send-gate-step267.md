# Step267 - Server Conversation Late-Observer Resume Send Gate

Scope: C++ only, no DB/tools mutation.

Added a no-send send gate for replay candidates. It classifies endpoint
availability, identity, datagram size and duplicate `action_id` / `ack_key`
before any future replay send can be allowed.

It produces evidence only and does not call UDP send.
