# Packs the source payload the launcher builds from into .artifacts\fm4-static-recomp-source.zip.
[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$artifacts = Join-Path $root '.artifacts'
$payloadRoot = Join-Path $artifacts 'payload'
$payloadZip = Join-Path $artifacts 'fm4-static-recomp-source.zip'

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

# Adapted from Pinyon Shift's tools/package-launcher.ps1 (BSD-3, see THIRD_PARTY_NOTICES.md).
function New-DeterministicZip {
    param(
        [Parameter(Mandatory)] [string]$SourceDirectory,
        [Parameter(Mandatory)] [string]$DestinationPath
    )

    if (Test-Path -LiteralPath $DestinationPath) {
        Remove-Item -LiteralPath $DestinationPath -Force
    }
    $fixedTimestamp = [DateTimeOffset]::FromUnixTimeSeconds(1784764800)
    $archive = [IO.Compression.ZipFile]::Open(
        $DestinationPath, [IO.Compression.ZipArchiveMode]::Create)
    try {
        Get-ChildItem -LiteralPath $SourceDirectory -Recurse -File |
            Sort-Object { $_.FullName.Substring($SourceDirectory.Length).Replace([char]92, [char]47) } |
            ForEach-Object {
                $entryName = $_.FullName.Substring($SourceDirectory.Length).TrimStart([char]92, [char]47).Replace([char]92, [char]47)
                $entry = $archive.CreateEntry(
                    $entryName, [IO.Compression.CompressionLevel]::Optimal)
                $entry.LastWriteTime = $fixedTimestamp
                $sourceStream = [IO.File]::OpenRead($_.FullName)
                $entryStream = $entry.Open()
                try {
                    $sourceStream.CopyTo($entryStream)
                }
                finally {
                    $entryStream.Dispose()
                    $sourceStream.Dispose()
                }
            }
    }
    finally {
        $archive.Dispose()
    }
}

if (Test-Path -LiteralPath $payloadRoot) { Remove-Item -LiteralPath $payloadRoot -Recurse -Force }
[void](New-Item -ItemType Directory -Force -Path $payloadRoot)

$include = @(
    'CMakeLists.txt', 'CMakePresets.json', 'LICENSE', 'README.md', 'THIRD_PARTY_NOTICES.md', 'DEBUG_OPTIONS.md', 'guide', 'cmake', 'src',
    'config/rexglue', 'config/release.json', 'config/release-toolchain.json',
    'config/supported-discs.json', 'config/shaders', 'config/warm-up',
    'tools/release-common.ps1', 'tools/provision-toolchain.ps1', 'tools/install-build-tools.ps1',
    'tools/prepare-sdk.ps1', 'tools/codegen.ps1', 'tools/build.ps1', 'tools/setup.ps1',
    'tools/release_analysis.py', 'tools/code_images.py',
    'tools/run.ps1', 'tools/warm-up.ps1', 'tools/warm-up-common.ps1',
    'tools/toolchain.py', 'tools/toolchain', 'tools/microsoft-licences.ps1', 'tools/create-crash-report.ps1'
)
foreach ($relative in $include) {
    $source = Join-Path $root $relative
    if (-not (Test-Path -LiteralPath $source)) { throw "Required payload source is missing: $relative" }
    $destination = Join-Path $payloadRoot $relative
    if (Test-Path -LiteralPath $source -PathType Container) {
        [void](New-Item -ItemType Directory -Force -Path $destination)
        Get-ChildItem -LiteralPath $source -Force | Copy-Item -Destination $destination -Recurse -Force
    }
    else {
        [void](New-Item -ItemType Directory -Force -Path (Split-Path $destination -Parent))
        Copy-Item -LiteralPath $source -Destination $destination -Force
    }
}
$toolchainTests = Join-Path $payloadRoot 'tools/toolchain/tests'
if (Test-Path -LiteralPath $toolchainTests) { Remove-Item -LiteralPath $toolchainTests -Recurse -Force }
Get-ChildItem -LiteralPath $payloadRoot -Recurse -Directory -Filter '__pycache__' |
    ForEach-Object { Remove-Item -LiteralPath $_.FullName -Recurse -Force }
$localManifest = Join-Path $payloadRoot 'config/rexglue/fm4_manifest.local.toml'
if (Test-Path -LiteralPath $localManifest) { Remove-Item -LiteralPath $localManifest -Force }

$git = Get-Command git.exe -ErrorAction Stop
$sourceCommit = @(& $git.Source -C $root rev-parse HEAD 2>$null | Select-Object -First 1)
if ($sourceCommit.Count -ne 1 -or $sourceCommit[0] -notmatch '^[0-9a-fA-F]{40}$') {
    throw 'Unable to record the source commit for the payload.'
}
$release = Get-Content -LiteralPath (Join-Path $root 'config/release.json') -Raw | ConvertFrom-Json
$sourceProvenance = [ordered]@{
    schema_version = 1
    repository = $release.repository
    commit = $sourceCommit[0].ToLowerInvariant()
    dirty = @(& $git.Source -C $root status --porcelain).Count -ne 0
}
[IO.File]::WriteAllText((Join-Path $payloadRoot 'config/source-provenance.json'),
    ($sourceProvenance | ConvertTo-Json) + [Environment]::NewLine,
    [Text.UTF8Encoding]::new($false))

Get-ChildItem -LiteralPath $payloadRoot -Recurse -File | Where-Object {
    $_.Extension -in @('.exe', '.dll', '.obj', '.lib', '.pdb', '.iso', '.xex')
} | ForEach-Object { throw "Forbidden file entered the payload: $($_.FullName)" }

New-DeterministicZip -SourceDirectory $payloadRoot -DestinationPath $payloadZip
[pscustomobject]@{
    source_payload = $payloadZip
    sha256 = (Get-FileHash -LiteralPath $payloadZip -Algorithm SHA256).Hash
}
