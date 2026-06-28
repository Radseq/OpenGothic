# MySQL Event Replay Contract


## Replay contract wallet class fix

Migration `004_wallet_write_path.sql` emits `character_wallet_delta` as `event_class='inventory'`. The replay contract registry must match this journal class exactly, even though the deterministic projection target is `character_wallets`.
