# Full Client Current State

Last verified: 2026-08-08 against focused tests and a clean `Gothic2Notr`
composition build.

This document separates four evidence levels. Source remains authoritative.

## Production-active composition

- The normal OpenGothic executable is C++23 and preserves native single-player.
- When the workspace sandbox target exists, the executable links its ASIO
  transport and enables the public runtime facade; a standalone build compiles
  with MMO transport disabled.
- SQLite capture/restore tooling is opt-in and disabled by default.
- Server-bound New Game and Continue/Load drive the facade session lifecycle:
  connect, authenticate or resume, obtain the roster, create/select a
  character, enter a world, then start the engine session.
- Save is not a client authority in server-bound mode.
- Runtime input and action paths translate movement, interaction, dialog,
  inventory and combat into typed intents. The bridge drains command
  completions and the typed presentation mailbox on the engine thread.
- In server-bound mode the normal inventory action opens a dedicated page over
  `ServerInventoryPresentationState`; native inventory behavior remains
  unchanged outside that mode.

## Implemented and focused-tested

- Native single-player telemetry is opt-in through
  `-native-telemetry <path.jsonl>` and is disabled when an MMO server flag is
  present. The parameter only arms the capture: `F12` starts it and a second
  `F12` stops it, with explicit lifecycle records in the JSONL file. While
  active, it writes one JSONL record per mouse press/release, game tick player
  position with yaw, and native world-item pickup attempt/success. Each record
  has an ordered sequence and a Unix millisecond timestamp. Pickup records
  retain player position, player yaw, source item coordinates and whether the
  native pickup succeeded. Movement-state and mob-distance feedback are not
  written, so this capture stays focused on the pickup distance and facing
  angle experiment. The generic JSONL writer remains reusable for a later
  telemetry event when another experiment needs it.
  The latest pickup run calibrated a 300-unit horizontal pickup range and a
  30-degree facing cone. The graphical client and sandbox reject an obviously
  invalid pickup before submitting it; the server repeats the distance check
  and, when it has a current movement-input yaw, the facing check.
  The earlier native capture reported walk/run maxima of 225/500
  units per second and bounds client/sandbox extrapolation to 500 horizontal
  units per second and 1450 vertical fall units per second. No native sneak
  sample was captured, so the existing sneak authority value remains unchanged.
  Production sustained run tuning is now 300 units/s after the user reported
  500 as too fast; graphical speed acceptance remains open.

- Engine intent DTO validation fails closed for incomplete legacy movement,
  generated interaction identity and missing item-stack revisions.
- A protocol-independent mailbox carries an optional route replacement, zero
  or more atomic bootstraps, then ordered live events.
- Facade output is validated and deterministically ordered by stream sequence
  before it reaches presentation state.
- Presentation state enforces route epoch, world generation, entity generation,
  baseline/revision monotonicity and bounded storage.
- Entity bindings prevent local-object aliasing; world-object bindings retain
  stable world-object identity separately from entity lifetime.
- Interpolation and movement correction reject stale, foreign-route and
  wrong-identity samples.
- In graphical server-bound mode the local player applies normal authoritative
  server transforms, not only explicit correction packets. Local Gothic
  movement cannot mutate its world position in this mode; rotation and visual
  input remain local, while position is owned by the server. This keeps the
  native Gothic position used for focus/pickup aligned with the server's
  fixed-tick position; the client-provided position remains diagnostic only.
  The 2026-10-21 correction makes that position gate effective even when the
  hero retains native input/rotation mode; previously the native-mode fallback
  admitted local displacement and the next server transform moved the hero back.
  Server corrections use the scoped presentation setter. NPC interpolation uses
  Gothic degrees throughout, including the shortest turn across ±180°, and
  complete NPC state carries the authoritative walk/run mode into animation.
  After the user confirmed facing and walking, delayed player updates exposed
  a second defect: live transforms used an input counter against world tick
  revisions. Player bootstrap/spawn and live transform revisions now share the
  server tick. Newly created grounded replicas also receive the bounded native
  floor offset previously available only to reused native NPCs. Those latest
  two changes were confirmed for floor alignment and removal of delayed position
  jumps. The user then reported an interrupted run cycle. Server-position players
  now skip native movement physics, whose rejected root-motion attempt otherwise
  called the collision handler and restarted Idle. Graphical confirmation of that
  final animation correction did not close acceptance: the user clarified that
  position still stalled for about three seconds while animation played. A later
  facade correction removes the requirement that the current route revision equal
  the last movement receipt revision. Other commands, including dialogs, may
  legitimately advance it; movement preparation already uses the current route.
  The real UDP facade regression covers that case. Graphical acceptance remains
  pending. Full local prediction/reconciliation is not implemented by these fixes.
  See [movement evidence](../../../../docs/llm/movement-facing-height-20261021.md).
  The 2026-10-24 follow-up removes receipt-by-receipt waiting, separates movement
  from inventory/dialog aggregate revision and prevents delayed input from
  undoing Stop. Real UDP loss and server-process restart gates pass, and the
  graphical executable builds. No active display is available for graphical
  acceptance in this environment. See the
  [current movement contract](../../../../docs/llm/player-movement-stream-20261024.md).
