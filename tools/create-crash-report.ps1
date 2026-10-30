# Adapted from Pinyon Shift's tools/create-crash-report.ps1 (BSD-3, see THIRD_PARTY_NOTICES.md).
# Bundles a crash for an issue report: a sanitized ZIP in <state>\reports and a prefilled GitHub
# issue link. The launcher runs it from the payload's tools\ with -StateRoot = its data folder and
# reads the last JSON line: crash_id, bundle, issue_url, exit_code_hex.
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$StateRoot,
    [Parameter(Mandatory)] [string]$Executable,
    [Parameter(Mandatory)] [datetime]$StartedUtc,
    [Parameter(Mandatory)] [int64]$ProcessId,
    [Parameter(Mandatory)] [int64]$ExitCode,
    [string]$ProfileId,
    [string]$Gamertag,
    [string]$RepositoryUrl,
    [switch]$Json
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$resolvedStateRoot = [IO.Path]::GetFullPath($StateRoot)
$resolvedExecutable = [IO.Path]::GetFullPath($Executable)
$reportsRoot = Join-Path $resolvedStateRoot 'reports'
[void](New-Item -ItemType Directory -Force -Path $reportsRoot)
$releasePath = Join-Path $repoRoot 'config/release.json'
$release = if (Test-Path -LiteralPath $releasePath) { Get-Content -LiteralPath $releasePath -Raw | ConvertFrom-Json } else { $null }
if (-not $RepositoryUrl) {
    $RepositoryUrl = if ($release) { $release.repository } else { 'https://github.com/i2kiselev/fm4-static-recomp' }
}

function Get-StringSha256([string]$Text) {
    $hasher = [Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [Text.Encoding]::UTF8.GetBytes($Text)
        return ([BitConverter]::ToString($hasher.ComputeHash($bytes))).Replace('-', '')
    }
    finally { $hasher.Dispose() }
}

function Get-FileSha256([string]$Path) {
    $hasher = [Security.Cryptography.SHA256]::Create()
    $stream = [IO.File]::OpenRead($Path)
    try { return ([BitConverter]::ToString($hasher.ComputeHash($stream))).Replace('-', '') }
    finally { $stream.Dispose(); $hasher.Dispose() }
}

