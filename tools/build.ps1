[CmdletBinding()]
param(
    [ValidateSet('Release', 'RelWithDebInfo')]
    [string]$Configuration = 'Release',
    [ValidateRange(0, 32)] [int]$Parallel = 0,
    [switch]$JsonEvents
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (Test-Path variable:PSNativeCommandUseErrorActionPreference) {
    $PSNativeCommandUseErrorActionPreference = $false
}

. (Join-Path $PSScriptRoot 'release-common.ps1')
$root = Get-Fm4RepoRoot

$environment = Enter-Fm4BuildEnvironment
$logs = Join-Path $root '.local/logs'
[void](New-Item -ItemType Directory -Force -Path $logs)
if ($Parallel -eq 0) { $Parallel = Get-Fm4BuildJobCount }
$preset = 'win-amd64-' + $Configuration.ToLowerInvariant()

# A running game keeps its executable and libraries open, and the linker cannot replace them.
# Windows still lets a loaded file be renamed: move each aside and leave a copy (same
# timestamp, so nothing relinks for it) under its name. The game goes on running the old
# build; what it leaves behind is removed by the first build after it has closed.
$output = [IO.Path]::GetFullPath((Join-Path $root "out/build/$preset"))
Get-ChildItem -LiteralPath $output -Filter '*.inuse-*' -File -ErrorAction SilentlyContinue |
    ForEach-Object { try { Remove-Item -LiteralPath $_.FullName -Force -ErrorAction Stop } catch {} }
foreach ($process in @(Get-Process fm4_static_recomp -ErrorAction SilentlyContinue)) {
    # Windows PowerShell gives $null for a process it may not read (elevated, or exiting).
    $loaded = @($process.Modules | Where-Object { $_ } | ForEach-Object FileName |
        Where-Object { $_.StartsWith($output + '\', [StringComparison]::OrdinalIgnoreCase) })
    foreach ($file in $loaded) {
        $aside = "$file.inuse-$($process.Id)"
        if (Test-Path -LiteralPath $aside) { continue }
        Move-Item -LiteralPath $file -Destination $aside
        Copy-Item -LiteralPath $aside -Destination $file
    }
}

# A build folder copied from another install (an update seeds the new version's folder
# with the old one) keeps a cache CMake refuses.
[void](Reset-Fm4RelocatedCMakeCache -BuildDirectory $output)

# The SDK's configure would fetch the DLSS and FidelityFX runtimes one slow request at a time
# with no progress; fetched here instead, CMake finds them in place. What failed is left to CMake
# as before: an empty REXGLUE_DLSS_SDK_DIR clears a folder an earlier configure cached.
$sdkRoot = Resolve-Fm4SdkRoot
$bundle = Get-Fm4SdkBundle
# The prebuilt SDK carries the two CMake files that pin the runtimes.
$sdkPins = if ($bundle) { $bundle } else { $sdkRoot }
$sdkArgument = if ($bundle) { "-DREXSDK_PREFIX=$bundle" } else { "-DREXSDK_DIR=$sdkRoot" }
$dlssCommit = Select-String -LiteralPath (Join-Path $sdkPins 'cmake/rexglue_dlss.cmake') `
    -Pattern 'set\(_rexglue_dlss_commit ([0-9a-fA-F]{40})\)' | Select-Object -First 1
$dlssDirectory = ''
if ($dlssCommit) {
    $candidate = Resolve-Fm4ToolchainPath -RelativePath "nvidia-dlss-$($dlssCommit.Matches[0].Groups[1].Value.Substring(0, 12))"
    $prefetch = Invoke-Fm4ToolchainPython -JsonEvents:$JsonEvents -Arguments @('sdk-downloads',
        '--sdk-root', $sdkPins, '--dlss-dir', $candidate, '--build-dir', $output, '--json-events')
    if ($null -ne $prefetch.Result -and $prefetch.Result.PSObject.Properties['dlss_dir']) { $dlssDirectory = $candidate }
    if ($prefetch.ExitCode -ne 0) {
        Write-Host "Some upscaler runtimes were not prefetched; CMake fetches them. $(Get-Fm4ToolchainRunMessage -Run $prefetch)"
    }
}
Write-Fm4Event build 70 'Compiling FM4 Static Recomp.' -JsonEvents:$JsonEvents

Push-Location -LiteralPath $root
try {
    # The Vulkan backend's glslang reads its version through Python.
    Invoke-Fm4BuildCommand $environment.CMake (@('--preset', $preset,
        "-DPYTHON_EXECUTABLE=$(Get-Fm4Python)", $sdkArgument,
        "-DREXGLUE_DLSS_SDK_DIR=$($dlssDirectory.Replace('\', '/'))") +
        (Get-Fm4CMakeToolArguments -Environment $environment)) `
        (Join-Path $logs 'configure.log') 'Configuration failed.'
    Invoke-Fm4BuildCommand $environment.CMake @('--build', '--preset', $preset, '--parallel', "$Parallel") `
        (Join-Path $logs 'build.log') 'Compilation failed.'
}
finally { Pop-Location }

$executable = Join-Path $root "out/build/$preset/fm4_static_recomp.exe"
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw 'Compilation completed without producing fm4_static_recomp.exe.'
}
# The game links Microsoft's C++ runtime dynamically; a PC without the redistributable installed
# runs it from the copies beside the executable.
foreach ($dll in @(Get-ChildItem -LiteralPath $environment.Toolset.Redist -Filter '*.dll' -File)) {
    $target = Join-Path $output $dll.Name
    if ((Test-Path -LiteralPath $target -PathType Leaf) -and
        (Get-FileHash -LiteralPath $target).Hash -eq (Get-FileHash -LiteralPath $dll.FullName).Hash) { continue }
    Copy-Item -LiteralPath $dll.FullName -Destination $target -Force
}
"built $executable"
