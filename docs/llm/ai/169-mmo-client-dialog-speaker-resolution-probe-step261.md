# Step261 - Client Dialog Speaker Resolution Probe

Scope: C++ only, no DB/tools mutation.

Extended the main-thread dialog probe with safe local speaker resolution for
supported `npc:<...>:pid:<...>:sym:<...>` and `character:<key>` formats.

Resolution is evidence-only. Unresolved speakers are skipped safely; no dialog
UI/audio state is changed.
