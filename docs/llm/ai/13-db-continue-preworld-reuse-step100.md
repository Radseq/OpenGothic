# 13 DB Continue Pre-World Reuse Step100

Purpose: avoid downloading the same DB save checkpoint bootstrap twice during
`.sav`-free DB Continue.

Step99 introduced a pre-world bootstrap request so the client can read
`world_name` from the latest DB save checkpoint before constructing the baseline
ZEN world. The first test proved the path works, but it also showed two
bootstrap requests:

1. pre-world request with no loaded world yet;
2. regular `GameSession(new/pre-start)` request after the baseline world was
   constructed.

Step100 reuses the pre-world snapshot when it is already downloaded and valid:

- active only for `-mmo-client-server` + `-mmo-db-continue-without-native-save`;
- the existing snapshot must validate as `mmo_bootstrap_snapshot_v1`;
- `snapshot_source` must be `db_save_checkpoint_v1`;
- `GameSession` schedules restore without deleting the file;
- the regular New Game bootstrap request is skipped for this path only.

Expected client evidence:

```text
MMO DB continue pre-world snapshot selected world=...
MMO DB continue pre-world snapshot reuse enabled ...
MMO server snapshot restore scheduled ... reuse_existing_snapshot=1 ...
MMO server snapshot restore reusing pre-world DB continue snapshot
```

Expected server evidence:

```text
bootstrap_ack ... world=
[bootstrap_db_save_checkpoint_restore] bytes=...
bootstrap_snapshot_sent id=1 ...
```

There should not be a second immediate `client_bootstrap_request` for
`world=newworld.zen` during the same DB Continue startup.
