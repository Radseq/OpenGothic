param(
  [Parameter(Mandatory=$true)][string]$Baseline,
  [Parameter(Mandatory=$true)][string]$Snapshot,
  [string]$Output,
  [int]$Limit = 0,
  [switch]$IncludeAmbient
)

$ErrorActionPreference = "Stop"

if (!(Test-Path -LiteralPath $Baseline -PathType Container)) {
  throw "Baseline directory not found: $Baseline"
}
if (!(Test-Path -LiteralPath $Snapshot -PathType Container)) {
  throw "Snapshot directory not found: $Snapshot"
}

function Read-JsonLinesByKey {
  param([string]$Path)

  $rows = @{}
  if (!(Test-Path -LiteralPath $Path)) {
    return $rows
  }

  foreach ($line in Get-Content -LiteralPath $Path) {
    if ([string]::IsNullOrWhiteSpace($line)) {
      continue
    }
    $obj = $line | ConvertFrom-Json
    if ($null -eq $obj.stable_key) {
      continue
    }
    $rows[[string]$obj.stable_key] = $obj
  }
  return $rows
}

function Read-Set {
  param([string]$FileName)

  return [pscustomobject]@{
    Base = Read-JsonLinesByKey (Join-Path $Baseline $FileName)
    Snap = Read-JsonLinesByKey (Join-Path $Snapshot $FileName)
  }
}

function JsonValue {
  param([object]$Value)

  if ($null -eq $Value) {
    return $null
  }
  return $Value
}

function Same-Json {
  param([object]$A, [object]$B)

  $left = $A | ConvertTo-Json -Compress -Depth 12
  $right = $B | ConvertTo-Json -Compress -Depth 12
  return ($left -eq $right)
}

function Changed-Fields {
  param(
    [object]$Base,
    [object]$Snap,
    [string[]]$Fields
  )

  $ret = [System.Collections.Generic.List[string]]::new()
  foreach ($field in $Fields) {
    $baseProp = $Base.PSObject.Properties[$field]
    $snapProp = $Snap.PSObject.Properties[$field]
    $baseValue = if ($null -eq $baseProp) { $null } else { $baseProp.Value }
    $snapValue = if ($null -eq $snapProp) { $null } else { $snapProp.Value }
    if (!(Same-Json $baseValue $snapValue)) {
      $ret.Add($field) | Out-Null
    }
  }
  return $ret.ToArray()
}

function Add-Event {
  param([object]$Event)

  if ($script:Limit -gt 0 -and $script:Events.Count -ge $script:Limit) {
    return
  }
  $script:Events.Add($Event) | Out-Null
}

function Row-Summary {
  param([object]$Row)

  if ($null -eq $Row) {
    return $null
  }

  $ret = [ordered]@{}
  foreach ($name in @(
    "stable_key",
    "display_name",
    "name",
    "owner_display_name",
    "owner_focus_name",
    "symbol_name",
    "info_symbol_name",
    "npc_symbol_name",
    "symbol_index",
    "category",
    "amount",
    "iterator_count",
    "equipped",
    "slot",
    "visual",
    "value",
    "flags",
    "material"
  )) {
    $prop = $Row.PSObject.Properties[$name]
    if ($null -ne $prop) {
      $ret[$name] = $prop.Value
    }
  }
  return $ret
}

function Emit-AddedRemoved {
  param(
    [string]$Kind,
    [hashtable]$Base,
    [hashtable]$Snap,
    [scriptblock]$ProjectAdded,
    [scriptblock]$ProjectRemoved
  )

  foreach ($key in $Snap.Keys) {
    if (!$Base.ContainsKey($key)) {
      Add-Event (& $ProjectAdded $Snap[$key])
    }
  }
  foreach ($key in $Base.Keys) {
    if (!$Snap.ContainsKey($key)) {
      Add-Event (& $ProjectRemoved $Base[$key])
    }
  }
}

$script:Events = [System.Collections.Generic.List[object]]::new()

$npcs = Read-Set "npcs.jsonl"
$npcStats = Read-Set "npc_stats.jsonl"
$items = Read-Set "items.jsonl"
$npcInv = Read-Set "npc_inventory.jsonl"
$mobsi = Read-Set "mobsi.jsonl"
$mobsiInv = Read-Set "mobsi_inventory.jsonl"
$quests = Read-Set "quests.jsonl"
$knownDialogs = Read-Set "known_dialogs.jsonl"
$scriptGlobals = Read-Set "script_globals.jsonl"

