# Step266 - Server Conversation Late-Observer Resume Packet Boundary

Scope: C++ only, no DB/tools mutation.

Added a no-send typed packet boundary for late-observer resume candidates. It
can build/log a `ServerNpcDialogIntent` replay candidate with remaining
duration and fresh action/ACK identity.

It does not register delivery state or send UDP.
