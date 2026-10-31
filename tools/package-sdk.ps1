# Builds the prebuilt ShiftGlue SDK that payloads build against (config\release-toolchain.json's
# sdk.bundle), published as a release asset: the SDK's own install of the pinned checkout, built
# with the settings Fm4RexGlue.cmake gives the SDK in-tree, plus the CMake files pinning the
# upscaler runtimes (build.ps1 fetches those from NVIDIA and AMD as before) and the licences of
# what is built in. It holds no game code. Beside it goes the source of every SDK submodule at its
# pinned commit (sdk.bundle.sources), which the LGPL and GPL parts ask for. Output in
# .artifacts\sdk-bundle.
[CmdletBinding()]
param([ValidateRange(0, 32)] [int]$Parallel = 0)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (Test-Path variable:PSNativeCommandUseErrorActionPreference) {
    $PSNativeCommandUseErrorActionPreference = $false
}

. (Join-Path $PSScriptRoot 'release-common.ps1')
$root = Get-Fm4RepoRoot
$config = Get-Fm4ReleaseToolchain
$sdkRoot = Resolve-Fm4SdkRoot
if (Get-Fm4SdkBundle) { throw 'package-sdk.ps1 builds from an SDK source tree: set FM4_SDK_ROOT or use the development sibling.' }
$git = (Get-Command git.exe -ErrorAction Stop).Source
$head = Get-Fm4GitHead -Git $git -Repository $sdkRoot
if ($head -ne $config.sdk.revision) { throw "The SDK at $sdkRoot is at $head, not the pinned $($config.sdk.revision)." }
if (Test-Fm4GitDirty -Git $git -Repository $sdkRoot) { throw "The SDK at $sdkRoot has local changes." }
$moved = @(& $git -C $sdkRoot submodule status --recursive | Where-Object { $_ -match '^[-+U]' })
if ($moved.Count -gt 0) { throw "SDK submodules are not at their pinned commits: $($moved -join '; ')" }

$environment = Enter-Fm4BuildEnvironment
if ($Parallel -eq 0) { $Parallel = Get-Fm4BuildJobCount }
$work = Join-Path $root '.artifacts/sdk-bundle'
$build = Join-Path $work 'build'
$install = Join-Path $work 'install'
$logs = Join-Path $work 'logs'
foreach ($folder in $install, (Join-Path $work 'stage')) {
    if (Test-Path -LiteralPath $folder) { Remove-Item -LiteralPath $folder -Recurse -Force }
}
[void](New-Item -ItemType Directory -Force -Path $build, $logs)

