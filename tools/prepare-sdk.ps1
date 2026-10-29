# Adapted from Pinyon Shift's tools/prepare-rexglue.ps1 (BSD-3, see THIRD_PARTY_NOTICES.md).
[CmdletBinding()]
param([switch]$JsonEvents)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (Test-Path variable:PSNativeCommandUseErrorActionPreference) {
    $PSNativeCommandUseErrorActionPreference = $false
}

. (Join-Path $PSScriptRoot 'release-common.ps1')
$config = Get-Fm4ReleaseToolchain
$sdkRoot = Resolve-Fm4SdkRoot
$managedRoot = Resolve-Fm4LocalPath -RelativePath $config.sdk.fallback_path
# FM4_SDK_ROOT, the development sibling or a submodule: an SDK someone else manages.
if (-not [string]::Equals($sdkRoot, $managedRoot, [StringComparison]::OrdinalIgnoreCase)) {
    Write-Fm4Event sdk 44 "Using the SDK at $sdkRoot" -JsonEvents:$JsonEvents
    return
}
$bundle = Get-Fm4SdkBundle
if ($bundle) {
    # Written once the bundle is unpacked, as fm4-prepared is for a checkout; it names the archive,
    # so a bundle published again for the same SDK revision replaces the old one.
    $marker = Join-Path $bundle 'fm4-prepared'
    if ((Test-Path -LiteralPath $marker -PathType Leaf) -and
        ([IO.File]::ReadAllText($marker).Trim() -eq $config.sdk.bundle.sha256)) {
        Write-Fm4Event sdk 44 'The prebuilt SDK is ready.' -JsonEvents:$JsonEvents
        return
    }
    if (Test-Path -LiteralPath $bundle) { Remove-Item -LiteralPath $bundle -Recurse -Force }
    $archive = Resolve-Fm4LocalPath -RelativePath ".local/downloads/shiftglue-sdk-$($config.sdk.revision).tar.xz"
    Write-Fm4Event sdk 36 'Downloading the prebuilt ShiftGlue SDK.' -JsonEvents:$JsonEvents
    Invoke-Fm4ParallelDownload -Uri $config.sdk.bundle.url -Destination $archive -Sha256 $config.sdk.bundle.sha256 `
        -What 'the prebuilt ShiftGlue SDK' -Percent 36 -EndPercent 43 -Stage sdk -JsonEvents:$JsonEvents
    $staging = "$bundle-staging"
    if (Test-Path -LiteralPath $staging) { Remove-Item -LiteralPath $staging -Recurse -Force }
    [void](New-Item -ItemType Directory -Force -Path $staging)
    $xz = Join-Path (Resolve-Fm4ToolchainPath -RelativePath $config.xz.install_path) $config.xz.executable
    Expand-Fm4TarXz -Archive $archive -Destination $staging -XzPath $xz
    $children = @(Get-ChildItem -LiteralPath $staging -Directory)
    if ($children.Count -ne 1) { throw 'The prebuilt SDK archive layout is not recognized.' }
    Move-Item -LiteralPath $children[0].FullName -Destination $bundle
    Remove-Item -LiteralPath $staging -Recurse -Force
    [IO.File]::WriteAllText($marker, $config.sdk.bundle.sha256)
    Write-Fm4Event sdk 44 'The prebuilt SDK is ready.' -JsonEvents:$JsonEvents
    return
}
$git = Get-Fm4Git
$repository = if ($env:FM4_SDK_REPOSITORY) { $env:FM4_SDK_REPOSITORY } else { $config.sdk.repository }
$logs = Resolve-Fm4LocalPath -RelativePath '.local/logs'
$gitLog = Join-Path $logs 'sdk-prepare.log'
if (Test-Path -LiteralPath $gitLog) { Remove-Item -LiteralPath $gitLog -Force }

function Invoke-Fm4GitWithRetry {
    param(
        [Parameter(Mandatory)] [string]$Activity,
        [Parameter(Mandatory)] [string[]]$Arguments
    )

    $maximumAttempts = 3
    $code = 0
    for ($attempt = 1; $attempt -le $maximumAttempts; $attempt++) {
        # Keep Git's stderr (where it reports errors) in the output and in
        # .local/logs/sdk-prepare.log for the failure report.
        Invoke-Fm4LoggedCommand -FilePath $git -Arguments $Arguments -LogPath $gitLog -Append
        $code = $LASTEXITCODE
        if ($code -eq 0) { return }
        if ($attempt -lt $maximumAttempts) {
            Write-Fm4Event sdk 37 `
                "$Activity failed (attempt $attempt of $maximumAttempts); retrying." `
                -JsonEvents:$JsonEvents
            Start-Sleep -Seconds (2 * $attempt)
        }
    }
    throw (New-Fm4CommandFailure -FailureMessage "$Activity failed after $maximumAttempts attempts." `
        -Step $Activity -LogPath $gitLog -ExitCode $code `
        -CommandLine (Format-Fm4CommandLine -FilePath $git -Arguments $Arguments))
}

function Repair-MaterializedLinks {
    param([Parameter(Mandatory)] [string]$RepositoryRoot)
    $mspackRoot = Join-Path $RepositoryRoot 'thirdparty/libmspack'
    if (-not (Test-Path -LiteralPath (Join-Path $mspackRoot '.git'))) { return }
    $entries = @(& $git -C $mspackRoot ls-files -s | Where-Object { $_ -match '^120000 ' })
    foreach ($entry in $entries) {
        if ($entry -notmatch '^120000 [0-9a-f]+ 0\t(.+)$') {
            throw "Unable to parse a libmspack symlink entry: $entry"
        }
        $relative = $Matches[1]
        $targetText = ((& $git -C $mspackRoot show "HEAD:$relative") -join "`n").Trim()
        $linkFile = [IO.Path]::GetFullPath((Join-Path $mspackRoot $relative))
        $targetFile = [IO.Path]::GetFullPath((Join-Path (Split-Path $linkFile -Parent) $targetText))
        $prefix = [IO.Path]::GetFullPath($mspackRoot).TrimEnd('\', '/') +
            [IO.Path]::DirectorySeparatorChar
        if (-not $targetFile.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase) -or
            -not (Test-Path -LiteralPath $targetFile -PathType Leaf)) {
            throw "Invalid libmspack link target for '$relative'."
        }
        $item = Get-Item -LiteralPath $linkFile
        if ($null -eq $item.LinkType) {
            $current = (Get-FileHash -LiteralPath $linkFile -Algorithm SHA256).Hash
            $target = (Get-FileHash -LiteralPath $targetFile -Algorithm SHA256).Hash
            if ($current -ne $target) {
                Copy-Item -LiteralPath $targetFile -Destination $linkFile -Force
            }
        }
    }
}