- Typed inventory/equipment read models retain server revisions and pending
  command completion state without optimistic quantity mutation. The
  server-backed page model builds equip, unequip, use, drop, split and merge
  requests with exact handles and revisions, disables actions while pending or
  resynchronizing, and exposes accepted, rejected and resync-required feedback.
- The server-bound corpse page opens only from a replicated dead entity with
  authoritative loot availability. Its bounded read model constructs open,
  exact-stack, take-all and close requests with corpse, stack and inventory
  revisions; it waits for authoritative snapshot/delta/closed records, disables
  actions while pending and closes on reroute, despawn, decay or resync.
- Typed live dialog presentation stages bounded server choice labels, renders
  them in the native menu and submits the selected stable ID with the exact
  dialog revision. Validated choice/update/end events now enter a bounded FIFO
  while a server line is visible, so consecutive NPC/player `AI_OUTPUT` lines
  cannot overwrite one another and terminal end waits behind the last line;
  server-authored lines remain visible for three seconds so the player can read
  the first line before selecting a response;
  choice-only refreshes install the new authoritative set without replaying the
  previous line. The UI still waits for authoritative update/end and never
  advances dialog or story state optimistically.
- Focused tests cover adapter validation, mailbox conversion, atomic bootstrap,
  route replacement, entity binding, interpolation, correction, catalog
  admission, inventory/equipment and corpse-loot projection, corpse-menu request
  adaptation, combat presentation and typed projectile
  spawn/state/impact/despawn handling.
- Projectile state is a self-contained upsert. A bounded full-client registry
  enforces frozen identity and route ownership, interpolates two authoritative
  samples, bounds extrapolation, pins impact positions and removes exact
  generations on despawn.

These tests compile selected full-client presentation components in the
sandbox test target. They do not instantiate the complete OpenGothic runtime.

Server dialog movies now stop the presentation queue until the first
`DialogMenu::tick` after `VideoWidget` finishes or is skipped. Later speech and
menu events retain their order and speaker. The real NK Xardas `Intro_ADDON`
scene and his subsequent line were confirmed graphically on 2026-10-21; see
[the scene evidence](../../../../docs/llm/dialog-video-xardas-20261021.md).

## Composition-integrated but not fully proven

The typed dialog chapter flag carries a bounded five-field presentation payload
(title, subtitle, image, sound, duration). `DialogMenu` drains earlier speech
before calling the presentation-only `Gothic::showChapterIntro`; it closes the
local dialog UI to expose the chapter screen. The bounded payload uses printable
separators accepted by the protocol text validator. Closing the chapter screen
sends a session/revision-bound receipt; the server then ends the dialog and
resumes the NPC routine. This path does not write local
chapter story state. The 2026-10-21 graphical run initially failed allocating
ray-tracing geometry; a rerun with `-rt 0 -gi 0 -rtsm 0` reached the live dialog.
The user confirmed the corrected chapter screen and that Xardas starts moving
only after it closes; the server log confirms the accepted presentation receipt
and subsequent native routine revision 2.

- `GameSession` consumes route, bootstrap and event records and materializes
  local player, remote player, NPC, world-object, interactive, mover, inventory,
  equipment and combat presentation into engine objects. It also owns sampled
  projectile presentation state, but actual mesh/particle/audio/decal objects are
  not materialized yet.
- Route replacement resets projection registries before installing the new
  bootstrap.
- An aggregate-revision rejection blocks further inventory submissions and
  requests a bounded Protocol V2 session restart; authoritative inventory and
  equipment replacement clears the resync requirement.