# The DLSS provider is built in: without NVIDIA's headers and loader the bundle would lack it.
$dlssCommit = (Select-String -LiteralPath (Join-Path $sdkRoot 'cmake/rexglue_dlss.cmake') `
    -Pattern 'set\(_rexglue_dlss_commit ([0-9a-fA-F]{40})\)').Matches[0].Groups[1].Value
$dlss = Resolve-Fm4ToolchainPath -RelativePath "nvidia-dlss-$($dlssCommit.Substring(0, 12))"
$prefetch = Invoke-Fm4ToolchainPython -Arguments @('sdk-downloads', '--sdk-root', $sdkRoot,
    '--dlss-dir', $dlss, '--build-dir', $build)
if ($prefetch.ExitCode -ne 0) { throw "The upscaler files were not fetched: $(Get-Fm4ToolchainRunMessage -Run $prefetch)" }

$installPrefix = $install.Replace('\', '/')
Invoke-Fm4BuildCommand $environment.CMake (@('-S', $sdkRoot, '-B', $build, '-G', 'Ninja',
    '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_C_COMPILER=clang', '-DCMAKE_CXX_COMPILER=clang++',
    '-DCMAKE_C_FLAGS=-msse4.1', '-DCMAKE_CXX_FLAGS=-msse4.1', '-DCMAKE_CXX_STANDARD=23',
    '-DREXGLUE_USE_VULKAN=ON', '-DREXGLUE_ENABLE_TRACY=OFF', '-DSDL_HIDAPI_LIBUSB=OFF',
    "-DPYTHON_EXECUTABLE=$(Get-Fm4Python)", "-DREXGLUE_DLSS_SDK_DIR=$($dlss.Replace('\', '/'))",
    "-DCMAKE_INSTALL_PREFIX=$installPrefix",
    "-DCMAKE_PROJECT_ReXGlue_INCLUDE=$((Join-Path $root 'cmake/Fm4SdkBundle.cmake').Replace('\', '/'))") +
    (Get-Fm4CMakeToolArguments -Environment $environment)) (Join-Path $logs 'configure.log') 'SDK configuration failed.'
Invoke-Fm4BuildCommand $environment.CMake @('--build', $build, '--parallel', "$Parallel") `
    (Join-Path $logs 'build.log') 'SDK build failed.'
Invoke-Fm4BuildCommand $environment.CMake @('--install', $build) (Join-Path $logs 'install.log') 'SDK install failed.'
# The SDK's install registers its prefix in the CMake user package registry; this one is scratch.
$registry = 'HKCU:\Software\Kitware\CMake\Packages\rexglue'
if (Test-Path -LiteralPath $registry) {
    $key = Get-Item -LiteralPath $registry
    foreach ($name in $key.GetValueNames()) {
        if ("$($key.GetValue($name))".Replace('\', '/') -ieq $installPrefix) { Remove-ItemProperty -LiteralPath $registry -Name $name }
    }
}

$top = "shiftglue-sdk-$($config.sdk.revision.Substring(0, 12))-windows-x64"
$stage = Join-Path $work "stage/$top"
[void](New-Item -ItemType Directory -Force -Path $stage)
Get-ChildItem -LiteralPath $install | Copy-Item -Destination $stage -Recurse
[void](New-Item -ItemType Directory -Force -Path (Join-Path $stage 'cmake'))
foreach ($pins in 'rexglue_dlss.cmake', 'rexglue_ffx_api_headers.cmake') {
    Copy-Item -LiteralPath (Join-Path $sdkRoot "cmake/$pins") -Destination (Join-Path $stage 'cmake')
}
# The installed rexglue_helpers.cmake stages AMD's licence beside the FidelityFX runtime from
# ../thirdparty relative to itself, a path only the source tree has.
$ffxLicence = Join-Path $stage 'lib/cmake/thirdparty/fidelityfx-vk'
[void](New-Item -ItemType Directory -Force -Path $ffxLicence)
Copy-Item -LiteralPath (Join-Path $sdkRoot 'thirdparty/fidelityfx-vk/LICENSE.txt') -Destination $ffxLicence
$licences = Join-Path $stage 'licenses'
[void](New-Item -ItemType Directory -Force -Path (Join-Path $licences 'shiftglue-sdk'), (Join-Path $licences 'nvidia-dlss'))
Copy-Item -LiteralPath (Join-Path $sdkRoot 'LICENSE') -Destination (Join-Path $licences 'shiftglue-sdk')
Copy-Item -LiteralPath (Join-Path $dlss 'LICENSE.txt') -Destination (Join-Path $licences 'nvidia-dlss')
foreach ($library in Get-ChildItem -LiteralPath (Join-Path $sdkRoot 'thirdparty') -Directory) {
    $texts = @(Get-ChildItem -LiteralPath $library.FullName -File |
        Where-Object { $_.Name -match '^(LICEN[CS]E|COPYING|NOTICE)' })
    if ($texts.Count -eq 0) { continue }
    $destination = Join-Path $licences $library.Name
    [void](New-Item -ItemType Directory -Force -Path $destination)
    $texts | Copy-Item -Destination $destination
}
# The FSR frame-generation host carries a pinned AMD SDK subset whose top-level
# text does not repeat AMD's licence.
$fsrLicences = Join-Path $licences 'fm4-fsr-vulkan'
[void](New-Item -ItemType Directory -Force -Path $fsrLicences)
Copy-Item -LiteralPath (Join-Path $sdkRoot 'thirdparty/fm4-fsr-vulkan/upstream/ffx-1.1.4/sdk/LICENSE.txt') `
    -Destination (Join-Path $fsrLicences 'AMD-FidelityFX-1.1.4-LICENSE.txt')
# Licence texts the loop above misses: libmspack keeps its own a folder deeper, and disasm (GNU
# binutils' PowerPC disassembler, GPL 2 or later, in rexglue.exe) carries none; FFmpeg's copy of
# the GPL 2 is the same text.
foreach ($licence in @(
    @{ Library = 'libmspack'; Text = 'thirdparty/libmspack/libmspack/COPYING.LIB' },
    @{ Library = 'disasm'; Text = 'thirdparty/FFmpeg/COPYING.GPLv2' }
)) {
    $destination = Join-Path $licences $licence.Library
    [void](New-Item -ItemType Directory -Force -Path $destination)
    Copy-Item -LiteralPath (Join-Path $sdkRoot $licence.Text) -Destination $destination
}
$sources = "shiftglue-sdk-$($config.sdk.revision.Substring(0, 12))-sources"
$readme = @(
    'The ShiftGlue SDK prebuilt for FM4 Static Recomp: the SDK''s install (headers, libraries and the'
    "rexglue code generator) built from $($config.sdk.repository)"
    "at $($config.sdk.revision) and the commits its submodules pin, with LLVM $($config.llvm.version),"
    'by tools/package-sdk.ps1 of https://github.com/i2kiselev/fm4-static-recomp. It holds no game code.'
    ''
    'The licences of the SDK and of the libraries built into it are in licenses/. FFmpeg and libmspack'
    'are under the LGPL 2.1; bin/rexglue.exe and lib/disasm.lib hold GNU binutils'' PowerPC disassembler'
    'under the GPL 2 or later. The FSR 3.1.6 Vulkan frame-generation host and its MIT-licensed'
    'FidelityFX source subset are under licenses/fm4-fsr-vulkan. Their complete source is published'
    'beside this archive: the release''s'
    "Source code archive (the SDK at $($config.sdk.revision)) and $sources.tar.xz"
    '(every submodule at its pinned commit). cmake/ holds the SDK''s pins of the DLSS and FidelityFX'
    'runtimes, which the build fetches from NVIDIA''s and AMD''s repositories.'
)
[IO.File]::WriteAllText((Join-Path $stage 'README-FM4.txt'), ($readme -join "`n") + "`n", [Text.UTF8Encoding]::new($false))

$archive = Join-Path $work "$top.tar.xz"
& (Get-Fm4Python) -I (Join-Path $PSScriptRoot 'tar_xz.py') $stage $archive
if ($LASTEXITCODE -ne 0) { throw 'Packing the SDK failed.' }

$notes = Join-Path $work 'sources-README-FM4.txt'
[IO.File]::WriteAllText($notes, (@(
    "The source of every submodule of the ShiftGlue SDK at $($config.sdk.revision)"
    "($($config.sdk.repository)), at the commits that revision pins, by tools/package-sdk.ps1 of"
    'https://github.com/i2kiselev/fm4-static-recomp. Unpacked over the SDK''s own source (this release''s'
    "Source code archive), it is the complete source $top.tar.xz was built from."
) -join "`n") + "`n", [Text.UTF8Encoding]::new($false))
$submodules = @(& $git -C $sdkRoot submodule status --recursive | ForEach-Object { $_.Substring(1).Split(' ')[1] })
$sourcesArchive = Join-Path $work "$sources.tar.xz"
& (Get-Fm4Python) -I (Join-Path $PSScriptRoot 'tar_xz.py') --git $sourcesArchive $sources $notes `
    @($submodules | ForEach-Object { "$_=$(Join-Path $sdkRoot $_)" })
if ($LASTEXITCODE -ne 0) { throw 'Packing the SDK''s submodule sources failed.' }

foreach ($result in $archive, $sourcesArchive) {
    [pscustomobject]@{
        archive = $result
        size = (Get-Item -LiteralPath $result).Length
        sha256 = (Get-FileHash -LiteralPath $result -Algorithm SHA256).Hash
    }
}
