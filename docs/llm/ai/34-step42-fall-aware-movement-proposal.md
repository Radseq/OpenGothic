# 34 Step42 Fall-aware Movement Proposal Authority

Step42 extends the Step41 movement proposal harness with fall-aware validation.
The real test that motivated this was a player deliberately jumping/falling from
a cliff during movement-proposal capture. A production server must not treat
all large vertical movement as cheating, but it also must not accept upward fly
or impossible downward teleports from the client.

Implemented changes:

- `movement_proposal` C++ payload now includes previous and current motion-state
  flags around the proposal boundary:
  - `from_is_in_air`, `from_is_falling`, `from_is_falling_deep`, `from_is_slide`,
    `from_is_jump`, `from_is_jump_up`, `from_is_swim`, `from_is_dive`,
    `from_is_in_water`;
  - corresponding `to_*` flags from the current NPC state;
  - `vertical_axis="y"`, because OpenGothic/Gothic height checks use `position().y`.
- The proposal payload now also carries previous HP/mana fields:
  - `from_health_current`, `from_health_max`, `from_mana_current`, `from_mana_max`.
  This allows fall-damage evidence to be correlated with a falling movement
  segment without trusting the client as final damage authority.
- `check_mmo_step41_movement_proposal_jsonl.py` is now a Step42 fall-aware
  authority validator:
  - horizontal speed is always strict;
  - upward movement has separate strict `--max-upward-speed` and
    `--max-upward-delta` limits;
  - downward movement can use fall-specific `--max-fall-speed` and
    `--max-fall-delta` only when the proposal is marked as airborne/falling/
    jump/slide/swim/dive/water, or when the down-step is within the small
    unmarked tolerance;
  - large unmarked drops can be rejected with `--require-motion-state-for-large-fall`;
  - accepted proposals include continuity checks so a rejected hostile segment
    does not allow later stale client proposals to silently continue from a
    different server position.
- New negative-corpus tools verify that authority rejects hostile proposal rows:
  - horizontal teleport;
  - upward fly;
  - unmarked large downward drop;
  - impossible marked fall;
  - time reversal;
  - invalid numeric position.

This is still a dev server-authority harness, not final network movement. The
production direction remains:

```text
client movement intent/proposal -> MMO server validation/correction -> accepted checkpoint/projection -> replication
```

A future live server must send correction/snapback or resync after a rejected
proposal. The continuity check models that requirement in offline replay by
rejecting dependent stale proposals after the first bad segment.