$changedNpcIds = @{}
foreach ($key in $npcs.Snap.Keys) {
  if (!$npcs.Base.ContainsKey($key)) {
    continue
  }
  $base = $npcs.Base[$key]
  $snap = $npcs.Snap[$key]
  $fields = @(Changed-Fields $base $snap @("guild", "true_guild", "hp", "hp_max", "mana", "mana_max", "level", "dead", "player"))
  if ($fields.Count -eq 0) {
    continue
  }

  $changedNpcIds[[string]$snap.persistent_id] = $true
  $eventType = if ($base.dead -ne $true -and $snap.dead -eq $true) { "npc_killed" } else { "npc_changed" }
  Add-Event ([ordered]@{
    event_type = $eventType
    entity_type = "npc"
    stable_key = $key
    name = $snap.display_name
    persistent_id = $snap.persistent_id
    changed_fields = $fields
    before = [ordered]@{ hp = $base.hp; mana = $base.mana; dead = $base.dead; guild = $base.guild; true_guild = $base.true_guild }
    after = [ordered]@{ hp = $snap.hp; mana = $snap.mana; dead = $snap.dead; guild = $snap.guild; true_guild = $snap.true_guild }
  })
}

$statFields = @("level", "experience", "experience_next", "learning_points", "attributes", "protection", "damage", "hit_chance", "talent_skill", "talent_value", "mission", "aivar")
foreach ($key in $npcStats.Snap.Keys) {
  if (!$npcStats.Base.ContainsKey($key)) {
    continue
  }
  $base = $npcStats.Base[$key]
  $snap = $npcStats.Snap[$key]
  $fields = @(Changed-Fields $base $snap $statFields)
  if ($fields.Count -eq 0) {
    continue
  }
  if ($snap.player -ne $true -and !$IncludeAmbient -and !$changedNpcIds.ContainsKey([string]$snap.owner_persistent_id)) {
    continue
  }

  Add-Event ([ordered]@{
    event_type = if ($snap.player -eq $true) { "player_stats_changed" } else { "npc_stats_changed" }
    entity_type = "npc_stats"
    stable_key = $key
    name = $snap.owner_display_name
    persistent_id = $snap.owner_persistent_id
    changed_fields = $fields
    before = [ordered]@{
      level = $base.level
      experience = $base.experience
      learning_points = $base.learning_points
      attributes = JsonValue $base.attributes
      talent_skill = JsonValue $base.talent_skill
    }
    after = [ordered]@{
      level = $snap.level
      experience = $snap.experience
      learning_points = $snap.learning_points
      attributes = JsonValue $snap.attributes
      talent_skill = JsonValue $snap.talent_skill
    }
  })
}

Emit-AddedRemoved "world_item" $items.Base $items.Snap `
  { param($row) [ordered]@{ event_type = "world_item_added"; entity_type = "item"; stable_key = $row.stable_key; item = Row-Summary $row } } `
  { param($row) [ordered]@{ event_type = "world_item_removed"; entity_type = "item"; stable_key = $row.stable_key; item = Row-Summary $row } }

foreach ($key in $npcInv.Snap.Keys) {
  if (!$npcInv.Base.ContainsKey($key)) {
    $row = $npcInv.Snap[$key]
    if ($row.owner_display_name -eq "Ja") {
      Add-Event ([ordered]@{ event_type = "player_item_added"; entity_type = "npc_inventory"; stable_key = $key; item = Row-Summary $row })
    } elseif ($IncludeAmbient -or $changedNpcIds.ContainsKey([string]$row.owner_persistent_id)) {
      Add-Event ([ordered]@{ event_type = "npc_inventory_added"; entity_type = "npc_inventory"; stable_key = $key; owner_persistent_id = $row.owner_persistent_id; item = Row-Summary $row })
    }
  }
}

foreach ($key in $npcInv.Base.Keys) {
  if (!$npcInv.Snap.ContainsKey($key)) {
    $row = $npcInv.Base[$key]
    if ($row.owner_display_name -eq "Ja") {
      Add-Event ([ordered]@{ event_type = "player_item_removed"; entity_type = "npc_inventory"; stable_key = $key; item = Row-Summary $row })
    } elseif ($IncludeAmbient -or $changedNpcIds.ContainsKey([string]$row.owner_persistent_id)) {
      Add-Event ([ordered]@{ event_type = "npc_inventory_removed"; entity_type = "npc_inventory"; stable_key = $key; owner_persistent_id = $row.owner_persistent_id; item = Row-Summary $row })
    }
  }
}

foreach ($key in $npcInv.Snap.Keys) {
  if (!$npcInv.Base.ContainsKey($key)) {
    continue
  }
  $base = $npcInv.Base[$key]
  $snap = $npcInv.Snap[$key]
  $fields = @(Changed-Fields $base $snap @("amount", "iterator_count", "equipped", "slot"))
  if ($fields.Count -eq 0) {
    continue
  }
  Add-Event ([ordered]@{
    event_type = if ($snap.owner_display_name -eq "Ja") { "player_item_changed" } else { "npc_inventory_changed" }
    entity_type = "npc_inventory"
    stable_key = $key
    owner = $snap.owner_display_name
    changed_fields = $fields
    before = Row-Summary $base
    after = Row-Summary $snap
  })
}

