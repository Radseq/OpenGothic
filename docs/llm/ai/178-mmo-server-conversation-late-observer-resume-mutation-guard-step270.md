# Step270 - Server Conversation Late-Observer Resume Mutation Guard

Scope: C++ only, no DB/tools mutation.

Added the final no-mutation guard for replay candidates. It validates the future
order: record outbound delivery, record conversation observer, then send UDP.

The guard executes none of those mutations and keeps `mark_applied` blocked.
