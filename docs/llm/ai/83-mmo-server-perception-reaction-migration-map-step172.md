# MMO server perception and NPC reaction migration map - step 172

## Durable scope update

Gothic 1 is not a target.

The MMO server targets Gothic II NotR semantics only. Old notes may mention
Gothic 1 differences as archaeology, but new gameplay authority work should not
spend design complexity on Gothic 1 unless the user explicitly reopens that
scope.

## What changed

Added the first server-side perception catalog:

- `server/cpp/mmo_server_perception_authority.h`
- `server/cpp/mmo_server_perception_authority.cpp`

It maps all Gothic II `PERC_*` ids from `game/game/constants.h` into a compact
server vocabulary:

- active scan vs passive event,
- social/combat/crime/sound/magic/movement/item/room/command domain,
- likely server responsibility,
- whether the perception needs a witness,
- whether it needs a victim,
- whether it may interrupt current routine/action.

This is not final AI authority yet. It is the checklist-backed foundation that
prevents perception migration from becoming ad-hoc.

## Client sources inspected

Primary client paths:

- `Npc::perceptionProcess(Npc&)`
- `Npc::perceptionProcess(Npc&, Npc*, float, PercType)`
- `WorldObjects::sendPassivePerc(...)`
- `WorldObjects::sendImmediatePerc(...)`
- `WorldObjects::passivePerceptionProcess(...)`
- `GameScript::npc_sendpassiveperc(...)`
- `GameScript::npc_sendsingleperc(...)`

Important observed behavior:

- active perception scans check `PERC_ASSESSPLAYER`, `PERC_ASSESSENEMY`,
  `PERC_ASSESSBODY`;
- passive perceptions are queued as `PerceptionMsg`;
- passive perception checks range by `percRanges().at(perc, senses_range)`;
- passive perception mostly ignores `INpc::senses`, except the commented
  hearing note for quiet sound/enter room;
- perception handlers are script functions stored on `INpc`;
- `PERC_ASSESSMAGIC` has a player default handler fallback;
- theft currently emits `PERC_ASSESSTHEFT` when player takes a world item;
- damage emits `PERC_ASSESSDAMAGE`, `PERC_ASSESSOTHERSDAMAGE`,
  `PERC_ASSESSDEFEAT`, `PERC_ASSESSMURDER`;
- weapon removal emits `PERC_ASSESSREMOVEWEAPON`;
- killing the current target can emit `PERC_ASSESSMURDER`;
- fight sound emits `PERC_ASSESSFIGHTSOUND`;
- quiet sound can be emitted immediately around player/NPC noise events.

## Full migration checklist

### Perception table and ranges

Move to server:

- `PERC_*` id table,
- per-NPC perception function assignment,
- perception enable/disable state,
- `senses_range`,
- `senses` flags,
- script `percRanges()` values,
- active perception next-tick cooldown,
- perception queue ordering.

Server nuance:

- perception must be computed once by the server, not separately per client;
- clients may still predict visuals, but they must not independently decide
  whether an NPC noticed a crime or starts combat;
- perception state is runtime state, not final DB truth, except durable crime or
  attitude consequences.

### Active perception scans

Move to server:

- `PERC_ASSESSPLAYER`,
- `PERC_ASSESSENEMY`,
- `PERC_ASSESSFIGHTER`,
- `PERC_ASSESSBODY`,
- `PERC_ASSESSITEM`.

Server nuance:

- replace client-local `npcNear` with server interest/spatial partitions;
- use server positions and LOS/senses authority;
- target choice must be deterministic for all players;
- nearest enemy/body/item must be chosen once and replicated;
- do not let every nearby client run a different active scan result.

### Passive event routing

Move to server:

- event queue equivalent of `PerceptionMsg`,
- event origin position,
- event source NPC/player,
- `other`,
- optional `victim`,
- optional item instance,
- range filtering,
- script handler invocation scheduling.

Server nuance:

- passive events must be idempotent and ordered by server tick;
- multiple witnesses must see the same crime/event result;
- late-joining players receive the resulting NPC action/state, not a replay of
  private client perception decisions;
- queue should be runtime memory first, not coupled to the temporary DB.

### Weapon visibility and threat

Move to server:

- `PERC_DRAWWEAPON`,
- `PERC_ASSESSREMOVEWEAPON`,
- `PERC_ASSESSTHREAT`,
- threat escalation from weapon draw near guards/NPCs,
- threat cancellation when weapon is holstered or actor leaves range.

Server nuance:

- server owns whether an NPC saw the weapon;
- player weapon-state packets are evidence only until validated;
- reaction can interrupt talk, sleep, routine, mob use and conversation;
- reaction priority must beat low-priority activity but not override already
  higher-priority combat/death.

### Theft and item crimes

Move to server:

- `PERC_ASSESSTHEFT`,
- `PERC_CATCHTHIEF`,
- `PERC_OBSERVESUSPECT`,
- `PERC_ASSESSUSEMOB`,
- item ownership/guild ownership,
- container ownership,
- room/sector ownership,
- witness list,
- suspect/thief state,
- warning escalation.

Server nuance:

- item pickup/container take must be authorized before final inventory commit;
- a thief cannot be innocent on one client and guilty on another;
- visible theft depends on server LOS/range and ownership, not only local client
  animation;
- stolen-item consequences may be durable, but the perception queue itself is
  transient.

### Attacks and violence

Move to server:

- `PERC_ASSESSDAMAGE`,
- `PERC_ASSESSOTHERSDAMAGE`,
- `PERC_ASSESSDEFEAT`,
- `PERC_ASSESSMURDER`,
- `PERC_ASSESSFIGHTSOUND`,
- attack victim,
- attacker,
- target faction/guild,
- murder vs defeat/unconscious distinction,
- friendly-fire handling.

Server nuance:

- damage events must be produced by server damage authority, not by client HP
  deltas;
- guard/faction reaction depends on authoritative attacker/victim identity;
- nearby witnesses must receive one shared reaction decision;
- combat action priority must interrupt conversations, sleep, routines and
  passive ambient actions.

### Guild, faction and attitude reactions

Move to server:

- guild values,
- true guild,
- temporary/permanent attitude,
- `B_Assess*` script decisions,
- ally/enemy checks,
- faction crime response,
- guard-specific escalation,
- fake guild detection via `PERC_ASSESSFAKEGUILD`.

Server nuance:

- attitude changes are shared world state;
- temporary hostility must be replicated and eventually expire/reset according
  to script semantics;
- permanent story/faction consequences must go through script authority, not
  arbitrary server heuristics;
- do not invent faction rules without checking Gothic II scripts.

### Guard and warning behavior

Move to server:

- `PERC_ASSESSWARN`,
- `PERC_OBSERVEINTRUDER`,
- `PERC_ASSESSENTERROOM`,
- guard posts/routines,
- forbidden room/sector checks,
- warning count/state,
- escalation from warn to attack/call.

Server nuance:

- guards must not warn different players differently because of local client
  timing;
- warning text/dialog is replicated output, but the decision is server-owned;
- a new player entering range should see the guard already in warning/attack
  state.

### Magic reactions

Move to server:

- `PERC_ASSESSMAGIC`,
- `PERC_ASSESSSTOPMAGIC`,
- `PERC_ASSESSCASTER`,
- spell target/victim,
- spell category,
- magic crime/threat rules.

Server nuance:

- magic perception should be produced from server-owned cast lifecycle;
- client spell animation is evidence only;
- spell effects and damage must share the same event id as the perception that
  triggered reactions.

### Sound and calls

Move to server:

- `PERC_ASSESSFIGHTSOUND`,
- `PERC_ASSESSQUIETSOUND`,
- `PERC_ASSESSCALL`,
- sound origin,
- audible range,
- hearing/senses rules,
- caller/victim/target references.

Server nuance:

- server emits sound perception events from authoritative actions;
- clients may play audio locally, but cannot decide who heard it;
- sound events need cooldown/coalescing to avoid perception spam.

### Talk and social perception

Move to server:

- `PERC_ASSESSTALK`,
- `PERC_ASSESSGIVENITEM`,
- `PERC_ASSESSSURPRISE`,
- dialog start permission,
- item gift reaction,
- social interruption.

Server nuance:

- dialog start must be server-authorized;
- conversation state must be shared for late joiners;
- NPC cannot be in private talk state for one player and routine/combat for
  another.

### Movement and commands

Move to server:

- `PERC_MOVEMOB`,
- `PERC_MOVENPC`,
- `PERC_NPCCOMMAND`,
- mob movement/use,
- NPC command target,
- command authority.

Server nuance:

- mob/NPC manipulation must be server-authorized;
- movement perceptions should feed action priority, not directly mutate final
  DB state;
- commands should be idempotent and reject stale targets.

## Required server modules

Build these as replaceable modules:

- `PerceptionCatalog` - stable `PERC_*` metadata.
- `PerceptionSensor` - range, senses, LOS, spatial query.
- `PerceptionQueue` - ordered runtime events.
- `PerceptionScriptAdapter` - calls Gothic II script handlers or server ports
  of those handlers.
- `CrimeAuthority` - ownership, forbidden room, theft, witness, suspect.
- `ReactionAuthority` - warn/flee/attack/ignore/call/talk interruption.
- `ThreatAuthority` - combat/threat priority and interruption rules.
- `NpcActionPriority` integration - perception can preempt lower-priority
  routine actions.
- `Replication` integration - current reaction/action visible to late joiners.

## What must not happen

- Do not invent generic MMO guard rules without checking Gothic II scripts.
- Do not let each client independently run final perception decisions.
- Do not persist every perception event into the temporary DB.
- Do not mix final storage design with runtime perception queues.
- Do not make a single huge AI file; perception, crime, threat, reaction and
  script adapters must stay modular.

## Verified

Built successfully:

```text
cmake --build build\mmo_cpp_server --target mmo_udp_server --config Debug
```

## Next good step

Add `PerceptionQueue` as an in-memory server module and feed it from already
observed semantic events:

- weapon draw/holster,
- theft/pickup/container take,
- combat damage,
- combat intent,
- magic intent.

Initially it should only log/coalesce and classify events. Later it can invoke
script-backed reactions and become hard authority.
