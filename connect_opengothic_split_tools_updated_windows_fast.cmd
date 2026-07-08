@echo off
setlocal EnableExtensions

rem Fast Windows CMD launcher. The heavy file walking/writing is done by the
rem embedded PowerShell block because pure CMD is very slow on large trees.
set "__OG_SCRIPT=%~f0"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$s=[IO.File]::ReadAllText($env:__OG_SCRIPT); $m='# POWERSHELL_START'; $i=$s.LastIndexOf($m); if($i -lt 0){throw 'PowerShell marker not found'}; $ps=$s.Substring($i + $m.Length); & ([ScriptBlock]::Create($ps)) @args" %*
exit /b %ERRORLEVEL%

# POWERSHELL_START
param(
    [string]$CodeOutput = "",
    [string]$CodeServerOutput = "wynik_code_server.txt",
    [string]$CodeClientOutput = "wynik_code_client.txt",
    [string]$LlmOutput = "wynik_llm.txt",
    [string]$ToolsOutput = "wynik_tools.txt",
    [string]$SchemaOutput = $(if ($env:MYSQL_SCHEMA_OUTPUT) { $env:MYSQL_SCHEMA_OUTPUT } else { "wynik_gothic_mmo_ch1_clean_schema.txt" })
)

$ErrorActionPreference = "Stop"
$Utf8NoBom = New-Object System.Text.UTF8Encoding($false)

function Get-EnvOrDefault([string]$Name, [string]$Default) {
    $value = [Environment]::GetEnvironmentVariable($Name)
    if ([string]::IsNullOrWhiteSpace($value)) { return $Default }
    return $value
}

function Resolve-OutputPath([string]$Path) {
    if ([IO.Path]::IsPathRooted($Path)) {
        return [IO.Path]::GetFullPath($Path)
    }
    return [IO.Path]::GetFullPath((Join-Path (Get-Location) $Path))
}

function Find-ProjectRoot {
    $dir = (Get-Location).ProviderPath

    while ($true) {
        if ((Test-Path -LiteralPath (Join-Path $dir "game") -PathType Container) -and
            (Test-Path -LiteralPath (Join-Path $dir "docs\llm") -PathType Container)) {
            return $dir
        }

        $base = Split-Path -Leaf $dir
        $parent = Split-Path -Parent $dir

        if ($base -ieq "game" -and (Test-Path -LiteralPath (Join-Path $parent "docs\llm") -PathType Container)) {
            return $parent
        }

        if ($base -ieq "server" -and
            (Test-Path -LiteralPath (Join-Path $parent "game") -PathType Container) -and
            (Test-Path -LiteralPath (Join-Path $parent "docs\llm") -PathType Container)) {
            return $parent
        }

        if ($base -ieq "docs" -and
            (Test-Path -LiteralPath (Join-Path $dir "llm") -PathType Container) -and
            (Test-Path -LiteralPath (Join-Path $parent "game") -PathType Container)) {
            return $parent
        }

        if ($base -ieq "tools" -and
            (Test-Path -LiteralPath (Join-Path $parent "game") -PathType Container) -and
            (Test-Path -LiteralPath (Join-Path $parent "docs\llm") -PathType Container)) {
            return $parent
        }

        if ([string]::IsNullOrEmpty($parent) -or $parent -eq $dir) {
            throw "Cannot detect OpenGothic project root. Run from OpenGothic\, OpenGothic\game\, OpenGothic\server\, OpenGothic\docs\ or OpenGothic\tools\."
        }

        $dir = $parent
    }
}

$ProjectRoot = Find-ProjectRoot
$GameRoot = Join-Path $ProjectRoot "game"
$ServerRoot = Join-Path $ProjectRoot "server"
$LlmRoot = Join-Path $ProjectRoot "docs\llm"
$ToolsRoot = Join-Path $ProjectRoot "tools"

