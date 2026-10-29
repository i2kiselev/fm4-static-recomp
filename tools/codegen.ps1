[CmdletBinding()]
param(
    [ValidateRange(0, 32)] [int]$Parallel = 0,
    [switch]$SkipSdkBuild,
    # The extracted Disc 1 (the folder holding default.xex).
    [string]$GameRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (Test-Path variable:PSNativeCommandUseErrorActionPreference) {
    $PSNativeCommandUseErrorActionPreference = $false
}

. (Join-Path $PSScriptRoot 'release-common.ps1')
$root = Get-Fm4RepoRoot
if (-not $GameRoot) { $GameRoot = [IO.Path]::GetFullPath((Join-Path $root '../extracted/fm4')) }

$environment = Enter-Fm4BuildEnvironment
$sdkRoot = Resolve-Fm4SdkRoot
$bundle = Get-Fm4SdkBundle
$rexglueExe = if ($bundle) { Join-Path $bundle 'bin/rexglue.exe' } else { Join-Path $sdkRoot 'out/win-amd64/Release/rexglue.exe' }
$logs = Join-Path $root '.local/logs'
[void](New-Item -ItemType Directory -Force -Path $logs)
if ($Parallel -eq 0) { $Parallel = Get-Fm4BuildJobCount }

$game = [IO.Path]::GetFullPath($GameRoot).Replace('\', '/')
if (-not (Test-Path -LiteralPath (Join-Path $game 'default.xex') -PathType Leaf)) {
    throw "default.xex is missing in $GameRoot"
}
$tracked = Join-Path $root 'config/rexglue/fm4_manifest.toml'
$manifest = Join-Path $root 'config/rexglue/fm4_manifest.local.toml'
# The launcher runs setup under Windows PowerShell 5.1 when PowerShell 7 is absent, whose
# Set-Content has no BOM-less UTF-8.
[IO.File]::WriteAllText($manifest,
    (Get-Content -LiteralPath $tracked -Raw).Replace('../../../extracted/fm4', $game),
    [Text.UTF8Encoding]::new($false))

if (-not $SkipSdkBuild -and -not $bundle) {
    Push-Location -LiteralPath $sdkRoot
    try {
        Invoke-Fm4BuildCommand $environment.CMake (@('--preset', 'win-amd64',
            '-DREXGLUE_ENABLE_TRACY=OFF', '-DSDL_HIDAPI_LIBUSB=OFF') +
            (Get-Fm4CMakeToolArguments -Environment $environment)) `
            (Join-Path $logs 'rexglue-configure.log') 'ReXGlue configuration failed.'
        Invoke-Fm4BuildCommand $environment.CMake @('--build', '--preset', 'win-amd64-release',
            '--target', 'rexglue', '--parallel', "$Parallel") `
            (Join-Path $logs 'rexglue-build.log') 'ReXGlue code-generator build failed.'
    }
    finally { Pop-Location }
}
if (-not (Test-Path -LiteralPath $rexglueExe -PathType Leaf)) {
    throw "The ReXGlue code generator was not produced: $rexglueExe"
}

$codegenLog = Join-Path $logs 'codegen.log'
if (Test-Path -LiteralPath $codegenLog) { Remove-Item -LiteralPath $codegenLog -Force }
& $rexglueExe --log-level info --log-file $codegenLog codegen $manifest
"codegen exit code: $LASTEXITCODE"
exit $LASTEXITCODE
