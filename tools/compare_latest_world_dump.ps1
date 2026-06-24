param(
  [string]$WorldDir = "exports\g2notr\newworld.zen",
  [int]$Limit = 30,
  [switch]$Details,
  [switch]$ShowAmbientNpcStats,
  [switch]$ShowAmbientNpcInventory,
  [switch]$ShowMobsiInit,
  [switch]$WriteEvents,
  [string]$EventsOut,
  [switch]$ImportSqlite,
  [string]$SqliteOut,
  [switch]$BuildMmoDb,
  [string]$MmoDbOut
)

$ErrorActionPreference = "Stop"

if (!(Test-Path -LiteralPath $WorldDir -PathType Container)) {
  throw "World dump directory not found: $WorldDir"
}

$snapshotRoot = Join-Path $WorldDir "snapshots"
if (!(Test-Path -LiteralPath $snapshotRoot -PathType Container)) {
  throw "Snapshot directory not found: $snapshotRoot"
}

$snapshot = Get-ChildItem -LiteralPath $snapshotRoot -Directory |
  Sort-Object LastWriteTime -Descending |
  Select-Object -First 1

if ($null -eq $snapshot) {
  throw "No snapshots found in: $snapshotRoot"
}

Write-Host "Baseline: $WorldDir"
Write-Host "Snapshot: $($snapshot.FullName)"
Write-Host ""

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

$compareArgs = @(
  "-ExecutionPolicy", "Bypass",
  "-File", (Join-Path $scriptDir "compare_world_dumps.ps1"),
  "-Baseline", $WorldDir,
  "-Snapshot", $snapshot.FullName
)
if ($Details) {
  $compareArgs += "-Details"
}

& powershell @compareArgs

Write-Host ""
Write-Host "Event summary"

$summaryArgs = @(
  "-ExecutionPolicy", "Bypass",
  "-File", (Join-Path $scriptDir "summarize_world_events.ps1"),
  "-Baseline", $WorldDir,
  "-Snapshot", $snapshot.FullName,
  "-Limit", $Limit
)
if ($ShowAmbientNpcInventory) {
  $summaryArgs += "-ShowAmbientNpcInventory"
}
if ($ShowAmbientNpcStats) {
  $summaryArgs += "-ShowAmbientNpcStats"
}
if ($ShowMobsiInit) {
  $summaryArgs += "-ShowMobsiInit"
}

& powershell @summaryArgs

if ($WriteEvents -or ![string]::IsNullOrWhiteSpace($EventsOut)) {
  if ([string]::IsNullOrWhiteSpace($EventsOut)) {
    $EventsOut = Join-Path $snapshot.FullName "world_events.jsonl"
  }

  Write-Host ""
  Write-Host "Event JSONL"
  $eventArgs = @(
    "-ExecutionPolicy", "Bypass",
    "-File", (Join-Path $scriptDir "export_world_events.ps1"),
    "-Baseline", $WorldDir,
    "-Snapshot", $snapshot.FullName,
    "-Output", $EventsOut
  )
  if ($ShowAmbientNpcStats -or $ShowAmbientNpcInventory -or $ShowMobsiInit) {
    $eventArgs += "-IncludeAmbient"
  }
  & powershell @eventArgs
}

if ($ImportSqlite -or $BuildMmoDb -or ![string]::IsNullOrWhiteSpace($SqliteOut) -or ![string]::IsNullOrWhiteSpace($MmoDbOut)) {
  if ([string]::IsNullOrWhiteSpace($EventsOut)) {
    $EventsOut = Join-Path $snapshot.FullName "world_events.jsonl"
  }
  if (!(Test-Path -LiteralPath $EventsOut -PathType Leaf)) {
    Write-Host ""
    Write-Host "Event JSONL"
    $eventArgs = @(
      "-ExecutionPolicy", "Bypass",
      "-File", (Join-Path $scriptDir "export_world_events.ps1"),
      "-Baseline", $WorldDir,
      "-Snapshot", $snapshot.FullName,
      "-Output", $EventsOut
    )
    if ($ShowAmbientNpcStats -or $ShowAmbientNpcInventory -or $ShowMobsiInit) {
      $eventArgs += "-IncludeAmbient"
    }
    & powershell @eventArgs
  }

  if ([string]::IsNullOrWhiteSpace($SqliteOut)) {
    $SqliteOut = Join-Path $snapshot.FullName "world_staging.sqlite"
  }

  Write-Host ""
  Write-Host "SQLite staging import"
  & python (Join-Path $scriptDir "import_world_dump_sqlite.py") `
    --baseline $WorldDir `
    --snapshot $snapshot.FullName `
    --events $EventsOut `
    --db $SqliteOut `
    --reset
}

if ($BuildMmoDb -or ![string]::IsNullOrWhiteSpace($MmoDbOut)) {
  if ([string]::IsNullOrWhiteSpace($SqliteOut)) {
    $SqliteOut = Join-Path $snapshot.FullName "world_staging.sqlite"
  }
  if (!(Test-Path -LiteralPath $SqliteOut -PathType Leaf)) {
    throw "SQLite staging DB not found: $SqliteOut"
  }
  if ([string]::IsNullOrWhiteSpace($MmoDbOut)) {
    $MmoDbOut = Join-Path $snapshot.FullName "gothic_mmo.sqlite"
  }

  Write-Host ""
  Write-Host "Gothic MMO database build"
  & python (Join-Path $scriptDir "build_mmo_database.py") `
    --source $SqliteOut `
    --out $MmoDbOut `
    --reset
}