# Longest paths first: the install root holds the game and source folders and usually lies in
# the user profile, so each path keeps its most specific placeholder.
function Protect-ReportText([string]$Text, $Replacements) {
    $result = $Text
    foreach ($entry in $Replacements) {
        $value = [string]$entry.Value
        if ([string]::IsNullOrWhiteSpace($value)) { continue }
        if ($entry.Name -in '<USERNAME>', '<GAMERTAG>') {
            $usernamePattern = '(?i)(?<![A-Z0-9_.-])' + [regex]::Escape($value) + '(?![A-Z0-9_.-])'
            $result = [regex]::Replace($result, $usernamePattern, [string]$entry.Name)
            continue
        }
        foreach ($variant in @($value, $value.Replace('\', '/'), $value.Replace('\', '\\')) | Select-Object -Unique) {
            $result = [regex]::Replace($result, [regex]::Escape($variant), [string]$entry.Name,
                [Text.RegularExpressions.RegexOptions]::IgnoreCase)
        }
    }
    # Catch any remaining user-profile path even if it used different separators.
    $result = [regex]::Replace($result, '(?i)[A-Z]:(?:\\\\|[\\/])Users(?:\\\\|[\\/])[^\\/\s"''<>]+', '<USER_PROFILE>')
    return $result
}

# The log lines written since the game started ([yyyy-MM-dd HH:mm:ss.fff] local time).
function Write-SanitizedTail([string]$Source, [string]$Destination, [int]$MaximumLines, $Replacements, [datetime]$SinceUtc) {
    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) { return $false }
    $lines = @(Get-Content -LiteralPath $Source -Tail $MaximumLines -Encoding UTF8 -ErrorAction Stop)
    $current = $false
    $lines = @($lines | ForEach-Object {
        if ($_ -match '^\[(?<time>\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})\]') {
            $lineTime = [datetime]::ParseExact($Matches.time, 'yyyy-MM-dd HH:mm:ss.fff',
                [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::AssumeLocal)
            $current = $lineTime.ToUniversalTime() -ge $SinceUtc.ToUniversalTime().AddSeconds(-2)
        }
        if ($current) { $_ }
    })
    if ($lines.Count -eq 0) { return $false }
    $content = Protect-ReportText (($lines -join [Environment]::NewLine) + [Environment]::NewLine) $Replacements
    [IO.File]::WriteAllText($Destination, $content, [Text.UTF8Encoding]::new($false))
    return $true
}

$timestamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ')
$stagingRoot = Join-Path ([IO.Path]::GetTempPath()) "FM4StaticRecompReport-$([guid]::NewGuid().ToString('N'))"
[void](New-Item -ItemType Directory -Path $stagingRoot)

$replacements = @(
    [pscustomobject]@{ Name = '<REPOSITORY>'; Value = $repoRoot }
    [pscustomobject]@{ Name = '<STATE_ROOT>'; Value = $resolvedStateRoot }
    [pscustomobject]@{ Name = '<GAME_ROOT>'; Value = (Join-Path $resolvedStateRoot 'game') }
    [pscustomobject]@{ Name = '<EXECUTABLE_DIR>'; Value = (Split-Path $resolvedExecutable -Parent) }
    [pscustomobject]@{ Name = '<USER_PROFILE>'; Value = $env:USERPROFILE }
    [pscustomobject]@{ Name = '<PROFILE>'; Value = if ($ProfileId) { Join-Path $resolvedStateRoot "profiles\$ProfileId" } else { '' } }
) | Sort-Object { ([string]$_.Value).Length } -Descending
# Whole words only: the game logs the gamertag it was started with.
$replacements = @($replacements) + @(
    [pscustomobject]@{ Name = '<USERNAME>'; Value = $env:USERNAME }
    [pscustomobject]@{ Name = '<GAMERTAG>'; Value = $Gamertag }
)

try {
    $exitHex = '0x{0:X8}' -f ([uint32]($ExitCode -band 0xFFFFFFFFL))
    $executableHash = Get-FileSha256 $resolvedExecutable
    $identity = @('fm4-static-recomp.crash.v1', "executable=$($executableHash.ToLowerInvariant())", "exit=$ExitCode") -join "`n"
    $fingerprint = 'fm4crash-v1-' + (Get-StringSha256 $identity).Substring(0, 20).ToLowerInvariant()

    $runtimeLog = Join-Path $resolvedStateRoot 'logs/runtime.log'
    $logIncluded = (Test-Path -LiteralPath $runtimeLog -PathType Leaf) -and
        (Get-Item -LiteralPath $runtimeLog).LastWriteTimeUtc -ge $StartedUtc.ToUniversalTime() -and
        (Write-SanitizedTail $runtimeLog (Join-Path $stagingRoot 'runtime-tail.log') 2500 $replacements $StartedUtc)

    # The GRAPHICS page's keys and the launcher's own options; no names, paths or licences.
    $runtimeDirectory = Split-Path $resolvedExecutable -Parent
    $allowedSettings = @('fh1_async_readbacks', 'vulkan_present_via_dxgi', 'skip_intro', 'async_readbacks',
        'present_via_dxgi', 'compat', 'build_jobs', 'toolchain_source')
    $schemaPath = Join-Path $runtimeDirectory 'graphics-settings.json'
    if (Test-Path -LiteralPath $schemaPath) {
        $schema = Get-Content -LiteralPath $schemaPath -Raw | ConvertFrom-Json
        $allowedSettings += @($schema.blocks | ForEach-Object { $_.rows } |
            Where-Object { $_.PSObject.Properties['key'] -and $_.key } | ForEach-Object { $_.key })
    }
    $settings = [ordered]@{}
    foreach ($configName in 'graphics.toml', 'launcher.toml') {
        $configPath = Join-Path $resolvedStateRoot "config/$configName"
        if (-not (Test-Path -LiteralPath $configPath -PathType Leaf)) { continue }
        foreach ($line in Get-Content -LiteralPath $configPath -Encoding UTF8) {
            if ($line -match '^\s*(?<key>[a-zA-Z0-9_]+)\s*=\s*(?<value>.+?)\s*(?:#.*)?$' -and $Matches.key -in $allowedSettings) {
                $settings[$Matches.key] = $Matches.value
            }
        }
    }

    $os = Get-CimInstance Win32_OperatingSystem -ErrorAction SilentlyContinue | Select-Object -First 1
    $cpu = Get-CimInstance Win32_Processor -ErrorAction SilentlyContinue | Select-Object -First 1
    $gpus = @(Get-CimInstance Win32_VideoController -ErrorAction SilentlyContinue | Select-Object Name, DriverVersion)
    $binaryHashes = [ordered]@{}
    foreach ($name in @('fm4_static_recomp.exe', 'rexruntime.dll', 'rexgpu-fh1.dll',
        'fm4_static_recomp_SpeechFacade_default.dll', 'fm4_static_recomp_XMediaFacade_default.dll')) {
        $path = Join-Path $runtimeDirectory $name
        if (Test-Path -LiteralPath $path -PathType Leaf) { $binaryHashes[$name] = Get-FileSha256 $path }
    }
    $manifest = [ordered]@{
        schema = 'fm4-static-recomp.crash-report.v1'
        created_utc = [DateTime]::UtcNow.ToString('o')
        crash_id = $fingerprint
        process = [ordered]@{
            process_id = $ProcessId
            started_utc = $StartedUtc.ToUniversalTime().ToString('o')
            exit_code_signed = $ExitCode
            exit_code_hex = $exitHex
        }
        build = [ordered]@{ binaries = $binaryHashes }
        release = [ordered]@{ version = if ($release) { $release.version } else { $null } }
        system = [ordered]@{
            os = if ($os) { $os.Caption } else { $null }
            os_version = if ($os) { $os.Version } else { $null }
            os_build = if ($os) { $os.BuildNumber } else { $null }
            cpu = if ($cpu) { $cpu.Name } else { $null }
            logical_processors = if ($cpu) { $cpu.NumberOfLogicalProcessors } else { $null }
            memory_bytes = if ($os) { [uint64]$os.TotalVisibleMemorySize * 1KB } else { $null }
            gpu = $gpus
        }
        settings = $settings
        runtime_log_included = [bool]$logIncluded
        privacy = [ordered]@{
            paths_redacted = $true
            game_files_included = $false
            save_data_included = $false
            generated_code_included = $false
            memory_dump_included = $false
        }
    }
    [IO.File]::WriteAllText((Join-Path $stagingRoot 'report.json'),
        (Protect-ReportText (($manifest | ConvertTo-Json -Depth 10) + [Environment]::NewLine) $replacements),
        [Text.UTF8Encoding]::new($false))

    $title = "[Crash] $fingerprint"
    $body = @(
        '### What happened?',
        '',
        '<!-- Tell us what you were doing immediately before the crash. -->',
        '',
        '### Reproduction steps',
        '',
        '1. ',
        '2. ',
        '3. ',
        '',
        '### Crash details',
        '',
        "- Crash ID: ``$fingerprint``",
        "- Version: ``$($manifest.release.version)``",
        "- Exit code: ``$exitHex``",
        "- Windows build: ``$($manifest.system.os_build)``",
        "- GPU: ``$(@($gpus | ForEach-Object { $_.Name }) -join '; ')``",
        '',
        '### Diagnostic report',
        '',
        '<!-- Drag the ZIP the launcher selected into this issue. It holds no game files, saves, generated code or memory dumps. -->'
    ) -join "`n"
    $body = Protect-ReportText $body $replacements
    [IO.File]::WriteAllText((Join-Path $stagingRoot 'issue-body.md'), $body, [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $stagingRoot 'README.txt'),
        "Attach this ZIP to the GitHub issue the launcher opened.`r`nNo game files, saves, generated code or memory dumps are included.`r`n",
        [Text.UTF8Encoding]::new($false))

    foreach ($file in Get-ChildItem -LiteralPath $stagingRoot -Recurse -File) {
        if ($file.Extension.ToLowerInvariant() -in @('.dmp', '.exe', '.dll', '.iso', '.xex', '.obj', '.lib', '.pdb', '.dxil')) {
            throw "Forbidden diagnostic attachment: $($file.Name)"
        }
        $content = Get-Content -LiteralPath $file.FullName -Raw -Encoding UTF8 -ErrorAction SilentlyContinue
        $usernamePattern = if ($env:USERNAME) { '(?i)(?<![A-Z0-9_.-])' + [regex]::Escape($env:USERNAME) + '(?![A-Z0-9_.-])' } else { $null }
        if ($content -and $usernamePattern -and $content -match $usernamePattern) {
            throw "A local user name remained in the diagnostic report: $($file.Name)"
        }
    }

    $archivePath = Join-Path $reportsRoot "FM4StaticRecomp-$fingerprint-$timestamp.zip"
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::CreateFromDirectory($stagingRoot, $archivePath, [IO.Compression.CompressionLevel]::Optimal, $false)
    # The crash form, not a blank issue with a body: its required fields stop an empty report.
    $details = Protect-ReportText (@(
        "- Exit code: $exitHex",
        "- Windows build: $($manifest.system.os_build)",
        "- GPU: $(@($gpus | ForEach-Object { $_.Name }) -join '; ')"
    ) -join "`n") $replacements
    $fields = [ordered]@{
        template = 'crash_report.yml'
        title = $title
        'crash-id' = $fingerprint
        version = [string]$manifest.release.version
        details = $details
    }
    $query = @($fields.GetEnumerator() | ForEach-Object { "$($_.Key)=$([Uri]::EscapeDataString($_.Value))" }) -join '&'
    $issueUrl = "$RepositoryUrl/issues/new?$query"
    $record = [ordered]@{
        schema = 1
        created_utc = [DateTime]::UtcNow.ToString('o')
        crash_id = $fingerprint
        bundle = $archivePath
        issue_url = $issueUrl
        exit_code_hex = $exitHex
    }
    if ($Json) { $record | ConvertTo-Json -Compress }
    else { $record }
}
finally {
    $resolvedStaging = [IO.Path]::GetFullPath($stagingRoot)
    $temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/') + '\'
    if ($resolvedStaging.StartsWith($temporaryRoot, [StringComparison]::OrdinalIgnoreCase) -and
        (Split-Path $resolvedStaging -Leaf).StartsWith('FM4StaticRecompReport-')) {
        Remove-Item -LiteralPath $resolvedStaging -Recurse -Force -ErrorAction SilentlyContinue
    }
}
