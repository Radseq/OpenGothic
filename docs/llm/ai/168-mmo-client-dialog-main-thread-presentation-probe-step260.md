# Step260 - Client Dialog Main-Thread Presentation Probe

Scope: C++ only, no DB/tools mutation.

Added a disabled-by-default game-thread probe drained from `GameSession::tick`
for decoded server dialog intents. The network thread keeps the fast ACK path;
the game thread writes no-apply presentation evidence.

No real UI/audio is applied and native behavior is unchanged without flags.
