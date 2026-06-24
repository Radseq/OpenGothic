param(
  [Parameter(Mandatory=$true)][string]$Baseline,
  [Parameter(Mandatory=$true)][string]$Snapshot,
  [switch]$Full,
  [switch]$Details,
  [int]$DetailLimit = 12
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
    $rows[[string]$obj.stable_key] = [pscustomobject]@{
      Raw = $line
      Obj = $obj
    }
  }
  return $rows
}

function Get-IgnoredFields {
  param([string]$Name)

  if ($Full) {
    return @()
  }

  switch ($Name) {
    "npcs.jsonl" {
      return @("slot_id", "pos", "rotation", "waypoint")
    }
    "npc_inventory.jsonl" {
      return @("owner_slot_id")
    }
    "npc_stats.jsonl" {
      return @("owner_slot_id", "aivar", "damage")
    }
    "items.jsonl" {
      return @("slot_id")
    }
    "mobsi.jsonl" {
      return @("slot_id", "pos", "display_pos", "pos_scheme")
    }
    "mobsi_inventory.jsonl" {
      return @("owner_slot_id")
    }
    default {
      return @()
    }
  }
}

function ConvertTo-ComparableJson {
  param(
    [object]$Obj,
    [string[]]$Ignore
  )

  $copy = [ordered]@{}
  foreach ($prop in $Obj.PSObject.Properties | Sort-Object Name) {
    if ($Ignore -contains $prop.Name) {
      continue
    }
    $copy[$prop.Name] = $prop.Value
  }
  return ($copy | ConvertTo-Json -Compress -Depth 8)
}

function Get-ChangedFields {
  param(
    [object]$Base,
    [object]$Snap,
    [string[]]$Ignore
  )

  $fields = @()
  foreach ($prop in $Snap.PSObject.Properties | Sort-Object Name) {
    if ($Ignore -contains $prop.Name) {
      continue
    }
    $name = $prop.Name
    $baseProp = $Base.PSObject.Properties[$name]
    if ($null -eq $baseProp) {
      $fields += $name
      continue
    }
    $a = $baseProp.Value | ConvertTo-Json -Compress -Depth 8
    $b = $prop.Value | ConvertTo-Json -Compress -Depth 8
    if ($a -ne $b) {
      $fields += $name
    }
  }
  return $fields
}

function Compare-DumpFile {
  param(
    [string]$Name,
    [string]$BaselineDir,
    [string]$SnapshotDir
  )

  $base = Read-JsonLinesByKey (Join-Path $BaselineDir $Name)
  $snap = Read-JsonLinesByKey (Join-Path $SnapshotDir $Name)
  $ignore = Get-IgnoredFields $Name

  $added = @()
  $removed = @()
  $changed = @()
  $examples = @()

  foreach ($key in $snap.Keys) {
    if (!$base.ContainsKey($key)) {
      $added += $key
    } elseif ((ConvertTo-ComparableJson $base[$key].Obj $ignore) -ne (ConvertTo-ComparableJson $snap[$key].Obj $ignore)) {
      $changed += $key
      if ($Details -and $examples.Count -lt $DetailLimit) {
        $examples += [pscustomobject]@{
          file = $Name
          stable_key = $key
          type = $snap[$key].Obj.type
          name = $snap[$key].Obj.display_name
          changed_fields = (Get-ChangedFields $base[$key].Obj $snap[$key].Obj $ignore) -join ","
        }
      }
    }
  }

  foreach ($key in $base.Keys) {
    if (!$snap.ContainsKey($key)) {
      $removed += $key
    }
  }

  return [pscustomobject]@{
    file = $Name
    baseline_rows = $base.Count
    snapshot_rows = $snap.Count
    added = $added.Count
    removed = $removed.Count
    changed = $changed.Count
    examples = $examples
  }
}

$files = @(
  "npcs.jsonl",
  "npc_stats.jsonl",
  "items.jsonl",
  "npc_inventory.jsonl",
  "mobsi.jsonl",
  "mobsi_inventory.jsonl",
  "quests.jsonl",
  "known_dialogs.jsonl",
  "script_globals.jsonl"
)

$summary = foreach ($file in $files) {
  Compare-DumpFile $file $Baseline $Snapshot
}

$summary | Select-Object file,baseline_rows,snapshot_rows,added,removed,changed | Format-Table -AutoSize

if ($Details) {
  $summary | ForEach-Object { $_.examples } | Where-Object { $null -ne $_ } | Format-Table -AutoSize
}
