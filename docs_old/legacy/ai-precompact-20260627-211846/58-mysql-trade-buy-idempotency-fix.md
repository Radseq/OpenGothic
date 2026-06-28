# MySQL Trade Buy Idempotency Fix

Fixes `mmo_trade_buy_from_npc(...)` idempotent retry output.

The original Step 11 procedure accepted the buy transaction correctly, but the retry path returned
`NULL` for `p_bag_index` because it did not reload the accepted bag index from the audit payload.
The mutation itself was not duplicated; the bug was in the procedure OUT contract.

Implemented behavior:

```text
first buy -> event id, wallet_after, bag_index
same idempotency key retry -> same event id, same wallet_after, same bag_index
```

Only `011_trade_economy_write_path.sql` needs to be reapplied. The table shape is unchanged.