- The native corpse `ransack` path remains disabled in server-bound mode. The
  typed corpse page and production selective-loot snapshot/delta/closed
  projection are composition-integrated behind `TypedSelectiveLootV1`.
- Server replicas suppress local authority and use correction/interpolation
  paths.
- Server NPC replicas resolve the admitted presentation catalog's body, head,
  texture variants and captured default armor before they enter the world.
  Static MMO world objects
  also use their server transform as a bounded fallback when a transported
  world-object identity differs from the local VOB identity.
- World-object binding accepts the small coordinate difference caused by
  server quantization, so graphical interactive objects such as bookshelves
  can still resolve to their authoritative target. Replica NPCs clear local
  single-player weapon meshes before applying server equipment state, and
  startup world items use the same catalog/materialization path as dropped
  items.
- In graphical server mode an unmapped left mouse click is treated as the
  generic action. Book-like interactives submit authoritative `Read` and attach
  once to the native Gothic MOBSI state machine; there is no second hard-coded
  1.5-second timer or synthetic `onKeyInput` advance. A repeated action while
  already attached is consumed instead of restarting the presentation. This is
  still a graphical fallback: native MOBSI state functions can execute local
  script semantics, so the final server-bound cutover still requires explicit
  server-produced presentation events for script-affecting interactions. Item
  pickup submits the exact server item and inventory revisions and starts only
  the native pickup animation locally; world/inventory mutation remains server
  owned. A pending pickup blocks repeated mouse-down submissions for the same
  server item and is released by its receipt, despawn or transport timeout.
  Interaction and dialog command receipts keep their typed command identity in
  the full-client bridge, and the graphical bridge uses a longer response
  window while the native world is still loading, so a slow scene load does not
  discard the route before a pickup or dialog result arrives.
  When a real native world VOB had to provide an item presentation that
  was absent from the transported catalog, the client retains that exact
  instance/name binding for the resulting server-backed inventory stack.
- Reused native Gothic NPCs retain a bounded presentation-only vertical offset
  against the authoritative transform, avoiding visible floating caused by a
  different authored spawn anchor. During an authoritative dialog activity the
  replica also faces its replicated target after each transform sample; neither
  adjustment changes server position/rotation authority.
- Main-menu orchestration, the full-client reconnect/resume path, bridge
  locking and complete engine materializers are not directly covered by the
  focused unit target. Their strongest evidence is build/process/graphical
  execution.

## Known gaps and misleading legacy surfaces

1. The old string-based restore subsystem is still compiled and polled, but the
   bridge returns no legacy bootstrap snapshots and never populates its stored
   legacy snapshot. Typed mailbox bootstrap is therefore the effective path;
   the unreachable compatibility path is removal debt, not a supported mode.
2. The server-backed inventory page has focused model coverage and a complete
   client build, but still needs a graphical server-bound smoke covering visible
   pending/rejection/resync feedback and native-mode non-regression.
3. Live dialog choices are selectable and Protocol V2 now carries bounded
   authoritative UTF-8 line text. The full client deliberately does not
   reinterpret the stable numeric line ID as a Gothic OU/message key because
   it can resolve the wrong subtitle. Bootstrap restoration of the current
   line text and choices remains missing; ordinary eager startup NPCs still
   now use the production-composed Daedalus condition/information and ordered
   dialog-process lifecycle when canonical content is available. Bootstrap
   restoration of in-flight line/choice FIFO state and original voice/audio
   timing remain missing, and server-initiated `important` scheduling is not yet
   claimed as full Gothic parity.
4. Character-attribute facade records still have no full-client presentation
   component. Loot availability and corpse session records are mapped and
   emitted by production composition, but real graphical acceptance remains.
5. Legacy semantic-hook and movement/NPC observation code still needs
   classification as native-only, diagnostic, intent adaptation or obsolete.
6. No deterministic fake-facade test currently proves `mmoclientbridge`, menu
   orchestration or the full `GameSession` MMO projection; the present evidence
   is the headless page-model gate, sandbox regressions and full-client build.
7. The graphical launcher requires an explicit authority source. Real Gothic
   II / Night of the Raven validation uses `--content-root`; `--fixture` is an
   explicit synthetic process-gate mode and is not equivalent to the native
   rendered world.

Do not describe a feature as production-active merely because its DTO, mapper
or engine method exists.