if (-not [string]::IsNullOrWhiteSpace($CodeOutput)) {
    $legacyCodeOutputAbs = Resolve-OutputPath $CodeOutput
    $legacyCodeOutputDir = Split-Path -Parent $legacyCodeOutputAbs
    $legacyCodeOutputBase = [IO.Path]::GetFileNameWithoutExtension($legacyCodeOutputAbs)
    $legacyCodeOutputExt = [IO.Path]::GetExtension($legacyCodeOutputAbs)
    $CodeServerOutput = Join-Path $legacyCodeOutputDir "${legacyCodeOutputBase}_server${legacyCodeOutputExt}"
    $CodeClientOutput = Join-Path $legacyCodeOutputDir "${legacyCodeOutputBase}_client${legacyCodeOutputExt}"
}

$CodeServerOutputAbs = Resolve-OutputPath $CodeServerOutput
$CodeClientOutputAbs = Resolve-OutputPath $CodeClientOutput
$LlmOutputAbs = Resolve-OutputPath $LlmOutput
$ToolsOutputAbs = Resolve-OutputPath $ToolsOutput
$SchemaOutputAbs = Resolve-OutputPath $SchemaOutput

$MaxBytes = [int64](Get-EnvOrDefault "MAX_BYTES" "1500000")
$IncludePrivateDocs = Get-EnvOrDefault "INCLUDE_PRIVATE_DOCS" "1"
$IncludePrivateTools = Get-EnvOrDefault "INCLUDE_PRIVATE_TOOLS" "1"

$OutputNames = @(
    [IO.Path]::GetFileName($CodeServerOutputAbs),
    [IO.Path]::GetFileName($CodeClientOutputAbs),
    [IO.Path]::GetFileName($LlmOutputAbs),
    [IO.Path]::GetFileName($ToolsOutputAbs),
    [IO.Path]::GetFileName($SchemaOutputAbs)
)

$OutputPaths = @(
    $CodeServerOutputAbs.ToLowerInvariant(),
    $CodeClientOutputAbs.ToLowerInvariant(),
    $LlmOutputAbs.ToLowerInvariant(),
    $ToolsOutputAbs.ToLowerInvariant(),
    $SchemaOutputAbs.ToLowerInvariant()
)

$BinaryOrArchiveExts = New-Object "System.Collections.Generic.HashSet[string]" ([StringComparer]::OrdinalIgnoreCase)
@(
    ".spv",".o",".obj",".a",".so",".dll",".lib",".exe",".pdb",".ilk",".exp",
    ".png",".jpg",".jpeg",".tga",".bmp",".hdr",".ktx",".ktx2",".dds",".webp",
    ".fbx",".gltf",".glb",".dae",".blend",".wav",".mp3",".ogg",".flac",
    ".zip",".7z",".tar",".gz",".rar"
) | ForEach-Object { [void]$BinaryOrArchiveExts.Add($_) }

