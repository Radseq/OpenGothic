param(
  [Parameter(Mandatory=$true)][string]$Baseline,
  [Parameter(Mandatory=$true)][string]$Snapshot,
  [int]$Limit = 30,
  [switch]$ShowAmbientNpcStats,
  [switch]$ShowAmbientNpcInventory,
  [switch]$ShowMobsiInit
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

function Get-FieldValue {
  param(
    [object]$Obj,
    [string]$Name
  )

  $prop = $Obj.PSObject.Properties[$Name]
  if ($null -eq $prop) {
    return $null
  }
  return $prop.Value
}

function Compare-Value {
  param(
    [object]$A,
    [object]$B
  )

  $left = $A | ConvertTo-Json -Compress -Depth 8
  $right = $B | ConvertTo-Json -Compress -Depth 8
  return ($left -eq $right)
}

function Get-ChangedFields {
  param(
    [object]$Base,
    [object]$Snap,
    [string[]]$Ignore = @()
  )

  $fields = @()
  foreach ($prop in $Snap.PSObject.Properties | Sort-Object Name) {
    if ($Ignore -contains $prop.Name) {
      continue
    }

    $baseValue = Get-FieldValue $Base $prop.Name
    if (!(Compare-Value $baseValue $prop.Value)) {
      $fields += $prop.Name
    }
  }
  return $fields
}

function Read-Set {
  param([string]$FileName)

  return [pscustomobject]@{
    Base = Read-JsonLinesByKey (Join-Path $Baseline $FileName)
    Snap = Read-JsonLinesByKey (Join-Path $Snapshot $FileName)
  }
}

function Get-Added {
  param([hashtable]$Base, [hashtable]$Snap)

  $rows = [System.Collections.Generic.List[object]]::new()
  foreach ($key in $Snap.Keys) {
    if (!$Base.ContainsKey($key)) {
      $rows.Add($Snap[$key]) | Out-Null
    }
  }
  return $rows.ToArray()
}

function Get-Removed {
  param([hashtable]$Base, [hashtable]$Snap)

  $rows = [System.Collections.Generic.List[object]]::new()
  foreach ($key in $Base.Keys) {
    if (!$Snap.ContainsKey($key)) {
      $rows.Add($Base[$key]) | Out-Null
    }
  }
  return $rows.ToArray()
}

function Get-Changed {
  param(
    [hashtable]$Base,
    [hashtable]$Snap,
    [string[]]$Ignore = @()
  )

  $rows = [System.Collections.Generic.List[object]]::new()
  foreach ($key in $Snap.Keys) {
    if (!$Base.ContainsKey($key)) {
      continue
    }

    $fields = Get-ChangedFields $Base[$key] $Snap[$key] $Ignore
    if ($fields.Count -gt 0) {
      $rows.Add([pscustomobject]@{
        base = $Base[$key]
        snap = $Snap[$key]
        fields = $fields
      }) | Out-Null
    }
  }
  return $rows.ToArray()
}

function Get-ChangedFieldsByName {
  param(
    [object]$Base,
    [object]$Snap,
    [string[]]$Names
  )

  $fields = [System.Collections.Generic.List[string]]::new()
  foreach ($name in $Names) {
    $baseValue = Get-FieldValue $Base $name
    $snapValue = Get-FieldValue $Snap $name
    if (!(Compare-Value $baseValue $snapValue)) {
      $fields.Add($name) | Out-Null
    }
  }
  return $fields.ToArray()
}

function Get-ChangedByFields {
  param(
    [hashtable]$Base,
    [hashtable]$Snap,
    [string[]]$Fields,
    [scriptblock]$Where = { param($row) $true }
  )

  $rows = [System.Collections.Generic.List[object]]::new()
  foreach ($key in $Snap.Keys) {
    if (!$Base.ContainsKey($key)) {
      continue
    }

    $snapRow = $Snap[$key]
    if (!(& $Where $snapRow)) {
      continue
    }

    $changedFields = Get-ChangedFieldsByName $Base[$key] $snapRow $Fields
    if ($changedFields.Count -gt 0) {
      $rows.Add([pscustomobject]@{
        base = $Base[$key]
        snap = $snapRow
        fields = $changedFields
      }) | Out-Null
    }
  }
  return $rows.ToArray()
}

function Show-Section {
  param(
    [string]$Title,
    [object[]]$Rows
  )

  Write-Host ""
  Write-Host $Title
  if ($Rows.Count -eq 0) {
    Write-Host "  (none)"
    return
  }
  $Rows | Select-Object -First $Limit | Format-Table -AutoSize
}

function Show-Count {
  param(
    [string]$Title,
    [int]$Count,
    [string]$Hint
  )

  Write-Host ""
  Write-Host $Title
  if ($Count -eq 0) {
    Write-Host "  (none)"
    return
  }
  Write-Host "  $Count"
  if (![string]::IsNullOrWhiteSpace($Hint)) {
    Write-Host "  $Hint"
  }
}

function Format-JsonValue {
  param([object]$Value)

  if ($null -eq $Value) {
    return "null"
  }

  $json = $Value | ConvertTo-Json -Compress -Depth 8
  if ($json.Length -gt 160) {
    return $json.Substring(0, 157) + "..."
  }
  return $json
}

$npcs = Read-Set "npcs.jsonl"
$npcStats = Read-Set "npc_stats.jsonl"
$items = Read-Set "items.jsonl"
$npcInv = Read-Set "npc_inventory.jsonl"
$mobsi = Read-Set "mobsi.jsonl"
$mobsiInv = Read-Set "mobsi_inventory.jsonl"
$quests = Read-Set "quests.jsonl"
$knownDialogs = Read-Set "known_dialogs.jsonl"
$scriptGlobals = Read-Set "script_globals.jsonl"

$npcChanges = Get-ChangedByFields $npcs.Base $npcs.Snap @("guild", "true_guild", "hp", "hp_max", "mana", "mana_max", "level", "dead", "player") | ForEach-Object {
  [pscustomobject]@{
    name = $_.snap.display_name
    persistent_id = $_.snap.persistent_id
    changed = ($_.fields -join ",")
    hp_before = $_.base.hp
    hp_after = $_.snap.hp
    dead_before = $_.base.dead
    dead_after = $_.snap.dead
    mana_before = $_.base.mana
    mana_after = $_.snap.mana
  }
}
$changedNpcPersistentIds = @{}
foreach ($row in $npcChanges) {
  $changedNpcPersistentIds[[string]$row.persistent_id] = $true
}

$npcStatsFields = @(
  "level",
  "experience",
  "experience_next",
  "learning_points",
  "attributes",
  "protection",
  "damage",
  "hit_chance",
  "talent_skill",
  "talent_value",
  "mission",
  "aivar"
)

$playerStatsChanged = Get-ChangedByFields $npcStats.Base $npcStats.Snap $npcStatsFields { param($row) $row.player -eq $true } | ForEach-Object {
  [pscustomobject]@{
    name = $_.snap.owner_display_name
    changed = ($_.fields -join ",")
    level_before = $_.base.level
    level_after = $_.snap.level
    exp_before = $_.base.experience
    exp_after = $_.snap.experience
    lp_before = $_.base.learning_points
    lp_after = $_.snap.learning_points
    attributes_before = Format-JsonValue $_.base.attributes
    attributes_after = Format-JsonValue $_.snap.attributes
    talents_before = Format-JsonValue $_.base.talent_skill
    talents_after = Format-JsonValue $_.snap.talent_skill
  }
}

$changedNpcStatsAll = Get-ChangedByFields $npcStats.Base $npcStats.Snap $npcStatsFields { param($row) $row.player -ne $true } | ForEach-Object {
  [pscustomobject]@{
    name = $_.snap.owner_display_name
    persistent_id = $_.snap.owner_persistent_id
    changed = ($_.fields -join ",")
    level_before = $_.base.level
    level_after = $_.snap.level
    exp_before = $_.base.experience
    exp_after = $_.snap.experience
    lp_before = $_.base.learning_points
    lp_after = $_.snap.learning_points
  }
}
$changedNpcStats = $changedNpcStatsAll | Where-Object { $changedNpcPersistentIds.ContainsKey([string]$_.persistent_id) }
$ambientNpcStats = $changedNpcStatsAll | Where-Object { !$changedNpcPersistentIds.ContainsKey([string]$_.persistent_id) }

$playerAdded = Get-Added $npcInv.Base $npcInv.Snap | Where-Object { $_.owner_display_name -eq "Ja" } | Sort-Object display_name
$playerRemoved = Get-Removed $npcInv.Base $npcInv.Snap | Where-Object { $_.owner_display_name -eq "Ja" } | Sort-Object display_name
$playerChanged = Get-ChangedByFields $npcInv.Base $npcInv.Snap @("amount", "iterator_count", "equipped", "slot") { param($row) $row.owner_display_name -eq "Ja" } | ForEach-Object {
  [pscustomobject]@{
    item = $_.snap.display_name
    changed = ($_.fields -join ",")
    amount_before = $_.base.amount
    amount_after = $_.snap.amount
    count_before = $_.base.iterator_count
    count_after = $_.snap.iterator_count
    equipped_before = $_.base.equipped
    equipped_after = $_.snap.equipped
  }
}

$worldItemsRemoved = Get-Removed $items.Base $items.Snap | Sort-Object display_name
$worldItemsAdded = Get-Added $items.Base $items.Snap | Sort-Object display_name
$otherNpcInvAddedAll = Get-Added $npcInv.Base $npcInv.Snap | Where-Object { $_.owner_display_name -ne "Ja" } | Sort-Object owner_display_name,display_name
$otherNpcInvRemovedAll = Get-Removed $npcInv.Base $npcInv.Snap | Where-Object { $_.owner_display_name -ne "Ja" } | Sort-Object owner_display_name,display_name
$changedNpcInvAdded = $otherNpcInvAddedAll | Where-Object { $changedNpcPersistentIds.ContainsKey([string]$_.owner_persistent_id) }
$changedNpcInvRemoved = $otherNpcInvRemovedAll | Where-Object { $changedNpcPersistentIds.ContainsKey([string]$_.owner_persistent_id) }
$ambientNpcInvAdded = $otherNpcInvAddedAll | Where-Object { !$changedNpcPersistentIds.ContainsKey([string]$_.owner_persistent_id) }
$ambientNpcInvRemoved = $otherNpcInvRemovedAll | Where-Object { !$changedNpcPersistentIds.ContainsKey([string]$_.owner_persistent_id) }

$mobsiChanges = Get-ChangedByFields $mobsi.Base $mobsi.Snap @("state", "state_count", "state_mask", "locked", "cracked", "key_instance", "pick_lock_code") | ForEach-Object {
  $changedFields = @($_.fields)
  [pscustomobject]@{
    name = $_.snap.display_name
    focus = $_.snap.focus_name
    vob_id = $_.snap.vob_id
    changed = ($changedFields -join ",")
    state_before = $_.base.state
    state_after = $_.snap.state
    container = $_.snap.container
    door = $_.snap.door
    init_only = ($changedFields.Count -eq 1 -and $changedFields[0] -eq "state" -and $_.base.state -eq -1)
  }
}
$mobsiInitChanges = $mobsiChanges | Where-Object { $_.init_only }
$mobsiMeaningfulChanges = $mobsiChanges | Where-Object { !$_.init_only }

$mobsiInvRemoved = Get-Removed $mobsiInv.Base $mobsiInv.Snap | Sort-Object owner_focus_name,owner_tag,display_name
$mobsiInvAdded = Get-Added $mobsiInv.Base $mobsiInv.Snap | Sort-Object owner_focus_name,owner_tag,display_name
$questAdded = Get-Added $quests.Base $quests.Snap | Sort-Object name
$questRemoved = Get-Removed $quests.Base $quests.Snap | Sort-Object name
$questChanged = Get-ChangedByFields $quests.Base $quests.Snap @("section", "status", "entry_count") | ForEach-Object {
  [pscustomobject]@{
    name = $_.snap.name
    changed = ($_.fields -join ",")
    status_before = $_.base.status
    status_after = $_.snap.status
    entries_before = $_.base.entry_count
    entries_after = $_.snap.entry_count
  }
}
$knownDialogAdded = Get-Added $knownDialogs.Base $knownDialogs.Snap | Sort-Object npc_symbol_name,info_symbol_name
$knownDialogRemoved = Get-Removed $knownDialogs.Base $knownDialogs.Snap | Sort-Object npc_symbol_name,info_symbol_name
$scriptGlobalAdded = Get-Added $scriptGlobals.Base $scriptGlobals.Snap | Sort-Object category,symbol_name | ForEach-Object {
  [pscustomobject]@{
    category = $_.category
    symbol_name = $_.symbol_name
    value_type = $_.value_type
    values = Format-JsonValue $_.values
  }
}
$scriptGlobalRemoved = Get-Removed $scriptGlobals.Base $scriptGlobals.Snap | Sort-Object category,symbol_name | ForEach-Object {
  [pscustomobject]@{
    category = $_.category
    symbol_name = $_.symbol_name
    value_type = $_.value_type
    values = Format-JsonValue $_.values
  }
}
$scriptGlobalChanged = Get-ChangedByFields $scriptGlobals.Base $scriptGlobals.Snap @("values") | Sort-Object { $_.snap.category }, { $_.snap.symbol_name } | ForEach-Object {
  [pscustomobject]@{
    category = $_.snap.category
    symbol_name = $_.snap.symbol_name
    value_type = $_.snap.value_type
    before = Format-JsonValue $_.base.values
    after = Format-JsonValue $_.snap.values
  }
}

Show-Section "NPC changes" $npcChanges
Show-Section "Player stats changed" $playerStatsChanged
Show-Section "Changed NPC stats" $changedNpcStats
if ($ShowAmbientNpcStats) {
  Show-Section "Ambient NPC stats changed" $ambientNpcStats
} else {
  Show-Count "Ambient NPC stats changed hidden" $ambientNpcStats.Count "Use -ShowAmbientNpcStats to print likely AI/runtime stat noise."
}
Show-Section "Player inventory added" ($playerAdded | Select-Object owner_display_name,display_name,amount,iterator_count,equipped,slot)
Show-Section "Player inventory removed" ($playerRemoved | Select-Object owner_display_name,display_name,amount,iterator_count,equipped,slot)
Show-Section "Player inventory changed" $playerChanged
Show-Section "World items removed" ($worldItemsRemoved | Select-Object display_name,name,amount,visual,stable_key)
Show-Section "World items added" ($worldItemsAdded | Select-Object display_name,name,amount,visual,stable_key)
Show-Section "Changed NPC inventory added" ($changedNpcInvAdded | Select-Object owner_display_name,display_name,amount,iterator_count,equipped,slot)
Show-Section "Changed NPC inventory removed" ($changedNpcInvRemoved | Select-Object owner_display_name,display_name,amount,iterator_count,equipped,slot)
if ($ShowAmbientNpcInventory) {
  Show-Section "Ambient NPC inventory added" ($ambientNpcInvAdded | Select-Object owner_display_name,display_name,amount,iterator_count,equipped,slot)
  Show-Section "Ambient NPC inventory removed" ($ambientNpcInvRemoved | Select-Object owner_display_name,display_name,amount,iterator_count,equipped,slot)
} else {
  Show-Count "Ambient NPC inventory added hidden" $ambientNpcInvAdded.Count "Use -ShowAmbientNpcInventory to print likely script/init noise."
  Show-Count "Ambient NPC inventory removed hidden" $ambientNpcInvRemoved.Count "Use -ShowAmbientNpcInventory to print likely script/init noise."
}
Show-Section "Mobsi meaningful state/lock changes" ($mobsiMeaningfulChanges | Select-Object name,focus,vob_id,changed,state_before,state_after,container,door)
if ($ShowMobsiInit) {
  Show-Section "Mobsi initialized state changes" ($mobsiInitChanges | Select-Object name,focus,vob_id,changed,state_before,state_after,container,door)
} else {
  Show-Count "Mobsi initialized state changes hidden" $mobsiInitChanges.Count "Use -ShowMobsiInit to print state-only -1 -> runtime value changes."
}
Show-Section "Mobsi inventory added" ($mobsiInvAdded | Select-Object owner_focus_name,owner_tag,display_name,amount,iterator_count)
Show-Section "Mobsi inventory removed" ($mobsiInvRemoved | Select-Object owner_focus_name,owner_tag,display_name,amount,iterator_count)
Show-Section "Quest added" ($questAdded | Select-Object name,section,status,entry_count)
Show-Section "Quest removed" ($questRemoved | Select-Object name,section,status,entry_count)
Show-Section "Quest changed" $questChanged
Show-Section "Known dialog added" ($knownDialogAdded | Select-Object npc_symbol_name,info_symbol_name,npc_symbol_index,info_symbol_index)
Show-Section "Known dialog removed" ($knownDialogRemoved | Select-Object npc_symbol_name,info_symbol_name,npc_symbol_index,info_symbol_index)
Show-Section "Script global added" $scriptGlobalAdded
Show-Section "Script global removed" $scriptGlobalRemoved
Show-Section "Script global changed" $scriptGlobalChanged
