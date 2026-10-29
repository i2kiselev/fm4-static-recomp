# Adapted from Pinyon Shift's tools/provision-toolchain.ps1 (BSD-3, see THIRD_PARTY_NOTICES.md).
[CmdletBinding()]
param(
    # The player accepted the licences of Microsoft's C++ runtime and Windows SDK files, which the
    # default route downloads from Microsoft into the toolchain folder.
    [switch]$AcceptMicrosoftLicense,
    # VisualStudio: take those files from an installed Visual Studio, or install the Build Tools
    # (asks for administrator permission once).
    [ValidateSet('Feed', 'VisualStudio')] [string]$ToolchainSource = 'Feed',
    [switch]$JsonEvents
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (Test-Path variable:PSNativeCommandUseErrorActionPreference) {
    $PSNativeCommandUseErrorActionPreference = $false
}

. (Join-Path $PSScriptRoot 'release-common.ps1')
$config = Get-Fm4ReleaseToolchain
Assert-Fm4ToolchainPathSupported
$downloads = Resolve-Fm4ToolchainPath -RelativePath 'downloads'
[void](New-Item -ItemType Directory -Force -Path $downloads)

function Install-Fm4BuildTools {
    Write-Fm4Event tools 26 'Installing the Microsoft C++ Build Tools. Windows may ask for administrator permission.' -JsonEvents:$JsonEvents
    $bootstrap = Join-Path $downloads 'vs_BuildTools.exe'
    if (-not (Test-Path -LiteralPath $bootstrap -PathType Leaf)) {
        $partial = "$bootstrap.partial"
        Invoke-WebRequest -Uri $config.visual_studio.bootstrap_url -OutFile $partial -UseBasicParsing
        Move-Item -LiteralPath $partial -Destination $bootstrap -Force
    }
    $signature = Get-AuthenticodeSignature -LiteralPath $bootstrap
    if ($signature.Status -ne 'Valid' -or
        $signature.SignerCertificate.Subject -notlike "*$($config.visual_studio.bootstrap_signer)*") {
        Remove-Item -LiteralPath $bootstrap -Force
        throw 'The Microsoft Build Tools installer signature is invalid.'
    }
    $helper = Join-Path $PSScriptRoot 'install-build-tools.ps1'
    $arguments = "-NoLogo -NoProfile -ExecutionPolicy Bypass -File `"$helper`" -Bootstrapper `"$bootstrap`""
    # The running PowerShell by its own path, since PATH may not hold it. Process.Start keeps
    # the Win32 error that Start-Process drops, so a declined permission prompt
    # (ERROR_CANCELLED) can be told apart from a failed installer.
    $startInfo = [Diagnostics.ProcessStartInfo]::new((Get-Process -Id $PID).Path, $arguments)
    $startInfo.Verb = 'runas'
    $startInfo.UseShellExecute = $true
    try {
        $elevated = [Diagnostics.Process]::Start($startInfo)
    }
    catch {
        $win32 = $_.Exception.InnerException -as [ComponentModel.Win32Exception]
        if ($null -ne $win32 -and $win32.NativeErrorCode -eq 1223) {
            throw ('Windows administrator permission was declined, so the Microsoft C++ Build Tools ' +
                'were not installed. The build needs them: start the build again and choose Yes when ' +
                'Windows asks for permission.')
        }
        throw
    }
    $elevated.WaitForExit()
    if ($elevated.ExitCode -notin @(0, 3010)) {
        throw "Microsoft C++ Build Tools installation stopped with exit code $($elevated.ExitCode)."
    }
}

function New-Fm4ToolchainFailure {
    param([Parameter(Mandatory)] [string]$Step, [Parameter(Mandatory)] [string]$Message)
    $failure = [Exception]::new($Message)
    $failure.Data['step'] = $Step
    $failure
}

# Python first: it downloads the larger tools below in parallel parts.
Write-Fm4Event tools 18 'Preparing the local Python runtime.' -JsonEvents:$JsonEvents
$pythonRoot = Resolve-Fm4ToolchainPath -RelativePath $config.python.install_path
$pythonExe = Join-Path $pythonRoot $config.python.executable
$pythonCheck = "import hashlib, json, struct, subprocess, tomllib, zipfile; assert hasattr(hashlib, 'file_digest')"
$pythonReady = $false
if (Test-Path -LiteralPath $pythonExe -PathType Leaf) {
    & $pythonExe -I -c $pythonCheck
    $pythonReady = $LASTEXITCODE -eq 0
}
if (-not $pythonReady) {
    $archive = Join-Path $downloads "python-$($config.python.version)-embed-amd64.zip"
    Invoke-Fm4Download -Uri $config.python.url -Destination $archive -Sha256 $config.python.sha256
    [void](New-Item -ItemType Directory -Force -Path $pythonRoot)
    Expand-Fm4Zip -Archive $archive -Destination $pythonRoot
}
& $pythonExe -I -c $pythonCheck
if ($LASTEXITCODE -ne 0) { throw 'The local Python runtime failed its shader tooling check.' }

# Git only checks the SDK source out; a prebuilt SDK needs none.
$needsGit = $null -eq (Get-Fm4SdkBundle)
$gitRoot = Resolve-Fm4ToolchainPath -RelativePath $config.git.install_path
$gitExe = Join-Path $gitRoot $config.git.executable
if ($needsGit -and -not (Test-Path -LiteralPath $gitExe -PathType Leaf)) {
    Write-Fm4Event tools 19 'Preparing Git.' -JsonEvents:$JsonEvents
    $archive = Join-Path $downloads "mingit-$($config.git.version).zip"
    Invoke-Fm4ParallelDownload -Uri $config.git.url -Destination $archive -Sha256 $config.git.sha256 `
        -What 'Git' -Percent 19 -EndPercent 19 -JsonEvents:$JsonEvents
    [void](New-Item -ItemType Directory -Force -Path $gitRoot)
    Expand-Fm4Zip -Archive $archive -Destination $gitRoot
}

Write-Fm4Event tools 20 'Preparing the pinned LLVM compiler.' -JsonEvents:$JsonEvents
$xzRoot = Resolve-Fm4ToolchainPath -RelativePath $config.xz.install_path
$xzExe = Join-Path $xzRoot $config.xz.executable
if (-not (Test-Path -LiteralPath $xzExe -PathType Leaf)) {
    $archive = Join-Path $downloads "xz-$($config.xz.version)-windows.zip"
    Invoke-Fm4ParallelDownload -Uri $config.xz.url -Destination $archive -Sha256 $config.xz.sha256 `
        -What 'XZ' -Percent 20 -EndPercent 20 -JsonEvents:$JsonEvents
    if (Test-Path -LiteralPath $xzRoot) { Remove-Item -LiteralPath $xzRoot -Recurse -Force }
    [void](New-Item -ItemType Directory -Force -Path $xzRoot)
    Expand-Fm4Zip -Archive $archive -Destination $xzRoot
}

$llvmRoot = Resolve-Fm4ToolchainPath -RelativePath $config.llvm.install_path
$llvmExe = Join-Path $llvmRoot $config.llvm.executable
if (-not (Test-Path -LiteralPath $llvmExe -PathType Leaf)) {
    $archive = Join-Path $downloads "llvm-$($config.llvm.version).tar.xz"
    Invoke-Fm4ParallelDownload -Uri $config.llvm.url -Destination $archive -Sha256 $config.llvm.sha256 `
        -What 'LLVM' -Percent 20 -EndPercent 23 -JsonEvents:$JsonEvents
    $staging = Resolve-Fm4ToolchainPath -RelativePath "llvm-$($config.llvm.version)-staging"
    if (Test-Path -LiteralPath $staging) { Remove-Item -LiteralPath $staging -Recurse -Force }
    [void](New-Item -ItemType Directory -Force -Path $staging)
    Expand-Fm4TarXz -Archive $archive -Destination $staging -XzPath $xzExe
    $children = @(Get-ChildItem -LiteralPath $staging -Directory)
    if ($children.Count -ne 1) { throw 'The LLVM archive layout is not recognized.' }
    if (Test-Path -LiteralPath $llvmRoot) { Remove-Item -LiteralPath $llvmRoot -Recurse -Force }
    Move-Item -LiteralPath $children[0].FullName -Destination $llvmRoot
    Remove-Item -LiteralPath $staging -Recurse -Force
}

Write-Fm4Event tools 24 'Preparing CMake with support for the build presets.' -JsonEvents:$JsonEvents
$cmakeRoot = Resolve-Fm4ToolchainPath -RelativePath $config.cmake.install_path
if (-not (Test-Path -LiteralPath (Join-Path $cmakeRoot $config.cmake.executable) -PathType Leaf)) {
    $archive = Join-Path $downloads "cmake-$($config.cmake.version).zip"
    Invoke-Fm4ParallelDownload -Uri $config.cmake.url -Destination $archive -Sha256 $config.cmake.sha256 `
        -What 'CMake' -Percent 24 -EndPercent 24 -JsonEvents:$JsonEvents
    Expand-Fm4Zip -Archive $archive -Destination (Split-Path $cmakeRoot -Parent)
}

Write-Fm4Event tools 25 'Preparing Ninja.' -JsonEvents:$JsonEvents
$ninjaRoot = Resolve-Fm4ToolchainPath -RelativePath $config.ninja.install_path
if (-not (Test-Path -LiteralPath (Join-Path $ninjaRoot $config.ninja.executable) -PathType Leaf)) {
    $archive = Join-Path $downloads "ninja-$($config.ninja.version)-win.zip"
    Invoke-Fm4ParallelDownload -Uri $config.ninja.url -Destination $archive -Sha256 $config.ninja.sha256 `
        -What 'Ninja' -Percent 25 -EndPercent 25 -JsonEvents:$JsonEvents
    [void](New-Item -ItemType Directory -Force -Path $ninjaRoot)
    Expand-Fm4Zip -Archive $archive -Destination $ninjaRoot
}

if ($ToolchainSource -eq 'VisualStudio') {
    if ($null -eq (Get-Fm4VisualStudioToolset)) { Install-Fm4BuildTools }
    if ($null -eq (Get-Fm4VisualStudioToolset)) {
        throw (New-Fm4ToolchainFailure -Step 'msvc' -Message ("The Microsoft C++ Build Tools hold no " +
            "$($config.msvc.toolset_line) toolset with the Windows SDK $($config.windows_sdk.sdk_folder_version)."))
    }
}
elseif ($null -eq (Get-Fm4BuildToolset | Where-Object { $null -ne $_ -and $_.Source -eq 'feed' })) {
    $failure = $null
    if ($AcceptMicrosoftLicense) {
        $paths = Get-Fm4MicrosoftToolchainPaths
        $microsoftDownloads = Join-Path (Split-Path $paths.MsvcRoot -Parent) 'downloads'
        $sdkArguments = @('windows-sdk', '--version', $paths.SdkVersion, '--root', $paths.SdkRoot,
            '--downloads', $microsoftDownloads, '--accept-license', '--json-events')
        foreach ($package in $config.windows_sdk.packages) {
            $sdkArguments += @('--package', "$($package.id)=$($package.sha256)")
        }
        foreach ($command in @(
            @{ Step = 'windows-sdk'; What = 'Windows SDK'; Arguments = $sdkArguments },
            @{ Step = 'msvc'; What = 'C++ runtime'; Arguments = @('msvc', '--channel', $config.msvc.channel_url, '--line', $paths.Line,
                '--root', $paths.MsvcRoot, '--downloads', $microsoftDownloads, '--accept-license', '--json-events') }
        )) {
            $run = Invoke-Fm4ToolchainPython -Arguments $command.Arguments -JsonEvents:$JsonEvents
            if ($run.ExitCode -ne 0) {
                $failure = New-Fm4ToolchainFailure -Step $command.Step `
                    -Message "Downloading Microsoft's $($command.What) failed: $(Get-Fm4ToolchainRunMessage -Run $run)"
                break
            }
        }
    }
    if ($null -ne $failure -or -not $AcceptMicrosoftLicense) {
        if ($null -ne (Get-Fm4VisualStudioToolset)) {
            Write-Fm4Event tools 31 'Using the Microsoft C++ files of the installed Visual Studio.' -JsonEvents:$JsonEvents
        }
        elseif ($null -ne $failure) { throw $failure }
        else {
            throw (New-Fm4ToolchainFailure -Step 'licence' -Message ("The build needs Microsoft's C++ runtime and " +
                'Windows SDK files, downloaded from Microsoft. Accept their licences (setup -AcceptMicrosoftLicense; ' +
                'tools\microsoft-licences.ps1 names them), or run setup with -ToolchainSource VisualStudio.'))
        }
    }
}

$environment = Enter-Fm4BuildEnvironment
$git = if ($needsGit) { Get-Fm4Git } else { $null }
$tools = @(
    @{ Name = 'XZ'; Path = $xzExe },
    @{ Name = 'CMake'; Path = $environment.CMake },
    @{ Name = 'Ninja'; Path = $environment.Ninja },
    @{ Name = 'LLVM'; Path = $llvmExe },
    @{ Name = 'Python'; Path = $pythonExe },
    @{ Name = 'rc.exe'; Path = $environment.Toolset.Rc }
)
if ($needsGit) { $tools += @{ Name = 'Git'; Path = $git } }
foreach ($required in $tools) {
    if (-not (Test-Path -LiteralPath $required.Path -PathType Leaf)) {
        throw "$($required.Name) was not provisioned correctly: $($required.Path)"
    }
}
Write-Fm4Event tools 32 'Windows build tools are ready.' -JsonEvents:$JsonEvents

[pscustomobject]@{
    result = 'pass'
    git = $git
    cmake = $environment.CMake
    ninja = $environment.Ninja
    llvm = $llvmExe
    python = $pythonExe
    msvc = "$($environment.Toolset.Source) $($environment.Toolset.ToolsetVersion)"
}
