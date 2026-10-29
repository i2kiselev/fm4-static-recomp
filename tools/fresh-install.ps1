# A new player's first setup, by hand: builds this tree's release package, unpacks the download
# players get (FM4StaticRecomp-Launcher.zip) into a new folder and starts its launcher.
# -LocalAssets points the package's setup at the LLVM and SDK archives built here
# (tools\package-llvm.py, tools\package-sdk.ps1) instead of their release URLs, for archives not
# uploaded yet. The folder is a full install (about 10 GB); delete it when done.
[CmdletBinding()]
param(
    [string]$Folder = (Join-Path $HOME "fm4e2e\fresh-$(Get-Date -Format 'yyyyMMdd-HHmmss')"),
    [switch]$LocalAssets,
    # Use .artifacts\release as it is instead of building it again.
    [switch]$SkipPackage
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$Folder = [IO.Path]::GetFullPath($Folder)
if (Test-Path -LiteralPath $Folder) { throw "$Folder exists; a fresh install needs a new folder." }

if (-not $SkipPackage) { & (Join-Path $PSScriptRoot 'package-release.ps1') | Out-Host }
$package = Join-Path $root '.artifacts/release/FM4StaticRecomp-Launcher.zip'
if (-not (Test-Path -LiteralPath $package -PathType Leaf)) { throw "No release package at $package; run without -SkipPackage." }

Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::ExtractToDirectory($package, $Folder)

if ($LocalAssets) {
    $config = Get-Content -LiteralPath (Join-Path $root 'config/release-toolchain.json') -Raw | ConvertFrom-Json
    $assets = @(
        @{ Pin = $config.llvm; Folder = '.artifacts/toolchain' },
        @{ Pin = $config.sdk.bundle; Folder = '.artifacts/sdk-bundle' }
    )
    $source = [IO.Compression.ZipFile]::Open((Join-Path $Folder 'fm4-static-recomp-source.zip'), [IO.Compression.ZipArchiveMode]::Update)
    try {
        $entry = $source.GetEntry('config/release-toolchain.json')
        $reader = [IO.StreamReader]::new($entry.Open())
        $text = $reader.ReadToEnd()
        $reader.Dispose()
        foreach ($asset in $assets) {
            $file = Join-Path $root "$($asset.Folder)/$([Uri]::UnescapeDataString(($asset.Pin.url -split '/')[-1]))"
            if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Not built here: $file" }
            if ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $asset.Pin.sha256) {
                throw "$file is not the archive config\release-toolchain.json pins."
            }
            $text = $text.Replace($asset.Pin.url, ([Uri]$file).AbsoluteUri)
        }
        $entry.Delete()
        $writer = [IO.StreamWriter]::new($source.CreateEntry('config/release-toolchain.json').Open(), [Text.UTF8Encoding]::new($false))
        $writer.Write($text)
        $writer.Dispose()
    }
    finally { $source.Dispose() }
    Write-Host 'Setup takes LLVM and the SDK from the archives built here.'
}

Start-Process -FilePath (Join-Path $Folder 'FM4StaticRecompLauncher.exe')
Write-Host "Launcher started from $Folder"
