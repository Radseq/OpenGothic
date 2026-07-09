# Step258 - Client Dialog Presentation Boundary

Scope: C++ only, no DB/tools mutation.

Added a disabled-by-default client validation boundary for decoded
`ServerNpcDialogIntent` packets. It can classify whether a packet is shaped for
future presentation and optionally return validate-only ACK/NACK evidence.

No subtitle UI, audio playback, local dialog mutation or single-player behavior
change is enabled.
