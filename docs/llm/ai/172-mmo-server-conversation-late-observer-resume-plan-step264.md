# Step264 - Server Conversation Late-Observer Resume Plan

Scope: C++ only, no DB/tools mutation.

Added a disabled-by-default planner for late observers joining an open
diagnostic conversation line. It computes whether a replay candidate is valid
and how much duration remains.

No resume packet is built or sent in this step.
