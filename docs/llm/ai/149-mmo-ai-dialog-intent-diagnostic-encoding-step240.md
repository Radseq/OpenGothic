# Step240 - AI Dialog Intent Diagnostic Encoding

Purpose: prove the Step239 diagnostic packet fits the existing binary
`ServerDiagnosticPacket` encode/decode boundary.

Changed:

- `server/cpp/mmo_npc_perception_dialog_intent_diagnostic_encoder.*`
- `server/cpp/mmo_npc_perception_action_dispatcher_probe.cpp`

Contract:

- `--encode-preview-diagnostic-packet` requires a Step239 packet.
- `--require-preview-diagnostic-encoding` fails with exit code `8` on failed
  encode/decode or sizing checks.
- Adapter validates datagram fit, client field limits, decode round-trip and
  decoded field equality.
- Send, fan-out, dialog UI, audio and mark-applied booleans remain false.

This is binary contract validation only. It still does not send anything.
