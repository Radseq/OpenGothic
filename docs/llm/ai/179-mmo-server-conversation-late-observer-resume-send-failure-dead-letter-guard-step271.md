# Step271 - Server Conversation Late-Observer Resume Send-Failure Dead-Letter Guard

Scope: C++ only, no DB/tools mutation.

Added a no-mutation guard for the future UDP send-failure branch. It proves the
needed terminalization/dead-letter evidence without changing runtime state.

Durable dead-letter storage is deferred to the Step273 DB contract.