foreach ($key in $mobsi.Snap.Keys) {
  if (!$mobsi.Base.ContainsKey($key)) {
    continue
  }
  $base = $mobsi.Base[$key]
  $snap = $mobsi.Snap[$key]
  $fields = @(Changed-Fields $base $snap @("state", "state_count", "state_mask", "locked", "cracked", "key_instance", "pick_lock_code"))
  if ($fields.Count -eq 0) {
    continue
  }
  if (!$IncludeAmbient -and $fields.Count -eq 1 -and $fields[0] -eq "state" -and $base.state -eq -1) {
    continue
  }
  Add-Event ([ordered]@{
    event_type = "mobsi_changed"
    entity_type = "mobsi"
    stable_key = $key
    name = $snap.display_name
    focus = $snap.focus_name
    vob_id = $snap.vob_id
    changed_fields = $fields
    before = [ordered]@{ state = $base.state; locked = $base.locked; cracked = $base.cracked }
    after = [ordered]@{ state = $snap.state; locked = $snap.locked; cracked = $snap.cracked }
  })
}

Emit-AddedRemoved "mobsi_inventory" $mobsiInv.Base $mobsiInv.Snap `
  { param($row) [ordered]@{ event_type = "container_item_added"; entity_type = "mobsi_inventory"; stable_key = $row.stable_key; owner_focus_name = $row.owner_focus_name; item = Row-Summary $row } } `
  { param($row) [ordered]@{ event_type = "container_item_removed"; entity_type = "mobsi_inventory"; stable_key = $row.stable_key; owner_focus_name = $row.owner_focus_name; item = Row-Summary $row } }

Emit-AddedRemoved "quest" $quests.Base $quests.Snap `
  { param($row) [ordered]@{ event_type = "quest_added"; entity_type = "quest"; stable_key = $row.stable_key; name = $row.name; status = $row.status; entry_count = $row.entry_count; entries = JsonValue $row.entries } } `
  { param($row) [ordered]@{ event_type = "quest_removed"; entity_type = "quest"; stable_key = $row.stable_key; name = $row.name; status = $row.status; entry_count = $row.entry_count; entries = JsonValue $row.entries } }

foreach ($key in $quests.Snap.Keys) {
  if (!$quests.Base.ContainsKey($key)) {
    continue
  }
  $base = $quests.Base[$key]
  $snap = $quests.Snap[$key]
  $fields = @(Changed-Fields $base $snap @("section", "status", "entry_count", "entries"))
  if ($fields.Count -gt 0) {
    Add-Event ([ordered]@{ event_type = "quest_changed"; entity_type = "quest"; stable_key = $key; name = $snap.name; changed_fields = $fields; before = Row-Summary $base; after = Row-Summary $snap })
  }
}

Emit-AddedRemoved "known_dialog" $knownDialogs.Base $knownDialogs.Snap `
  { param($row) [ordered]@{ event_type = "known_dialog_added"; entity_type = "known_dialog"; stable_key = $row.stable_key; npc_symbol_name = $row.npc_symbol_name; info_symbol_name = $row.info_symbol_name } } `
  { param($row) [ordered]@{ event_type = "known_dialog_removed"; entity_type = "known_dialog"; stable_key = $row.stable_key; npc_symbol_name = $row.npc_symbol_name; info_symbol_name = $row.info_symbol_name } }

Emit-AddedRemoved "script_global" $scriptGlobals.Base $scriptGlobals.Snap `
  { param($row) [ordered]@{ event_type = "script_global_added"; entity_type = "script_global"; stable_key = $row.stable_key; category = $row.category; symbol_name = $row.symbol_name; values = JsonValue $row.values } } `
  { param($row) [ordered]@{ event_type = "script_global_removed"; entity_type = "script_global"; stable_key = $row.stable_key; category = $row.category; symbol_name = $row.symbol_name; values = JsonValue $row.values } }

foreach ($key in $scriptGlobals.Snap.Keys) {
  if (!$scriptGlobals.Base.ContainsKey($key)) {
    continue
  }
  $base = $scriptGlobals.Base[$key]
  $snap = $scriptGlobals.Snap[$key]
  $fields = @(Changed-Fields $base $snap @("values"))
  if ($fields.Count -gt 0) {
    Add-Event ([ordered]@{
      event_type = "script_global_changed"
      entity_type = "script_global"
      stable_key = $key
      category = $snap.category
      symbol_name = $snap.symbol_name
      before = JsonValue $base.values
      after = JsonValue $snap.values
    })
  }
}

if (![string]::IsNullOrWhiteSpace($Output)) {
  $parent = Split-Path -Parent $Output
  if (![string]::IsNullOrWhiteSpace($parent)) {
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
  }
  $writer = [System.IO.StreamWriter]::new($Output, $false, [System.Text.UTF8Encoding]::new($false))
  try {
    foreach ($event in $script:Events) {
      $writer.WriteLine(($event | ConvertTo-Json -Compress -Depth 16))
    }
  } finally {
    $writer.Dispose()
  }
  Write-Host "Wrote $($script:Events.Count) events to: $Output"
} else {
  foreach ($event in $script:Events) {
    $event | ConvertTo-Json -Compress -Depth 16
  }
}
