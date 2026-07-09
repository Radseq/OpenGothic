# Step259 - Server Gameplay Fanout AOI

Scope: C++ only, no DB/tools mutation.

Added `mmo_server_gameplay_fanout.h`, a runtime target/AOI recipient selector
for server-owned gameplay packets. Default mode stays `target_session`; AOI is
explicitly enabled and bounded by radius/max-recipient settings.

The selector only classifies recipients and rejects. It does not persist fanout,
send packets by itself, or mark actions applied.