function Test-SkipCommonPath([IO.FileInfo]$File) {
    $full = $File.FullName
    $lower = $full.ToLowerInvariant()
    $name = $File.Name

    if ($OutputPaths -contains $lower) { return $true }
    if ($OutputNames -icontains $name) { return $true }
    if ($name -ieq "wynik.txt" -or ($name -ilike "wynik_*.txt")) { return $true }
    if ($name -ieq "Gomol.log" -or $name -ieq "rvk_trace.json" -or $name -ieq "compile_commands.json") { return $true }

    foreach ($part in @("\build\","\out\","\obj\","\bin\","\external\","\third_party\","\vendor\","\.git\","\.vscode\","\.idea\","\.cache\","\Testing\","\test-results\")) {
        if ($lower.Contains($part.ToLowerInvariant())) { return $true }
    }

    if ($lower.Contains("\cmake-build-")) { return $true }
    if ($BinaryOrArchiveExts.Contains($File.Extension)) { return $true }
    return $false
}

function Test-PrivatePath([IO.FileInfo]$File) {
    $lower = $File.FullName.ToLowerInvariant()
    if ($lower.Contains("\private\") -or $lower.Contains("\secret\") -or $lower.Contains("\secrets\")) { return $true }
    if ($File.Name -ieq ".env" -or $File.Name -ilike ".env.*") { return $true }
    return $false
}

function Get-RelativePathCompat([string]$Root, [string]$Path) {
    $rootFull = [IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    $pathFull = [IO.Path]::GetFullPath($Path)
    if ($pathFull.StartsWith($rootFull, [StringComparison]::OrdinalIgnoreCase)) {
        return $pathFull.Substring($rootFull.Length)
    }
    return $pathFull
}

function New-Snapshot([string]$OutputPath, [string]$Title, [string[]]$Roots, [string[]]$Extensions, [bool]$IncludePrivate) {
    $extSet = New-Object "System.Collections.Generic.HashSet[string]" ([StringComparer]::OrdinalIgnoreCase)
    $Extensions | ForEach-Object { [void]$extSet.Add($_) }

    $writer = New-Object IO.StreamWriter($OutputPath, $false, $Utf8NoBom)
    try {
        $writer.WriteLine("# $Title")
        $writer.WriteLine("# Generated: $((Get-Date).ToString('s'))")
        $writer.WriteLine("# Project root: $ProjectRoot")
        $writer.WriteLine("# MAX_BYTES per file: $MaxBytes")
        $writer.WriteLine()

        foreach ($root in $Roots) {
            if (-not (Test-Path -LiteralPath $root -PathType Container)) {
                if ($root -eq $ServerRoot) {
                    Write-Warning "Missing server directory, skipping: $root"
                    continue
                }
                throw "Missing directory: $root"
            }

            Get-ChildItem -LiteralPath $root -Recurse -File -Force |
                Sort-Object FullName |
                Where-Object {
                    $extSet.Contains($_.Extension) -and
                    -not (Test-SkipCommonPath $_) -and
                    ($IncludePrivate -or -not (Test-PrivatePath $_))
                } |
                ForEach-Object {
                    $rel = Get-RelativePathCompat $ProjectRoot $_.FullName
                    $writer.WriteLine("===== $rel =====")

                    if ($_.Length -gt $MaxBytes) {
                        $writer.WriteLine("[SKIPPED: file is $($_.Length) bytes, MAX_BYTES=$MaxBytes]")
                        $writer.WriteLine()
                    } else {
                        $reader = New-Object IO.StreamReader($_.FullName, $true)
                        try {
                            $buffer = New-Object char[] 65536
                            while (($read = $reader.Read($buffer, 0, $buffer.Length)) -gt 0) {
                                $writer.Write($buffer, 0, $read)
                            }
                        } finally {
                            $reader.Dispose()
                        }
                    }

                    $writer.WriteLine()
                    $writer.WriteLine()
                }
        }
    } finally {
        $writer.Dispose()
    }
}

function Quote-Arg([string]$Arg) {
    if ($Arg -notmatch '[\s"]') { return $Arg }
    return '"' + ($Arg -replace '"', '\"') + '"'
}

function Invoke-NativeCapture([string]$Exe, [string[]]$NativeArgs, [string]$Password) {
    $stdoutPath = [IO.Path]::GetTempFileName()
    $stderrPath = [IO.Path]::GetTempFileName()
    $oldPassword = [Environment]::GetEnvironmentVariable("MYSQL_PWD")
    $hadOldPassword = $null -ne $oldPassword
    $oldErrorActionPreference = $ErrorActionPreference

    try {
        if ($Password) {
            [Environment]::SetEnvironmentVariable("MYSQL_PWD", $Password)
        } else {
            [Environment]::SetEnvironmentVariable("MYSQL_PWD", $null)
        }

        Write-Host "Running: $Exe $($NativeArgs -join ' ')"
        $ErrorActionPreference = "Continue"
        & $Exe @NativeArgs > $stdoutPath 2> $stderrPath
        $exitCode = $LASTEXITCODE

        return [pscustomobject]@{
            ExitCode = $exitCode
            StdOut = [IO.File]::ReadAllText($stdoutPath)
            StdErr = [IO.File]::ReadAllText($stderrPath)
            Command = "$Exe $($NativeArgs -join ' ')"
        }
    } finally {
        $ErrorActionPreference = $oldErrorActionPreference
        if ($hadOldPassword) {
            [Environment]::SetEnvironmentVariable("MYSQL_PWD", $oldPassword)
        } else {
            [Environment]::SetEnvironmentVariable("MYSQL_PWD", $null)
        }

        Remove-Item -LiteralPath $stdoutPath, $stderrPath -Force -ErrorAction SilentlyContinue
    }
}

function Resolve-MySqlTool([string]$ToolName, [string]$EnvName) {
    $explicit = [Environment]::GetEnvironmentVariable($EnvName)
    if (-not [string]::IsNullOrWhiteSpace($explicit)) {
        if (Test-Path -LiteralPath $explicit -PathType Leaf) {
            return [IO.Path]::GetFullPath($explicit)
        }
        throw "$EnvName is set, but file does not exist: $explicit"
    }

    $fromPath = Get-Command $ToolName -ErrorAction SilentlyContinue
    if ($fromPath) {
        return $fromPath.Source
    }

    $candidateDirs = New-Object "System.Collections.Generic.List[string]"
    foreach ($base in @($env:ProgramFiles, ${env:ProgramFiles(x86)}, $env:ProgramW6432)) {
        if ([string]::IsNullOrWhiteSpace($base)) { continue }
        [void]$candidateDirs.Add((Join-Path $base "MySQL\MySQL Server 8.0\bin"))
        [void]$candidateDirs.Add((Join-Path $base "MySQL\MySQL Server 8.4\bin"))
        [void]$candidateDirs.Add((Join-Path $base "MySQL\MySQL Workbench 8.0 CE"))
        [void]$candidateDirs.Add((Join-Path $base "MySQL\MySQL Workbench 8.0"))
        [void]$candidateDirs.Add((Join-Path $base "MariaDB 10.11\bin"))
        [void]$candidateDirs.Add((Join-Path $base "MariaDB 11.4\bin"))
    }

    foreach ($dir in $candidateDirs) {
        $candidate = Join-Path $dir $ToolName
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return [IO.Path]::GetFullPath($candidate)
        }
    }

    foreach ($base in @($env:ProgramFiles, ${env:ProgramFiles(x86)}, $env:ProgramW6432)) {
        if ([string]::IsNullOrWhiteSpace($base)) { continue }
        $mysqlRoot = Join-Path $base "MySQL"
        if (-not (Test-Path -LiteralPath $mysqlRoot -PathType Container)) { continue }

        $found = Get-ChildItem -LiteralPath $mysqlRoot -Recurse -Filter $ToolName -File -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($found) {
            return $found.FullName
        }
    }

    throw "$ToolName not found. Set $EnvName to the full exe path, for example: set $EnvName=C:\Program Files\MySQL\MySQL Server 8.0\bin\$ToolName"
}

function Export-MySqlSchema {
    $mysqldumpExe = Resolve-MySqlTool "mysqldump.exe" "MYSQLDUMP_EXE"
    $mysqlExe = Resolve-MySqlTool "mysql.exe" "MYSQL_EXE"

    $mysqlHost = Get-EnvOrDefault "MYSQL_HOST" "192.168.195.94"
    $mysqlPort = Get-EnvOrDefault "MYSQL_PORT" "3306"
    $mysqlUser = Get-EnvOrDefault "MYSQL_USER" "gothic"
    $mysqlPassword = Get-EnvOrDefault "MYSQL_PWD" "gothic_dev_password"
    $mysqlDatabase = Get-EnvOrDefault "MYSQL_DATABASE" "gothic_mmo_ch1_clean"
    $includeViews = Get-EnvOrDefault "MYSQL_SCHEMA_INCLUDE_VIEWS" "1"
    $tmpOutput = "$SchemaOutputAbs.tmp.$PID"

    Write-Host "MySQL schema export: host=$mysqlHost port=$mysqlPort user=$mysqlUser database=$mysqlDatabase include_views=$includeViews"
    Write-Host "mysql.exe: $mysqlExe"
    Write-Host "mysqldump.exe: $mysqldumpExe"

    $test = Invoke-NativeCapture $mysqlExe @(
        "--no-defaults",
        "--protocol=TCP",
        "--host=$mysqlHost",
        "--port=$mysqlPort",
        "--user=$mysqlUser",
        "--connect-timeout=8",
        "--batch",
        "--raw",
        "--skip-column-names",
        "-e", "SELECT 1;"
    ) $mysqlPassword

    if ($test.ExitCode -ne 0) {
        throw "MySQL connection test failed.`n$($test.StdErr)"
    }

    $dumpArgs = @(
        "--no-defaults",
        "--protocol=TCP",
        "--host=$mysqlHost",
        "--port=$mysqlPort",
        "--user=$mysqlUser",
        "--databases", $mysqlDatabase,
        "--no-data",
        "--routines",
        "--events",
        "--triggers",
        "--single-transaction",
        "--set-gtid-purged=OFF",
        "--column-statistics=0",
        "--no-tablespaces"
    )

    if ($includeViews -eq "1") {
        $full = Invoke-NativeCapture $mysqldumpExe $dumpArgs $mysqlPassword
        if ($full.ExitCode -eq 0) {
            [IO.File]::WriteAllText($SchemaOutputAbs, $full.StdOut, $Utf8NoBom)
            return
        }

        Write-Warning "Full mysqldump failed. Retrying schema export with MySQL views skipped."
        Write-Warning $full.StdErr
    }

    $viewQuery = "SELECT TABLE_NAME FROM information_schema.TABLES WHERE TABLE_SCHEMA = '$mysqlDatabase' AND TABLE_TYPE = 'VIEW' ORDER BY TABLE_NAME;"
    $viewResult = Invoke-NativeCapture $mysqlExe @(
        "--no-defaults",
        "--protocol=TCP",
        "--host=$mysqlHost",
        "--port=$mysqlPort",
        "--user=$mysqlUser",
        "--connect-timeout=8",
        "--batch",
        "--raw",
        "--skip-column-names",
        "-e", $viewQuery
    ) $mysqlPassword

    if ($viewResult.ExitCode -ne 0) {
        throw "Could not read MySQL view list.`n$($viewResult.StdErr)"
    }

    $viewNames = @(
        $viewResult.StdOut -split "\r?\n" |
            Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
    )

    $ignoreViewArgs = @()
    foreach ($viewName in $viewNames) {
        $ignoreViewArgs += "--ignore-table=$mysqlDatabase.$viewName"
    }

    $header = ""
    if ($viewNames.Count -gt 0) {
        $header += "-- NOTE: MySQL views were skipped in this schema export.`r`n"
        $header += "--       This avoids mysqldump failures when a view is invalid or has a bad definer.`r`n"
        $header += "--       Set MYSQL_SCHEMA_INCLUDE_VIEWS=1 to try exporting views.`r`n"
        $header += "-- Skipped views:`r`n"
        foreach ($viewName in $viewNames) {
            $header += "--   $viewName`r`n"
        }
        $header += "`r`n"
    }

    $dump = Invoke-NativeCapture $mysqldumpExe ($dumpArgs + $ignoreViewArgs) $mysqlPassword
    if ($dump.ExitCode -ne 0) {
        throw "mysqldump failed.`n$($dump.StdErr)"
    }

    [IO.File]::WriteAllText($SchemaOutputAbs, $header + $dump.StdOut, $Utf8NoBom)
}

Write-Host "Project root: $ProjectRoot"
New-Snapshot $CodeServerOutputAbs "OpenGothic server C/C++ source snapshot" @($ServerRoot) @(".c",".cc",".cpp",".cxx",".h",".hh",".hpp",".hxx",".inl",".ipp",".tpp",".ixx",".cppm",".mpp") $true
New-Snapshot $CodeClientOutputAbs "OpenGothic client C/C++ source snapshot" @($GameRoot) @(".c",".cc",".cpp",".cxx",".h",".hh",".hpp",".hxx",".inl",".ipp",".tpp",".ixx",".cppm",".mpp") $true
New-Snapshot $LlmOutputAbs "OpenGothic docs/llm snapshot" @($LlmRoot) @(".md",".txt",".rst") ($IncludePrivateDocs -ne "0")
New-Snapshot $ToolsOutputAbs "OpenGothic tools snapshot" @($ToolsRoot) @(".py",".sh",".bash",".md",".txt",".rst",".json",".jsonl",".yml",".yaml",".toml",".ini",".cfg",".sql") ($IncludePrivateTools -ne "0")
Export-MySqlSchema

Write-Host "Generated server code snapshot: $CodeServerOutputAbs"
Write-Host "Generated client code snapshot: $CodeClientOutputAbs"
Write-Host "Generated llm snapshot:         $LlmOutputAbs"
Write-Host "Generated tools snapshot:       $ToolsOutputAbs"
Write-Host "Generated MySQL schema:         $SchemaOutputAbs"