# Written once the checkout is complete. Without it the checkout is a download cut short (a
# closed launcher ends git mid-clone, leaving submodules half checked out), not local changes.
$prepared = Join-Path $sdkRoot '.git/fm4-prepared'
if ((Test-Path -LiteralPath $sdkRoot) -and -not (Test-Path -LiteralPath $prepared)) {
    Remove-Item -LiteralPath $sdkRoot -Recurse -Force
}
if (Test-Path -LiteralPath $sdkRoot) {
    if (Test-Fm4GitDirty -Git $git -Repository $sdkRoot) {
        throw "The managed SDK checkout has local changes: $sdkRoot"
    }
    if ((Get-Fm4GitHead -Git $git -Repository $sdkRoot) -ne $config.sdk.revision) {
        Remove-Item -LiteralPath $sdkRoot -Recurse -Force
    }
}
if (-not (Test-Path -LiteralPath $sdkRoot)) {
    Write-Fm4Event sdk 36 'Downloading the pinned ShiftGlue SDK source.' -JsonEvents:$JsonEvents
    [void](New-Item -ItemType Directory -Force -Path $sdkRoot)
    & $git -C $sdkRoot init --quiet
    if ($LASTEXITCODE -ne 0) { throw "Unable to create the SDK checkout: $sdkRoot" }
    & $git -C $sdkRoot config core.symlinks false
    & $git -C $sdkRoot remote add origin $repository
    if ($LASTEXITCODE -ne 0) { throw "Unable to set the SDK repository: $repository" }
    Invoke-Fm4GitWithRetry -Activity 'Fetching the pinned SDK revision' -Arguments @(
        '-c', 'http.version=HTTP/1.1', '-C', $sdkRoot, 'fetch', '--depth', '1', '--no-tags',
        'origin', $config.sdk.revision
    )
    & $git -C $sdkRoot checkout --quiet --detach FETCH_HEAD
    if ($LASTEXITCODE -ne 0) { throw 'Unable to check out the pinned SDK revision.' }
}
& $git -C $sdkRoot submodule sync --recursive
if ($LASTEXITCODE -ne 0) { throw 'Unable to synchronize the SDK dependency URLs.' }
Invoke-Fm4GitWithRetry -Activity 'Downloading the SDK dependencies' -Arguments @(
    '-c', 'http.version=HTTP/1.1', '-C', $sdkRoot,
    'submodule', 'update', '--init', '--recursive', '--jobs', '8'
)

if ((Get-Fm4GitHead -Git $git -Repository $sdkRoot) -ne $config.sdk.revision) {
    throw 'The prepared SDK is not at the pinned revision.'
}
Repair-MaterializedLinks -RepositoryRoot $sdkRoot
[IO.File]::WriteAllText($prepared, $config.sdk.revision)
Write-Fm4Event sdk 44 'The SDK source is ready.' -JsonEvents:$JsonEvents
