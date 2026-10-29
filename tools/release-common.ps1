# Adapted from Pinyon Shift's tools/release-common.ps1 (BSD-3, see THIRD_PARTY_NOTICES.md).
Set-StrictMode -Version Latest

function Get-Fm4RepoRoot {
    [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
}

function Get-Fm4ReleaseToolchain {
    $root = Get-Fm4RepoRoot
    Get-Content -LiteralPath (Join-Path $root 'config/release-toolchain.json') -Raw |
        ConvertFrom-Json
}

function Write-Fm4Event {
    param(
        [Parameter(Mandatory)] [string]$Stage,
        [Parameter(Mandatory)] [ValidateRange(0, 100)] [int]$Percent,
        [Parameter(Mandatory)] [string]$Message,
        [switch]$JsonEvents
    )
    if ($JsonEvents) {
        $event = [ordered]@{ stage = $Stage; percent = $Percent; message = $Message }
        Write-Output ('::fm4::' + ($event | ConvertTo-Json -Compress))
    }
    else {
        Write-Host "[$($Stage.ToUpperInvariant())] $Message"
    }
}

function Resolve-Fm4LocalPath {
    param([Parameter(Mandatory)] [string]$RelativePath)
    $root = Get-Fm4RepoRoot
    $full = [IO.Path]::GetFullPath((Join-Path $root $RelativePath))
    $local = [IO.Path]::GetFullPath((Join-Path $root '.local'))
    $prefix = $local.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if (-not $full.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to use a release-work path outside $local"
    }
    $full
}

function Get-Fm4Python {
    $config = Get-Fm4ReleaseToolchain
    $pythonRoot = Resolve-Fm4ToolchainPath -RelativePath $config.python.install_path
    $pythonExe = Join-Path $pythonRoot $config.python.executable
    if (-not (Test-Path -LiteralPath $pythonExe -PathType Leaf)) {
        throw 'The local Python runtime is missing. Run tools/provision-toolchain.ps1 first.'
    }
    $pythonExe
}

# FM4_TOOLCHAIN_ROOT (the launcher's data\toolchain) holds every tool. A development checkout
# beside pinyon-shift reuses an item Pinyon already has (LLVM is 4 GB) and keeps its own copy of
# the rest, item by item, so the Microsoft files and Ninja never land in Pinyon's folder.
function Resolve-Fm4ToolchainPath {
    param([Parameter(Mandatory)] [string]$RelativePath)
    if ($env:FM4_TOOLCHAIN_ROOT) {
        return [IO.Path]::GetFullPath((Join-Path ([IO.Path]::GetFullPath($env:FM4_TOOLCHAIN_ROOT)) $RelativePath))
    }
    $root = Get-Fm4RepoRoot
    $own = [IO.Path]::GetFullPath((Join-Path $root ".local/toolchain/$RelativePath"))
    $sibling = [IO.Path]::GetFullPath((Join-Path $root "../pinyon-shift/.local/toolchain/$RelativePath"))
    if (-not (Test-Path -LiteralPath $own) -and (Test-Path -LiteralPath $sibling)) { return $sibling }
    $own
}

# Two CMake limits, both still in CMake 4.1: its compiler detection globs its own install folder
# unescaped (CMakeCompilerIdDetection.cmake), so below a folder holding [ or ] it finds no
# compiler; and it writes a tool path with non-English letters in its 8.3 short form, unquoted,
# into cmd /C link commands, where parentheses that the short form keeps break the command.
# Cyrillic letters, spaces, and parentheses in an English-only path work.
function Assert-Fm4ToolchainPathSupported {
    $cmakeRoot = Resolve-Fm4ToolchainPath -RelativePath (Get-Fm4ReleaseToolchain).cmake.install_path
    $toolchain = Split-Path $cmakeRoot -Parent
    $reason = $null
    if ($toolchain.IndexOfAny([char[]]'[]') -ge 0) {
        $reason = 'CMake, which the build needs, finds no compiler when its folder path holds [ or ]. Move the ' +
            'installation to a folder whose path has no square brackets.'
    }
    elseif ($toolchain -match '[^\x00-\x7F]' -and $toolchain.IndexOfAny([char[]]'()') -ge 0) {
        $reason = 'CMake, which the build needs, cannot run its link commands from a path that holds both ' +
            'non-English letters and parentheses. Move the installation to a folder whose path has no parentheses.'
    }
    if ($reason) {
        $failure = [Exception]::new("The build tools cannot run from $toolchain`: $reason")
        $failure.Data['step'] = 'tools'
        throw $failure
    }
}

function Get-Fm4SiblingSdkRoot {
    [IO.Path]::GetFullPath((Join-Path (Get-Fm4RepoRoot) '../pinyon-shift/thirdparty/shiftglue-sdk'))
}

function Resolve-Fm4SdkRoot {
    if ($env:FM4_SDK_ROOT) { return [IO.Path]::GetFullPath($env:FM4_SDK_ROOT) }
    $root = Get-Fm4RepoRoot
    $submodule = Join-Path $root 'thirdparty/shiftglue-sdk'
    if (Test-Path -LiteralPath (Join-Path $submodule 'CMakeLists.txt')) { return $submodule }
    # Only this development tree (a git checkout) uses the sibling; a payload has no .git.
    $sibling = Get-Fm4SiblingSdkRoot
    if ((Test-Path -LiteralPath (Join-Path $root '.git')) -and
        (Test-Path -LiteralPath (Join-Path $sibling 'CMakeLists.txt'))) {
        return $sibling
    }
    Resolve-Fm4LocalPath -RelativePath (Get-Fm4ReleaseToolchain).sdk.fallback_path
}

# The prebuilt SDK (tools/package-sdk.ps1) a payload builds against where it would otherwise
# check the pinned SDK source out itself; $null when an SDK source tree is in use.
function Get-Fm4SdkBundle {
    $config = Get-Fm4ReleaseToolchain
    if (-not $config.sdk.PSObject.Properties['bundle']) { return $null }
    $managedRoot = Resolve-Fm4LocalPath -RelativePath $config.sdk.fallback_path
    if (-not [string]::Equals((Resolve-Fm4SdkRoot), $managedRoot, [StringComparison]::OrdinalIgnoreCase)) {
        return $null
    }
    Resolve-Fm4LocalPath -RelativePath $config.sdk.bundle.install_path
}

function Invoke-Fm4Download {
    param(
        [Parameter(Mandatory)] [string]$Uri,
        [Parameter(Mandatory)] [string]$Destination,
        [Parameter(Mandatory)] [string]$Sha256
    )
    $parent = Split-Path $Destination -Parent
    [void](New-Item -ItemType Directory -Force -Path $parent)
    if (Test-Path -LiteralPath $Destination -PathType Leaf) {
        $actual = (Get-FileHash -LiteralPath $Destination -Algorithm SHA256).Hash
        if ($actual -eq $Sha256) { return }
        Remove-Item -LiteralPath $Destination -Force
    }
    $partial = "$Destination.partial"
    if (Test-Path -LiteralPath $partial) { Remove-Item -LiteralPath $partial -Force }
    try {
        $curl = Get-Command curl.exe -ErrorAction SilentlyContinue
        if ($null -ne $curl) {
            & $curl.Source --fail --location --retry 3 --output $partial $Uri
            if ($LASTEXITCODE -ne 0) { throw "Download failed: $Uri" }
        }
        else {
            Invoke-WebRequest -Uri $Uri -OutFile $partial -UseBasicParsing
        }
        $actual = (Get-FileHash -LiteralPath $partial -Algorithm SHA256).Hash
        if ($actual -ne $Sha256) {
            throw "Downloaded file failed SHA-256 verification. Expected $Sha256; got $actual."
        }
        Move-Item -LiteralPath $partial -Destination $Destination
    }
    finally {
        if (Test-Path -LiteralPath $partial) { Remove-Item -LiteralPath $partial -Force }
    }
}

# Through the toolchain's Python, in parallel ranged parts: GitHub and other far-away hosts cap
# each connection's speed on some players' lines (84 KB/s on one connection, 559 KB/s on eight).
function Invoke-Fm4ParallelDownload {
    param(
        [Parameter(Mandatory)] [string]$Uri,
        [Parameter(Mandatory)] [string]$Destination,
        [Parameter(Mandatory)] [string]$Sha256,
        [Parameter(Mandatory)] [string]$What,
        [Parameter(Mandatory)] [int]$Percent,
        [Parameter(Mandatory)] [int]$EndPercent,
        [string]$Stage = 'tools',
        [switch]$JsonEvents
    )
    $run = Invoke-Fm4ToolchainPython -JsonEvents:$JsonEvents -Arguments @('download', '--url', $Uri,
        '--sha256', $Sha256, '--destination', $Destination, '--what', $What, '--stage', $Stage,
        '--percent', "$Percent", "$EndPercent", '--json-events')
    if ($run.ExitCode -ne 0) {
        $failure = [Exception]::new("Downloading $What failed: $(Get-Fm4ToolchainRunMessage -Run $run)")
        $failure.Data['step'] = $Stage
        throw $failure
    }
}

# Expand-Archive checks an existing destination with wildcards, so a folder named with brackets
# breaks it in both PowerShell editions. Existing files are replaced. The archive is deleted once
# unpacked: provisioning reads it only while its tool is missing, and a new pin has a new name.
function Expand-Fm4Zip {
    param(
        [Parameter(Mandatory)] [string]$Archive,
        [Parameter(Mandatory)] [string]$Destination
    )
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $destination = [IO.Path]::GetFullPath($Destination)
    $prefix = $destination.TrimEnd('\') + '\'
    $zip = [IO.Compression.ZipFile]::OpenRead([IO.Path]::GetFullPath($Archive))
    try {
        foreach ($entry in $zip.Entries) {
            $target = [IO.Path]::GetFullPath((Join-Path $destination $entry.FullName))
            if (-not $target.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
                throw "The archive member $($entry.FullName) leaves $destination."
            }
            if ($entry.FullName.EndsWith('/')) {
                [void][IO.Directory]::CreateDirectory($target)
                continue
            }
            [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target))
            [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $target, $true)
        }
    }
    finally { $zip.Dispose() }
    Remove-Item -LiteralPath $Archive -Force
}

# Deletes the archive once unpacked, as Expand-Fm4Zip does.

function Expand-Fm4TarXz {
    param(
        [Parameter(Mandatory)] [string]$Archive,
        [Parameter(Mandatory)] [string]$Destination,
        [Parameter(Mandatory)] [string]$XzPath
    )
    $archive = [IO.Path]::GetFullPath($Archive)
    $destination = [IO.Path]::GetFullPath($Destination)
    $xzPath = [IO.Path]::GetFullPath($XzPath)
    $tarArchive = Join-Path (Split-Path $archive -Parent) `
        ([IO.Path]::GetFileNameWithoutExtension($archive))
    if (-not $archive.EndsWith('.tar.xz', [StringComparison]::OrdinalIgnoreCase)) {
        throw "Expected a .tar.xz archive: $archive"
    }
    if (-not (Test-Path -LiteralPath $xzPath -PathType Leaf)) {
        throw "The pinned XZ extractor is missing: $xzPath"
    }
    if (Test-Path -LiteralPath $tarArchive) {
        Remove-Item -LiteralPath $tarArchive -Force
    }
    try {
        & $xzPath --decompress --keep --force -- $archive
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $tarArchive -PathType Leaf)) {
            throw "Unable to decompress $archive."
        }
        # Windows' own bsdtar by path: a GNU tar earlier in PATH (Git's optional Unix
        # tools, MSYS2) reads "C:\..." as a remote host and fails to extract.
        $tarExe = Join-Path ([Environment]::SystemDirectory) 'tar.exe'
        if (-not (Test-Path -LiteralPath $tarExe -PathType Leaf)) { $tarExe = 'tar.exe' }
        & $tarExe -xf $tarArchive -C $destination
        if ($LASTEXITCODE -ne 0) { throw "Unable to extract $archive." }
    }
    finally {
        if (Test-Path -LiteralPath $tarArchive) {
            Remove-Item -LiteralPath $tarArchive -Force
        }
    }
    Remove-Item -LiteralPath $archive -Force
}

function Get-Fm4VisualStudioRoot {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) { return @() }
    @(& $vswhere -products * -property installationPath | Where-Object { $_ })
}

function Get-Fm4NewestFolder {
    param([Parameter(Mandatory)] [string]$Parent, [Parameter(Mandatory)] [string]$Filter, [string]$Contains)
    @(Get-ChildItem -LiteralPath $Parent -Directory -Filter $Filter -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^[\d.]+$' -and
            (-not $Contains -or (Test-Path -LiteralPath (Join-Path $_.FullName $Contains))) } |
        Sort-Object { [version]$_.Name } -Descending) | Select-Object -First 1
}

# The fallback: the pinned toolset line where Microsoft's installer put it (an installed Visual
# Studio or Build Tools, side by side or as its default toolset) and the Windows SDK at the pinned
# folder version. Found by folder, not by component, so either way of installing counts.
function Get-Fm4VisualStudioToolset {
    $config = Get-Fm4ReleaseToolchain
    $kits = Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots' -Name KitsRoot10 `
        -ErrorAction SilentlyContinue
    if ($null -eq $kits) { return $null }
    $sdkVersion = $config.windows_sdk.sdk_folder_version
    $sdkBin = Join-Path $kits.KitsRoot10 "bin\$sdkVersion\x64"
    $sdkInclude = @('ucrt', 'um', 'shared', 'winrt', 'cppwinrt') |
        ForEach-Object { Join-Path $kits.KitsRoot10 "Include\$sdkVersion\$_" }
    $sdkLib = @('ucrt', 'um') | ForEach-Object { Join-Path $kits.KitsRoot10 "Lib\$sdkVersion\$_\x64" }
    foreach ($required in @((Join-Path $sdkBin 'rc.exe'), (Join-Path $sdkBin 'mt.exe'), $sdkInclude[0], $sdkLib[1])) {
        if (-not (Test-Path -LiteralPath $required)) { return $null }
    }
    $line = $config.msvc.toolset_line
    foreach ($vsRoot in Get-Fm4VisualStudioRoot) {
        $tools = Get-Fm4NewestFolder -Parent (Join-Path $vsRoot 'VC\Tools\MSVC') -Filter "$line.*" `
            -Contains 'lib\x64\msvcprt.lib'
        $redistParent = Join-Path $vsRoot 'VC\Redist\MSVC'
        # A newer redistributable runs an older toolset's build; the line's own is preferred.
        $redist = Get-Fm4NewestFolder -Parent $redistParent -Filter "$line.*" -Contains 'x64\Microsoft.VC143.CRT'
        if ($null -eq $redist) {
            $redist = Get-Fm4NewestFolder -Parent $redistParent -Filter '14.*' -Contains 'x64\Microsoft.VC143.CRT'
        }
        if ($null -eq $tools -or $null -eq $redist) { continue }
        return [pscustomobject]@{
            Source = 'visual-studio'
            ToolsetVersion = $tools.Name
            MsvcInclude = Join-Path $tools.FullName 'include'
            MsvcLib = Join-Path $tools.FullName 'lib\x64'
            Redist = Join-Path $redist.FullName 'x64\Microsoft.VC143.CRT'
            SdkInclude = [string[]]$sdkInclude
            SdkLib = [string[]]$sdkLib
            Rc = Join-Path $sdkBin 'rc.exe'
            Mt = Join-Path $sdkBin 'mt.exe'
        }
    }
    $null
}

# Runs tools\toolchain.py. Its ::fm4:: progress lines go to the host as they come (as events, or
# as plain lines without -JsonEvents); its last stdout line is the JSON result.
function Invoke-Fm4ToolchainPython {
    param([Parameter(Mandatory)] [string[]]$Arguments, [switch]$JsonEvents)
    $ErrorActionPreference = 'Continue'
    $python = Get-Fm4Python
    $previousEncoding = $env:PYTHONIOENCODING
    $env:PYTHONIOENCODING = 'utf-8'
    # PowerShell decodes a native command's output with the console's code page (OEM 866 on a
    # Russian Windows); Python writes UTF-8, so a Cyrillic folder would come back mangled.
    $previousConsole = [Console]::OutputEncoding
    [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
    $last = $null
    try {
        & $python -I (Join-Path $PSScriptRoot 'toolchain.py') @Arguments 2>&1 | ForEach-Object {
            $line = "$_"
            if ($_ -is [Management.Automation.ErrorRecord]) { Write-Host $line }
            elseif ($line.StartsWith('::fm4::')) {
                if ($JsonEvents) { Write-Host $line }
                else {
                    $event = $line.Substring(7) | ConvertFrom-Json
                    Write-Host "[$($event.stage.ToUpperInvariant())] $($event.message)"
                }
            }
            elseif ($line) { $last = $line }
        }
        $code = $LASTEXITCODE
    }
    finally {
        $env:PYTHONIOENCODING = $previousEncoding
        [Console]::OutputEncoding = $previousConsole
    }
    $result = if ($last) { $last | ConvertFrom-Json } else { $null }
    [pscustomobject]@{ ExitCode = $code; Result = $result }
}

# A failed run's reason: its JSON message, or its exit code when it left none (a crash, a kill).
function Get-Fm4ToolchainRunMessage {
    param([Parameter(Mandatory)] $Run)
    if ($null -ne $Run.Result -and $Run.Result.PSObject.Properties['message']) { return $Run.Result.message }
    "exit code $($Run.ExitCode)"
}

# The Build Tools licence from Microsoft's channel, read without the toolchain's Python: the
# launcher shows it before setup has provisioned anything.
function Get-Fm4BuildToolsLicence {
    param([Parameter(Mandatory)] [string]$ChannelUrl)
    $response = Invoke-WebRequest -Uri $ChannelUrl -UseBasicParsing -UserAgent 'fm4-static-recomp-setup'
    $bytes = $response.RawContentStream.ToArray()
    $channel = [Text.Encoding]::UTF8.GetString($bytes).TrimStart([char]0xFEFF) | ConvertFrom-Json
    $product = @($channel.channelItems | Where-Object { $_.id -eq 'Microsoft.VisualStudio.Product.BuildTools' })
    $resources = @(if ($product.Count -gt 0 -and $product[0].PSObject.Properties['localizedResources']) {
        $product[0].localizedResources
    })
    $withLicence = @($resources | Where-Object { $_.PSObject.Properties['license'] -and $_.license })
    $licence = @($withLicence | Where-Object { $_.PSObject.Properties['language'] -and $_.language -eq 'en-us' }) +
        $withLicence | Select-Object -First 1
    if ($null -eq $licence) { throw "Microsoft's channel names no Build Tools licence: $ChannelUrl" }
    $licence.license
}

function Get-Fm4MicrosoftToolchainPaths {
    $config = Get-Fm4ReleaseToolchain
    # Resolved by the SDK's own folder: the toolchain root itself ('.') would find the sibling's.
    $sdkFolder = Resolve-Fm4ToolchainPath -RelativePath "windows-sdk-$($config.windows_sdk.version)"
    [pscustomobject]@{
        Line = $config.msvc.toolset_line
        MsvcRoot = Resolve-Fm4ToolchainPath -RelativePath $config.msvc.install_path
        SdkVersion = $config.windows_sdk.version
        SdkRoot = Split-Path $sdkFolder -Parent
    }
}

# The private copy (toolchain.py find) first, else the fallback; $null when neither is there.
function Get-Fm4BuildToolset {
    $paths = Get-Fm4MicrosoftToolchainPaths
    $found = Invoke-Fm4ToolchainPython -Arguments @('find', '--line', $paths.Line, '--msvc-root', $paths.MsvcRoot,
        '--sdk-version', $paths.SdkVersion, '--sdk-root', $paths.SdkRoot)
    if ($found.ExitCode -ne 0) { return Get-Fm4VisualStudioToolset }
    $msvc = $found.Result.msvc
    $sdk = $found.Result.windows_sdk
    [pscustomobject]@{
        Source = 'feed'
        ToolsetVersion = $msvc.toolset_version
        MsvcInclude = $msvc.include
        MsvcLib = $msvc.lib
        Redist = $msvc.redist
        SdkInclude = [string[]]$sdk.include
        SdkLib = [string[]]$sdk.lib
        Rc = $sdk.rc
        Mt = $sdk.mt
    }
}

# Replaces, never extends, what a Visual Studio developer prompt may have set: its INCLUDE, LIB
# and EXTERNAL_INCLUDE would mix another toolset's files into the build. VCToolsInstallDir keeps
# clang from looking for an installed Visual Studio at all.
function Get-Fm4BuildVariables {
    param(
        [Parameter(Mandatory)] $Toolset,
        [Parameter(Mandatory)] [string]$LlvmBin,
        [Parameter(Mandatory)] [string]$NinjaDir,
        [Parameter(Mandatory)] [string]$CMakeBin,
        [AllowEmptyString()] [string]$InheritedPath = ''
    )
    $ours = @($LlvmBin, $NinjaDir, $CMakeBin, (Split-Path $Toolset.Rc -Parent))
    $inherited = @($InheritedPath -split ';' | Where-Object { $_ -and $_ -notin $ours })
    [ordered]@{
        INCLUDE = (@($Toolset.MsvcInclude) + @($Toolset.SdkInclude)) -join ';'
        LIB = (@($Toolset.MsvcLib) + @($Toolset.SdkLib)) -join ';'
        PATH = ($ours + $inherited) -join ';'
        EXTERNAL_INCLUDE = ''
        VCToolsInstallDir = (Split-Path $Toolset.MsvcInclude -Parent) + '\'
    }
}

# clang takes the MSVC version (_MSC_VER) from the target triple; without one it would ask a
# cl.exe, which the private copy does not have, and fall back to 19.33.
function Get-Fm4CMakeToolArguments {
    param([Parameter(Mandatory)] $Environment)
    $target = 'x86_64-pc-windows-msvc' + ($Environment.Toolset.ToolsetVersion -replace '^14\.', '19.')
    @(
        "-DCMAKE_MAKE_PROGRAM=$($Environment.Ninja.Replace('\', '/'))",
        '-DCMAKE_LINKER_TYPE=LLD',
        "-DCMAKE_RC_COMPILER=$($Environment.Toolset.Rc.Replace('\', '/'))",
        "-DCMAKE_MT=$($Environment.Toolset.Mt.Replace('\', '/'))",
        "-DCMAKE_C_COMPILER_TARGET=$target",
        "-DCMAKE_CXX_COMPILER_TARGET=$target"
    )
}

function Get-Fm4Ninja {
    $config = Get-Fm4ReleaseToolchain
    $ninja = Join-Path (Resolve-Fm4ToolchainPath -RelativePath $config.ninja.install_path) $config.ninja.executable
    if (-not (Test-Path -LiteralPath $ninja -PathType Leaf)) {
        throw 'Ninja is missing. Run tools/provision-toolchain.ps1 first.'
    }
    $ninja
}

function Get-Fm4CMake {
    $config = Get-Fm4ReleaseToolchain
    $candidate = Join-Path (Resolve-Fm4ToolchainPath -RelativePath $config.cmake.install_path) $config.cmake.executable
    if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
        throw 'CMake is missing. Run tools/provision-toolchain.ps1 first.'
    }
    $candidate
}

# One argument in double quotes for a Windows command line: backslashes before the closing quote
# are doubled, or a folder ending in `\` would escape it and swallow the next argument.
function ConvertTo-Fm4QuotedArgument {
    param([Parameter(Mandatory)] [string]$Value)
    '"' + ($Value -replace '(\\+)$', '$1$1') + '"'
}

function Format-Fm4CommandLine {
    param(
        [Parameter(Mandatory)] [string]$FilePath,
        [AllowEmptyCollection()] [string[]]$Arguments = @()
    )
    $parts = foreach ($part in @($FilePath) + @($Arguments)) {
        if ($part -match '[\s"]' -or $part.Length -eq 0) { '"' + ($part -replace '"', '\"') + '"' }
        else { $part }
    }
    $parts -join ' '
}

# Runs a native command, streams its stdout and stderr in arrival order and
# writes the same lines to a UTF-8 log. The caller reads $LASTEXITCODE.
function Invoke-Fm4LoggedCommand {
    param(
        [Parameter(Mandatory)] [string]$FilePath,
        [Parameter(Mandatory)] [AllowEmptyCollection()] [string[]]$Arguments,
        [Parameter(Mandatory)] [string]$LogPath,
        [switch]$Append
    )
    # Windows PowerShell turns redirected native stderr into ErrorRecord
    # objects. Keep streaming them as plain text and decide success from the
    # process exit code, never from stderr.
    $ErrorActionPreference = 'Continue'
    $parent = Split-Path $LogPath -Parent
    if ($parent) { [void](New-Item -ItemType Directory -Force -Path $parent) }
    $writer = [IO.StreamWriter]::new($LogPath, [bool]$Append, [Text.UTF8Encoding]::new($false))
    $writer.AutoFlush = $true
    try {
        $writer.WriteLine("> $(Format-Fm4CommandLine -FilePath $FilePath -Arguments $Arguments)")
        if ($null -eq (Get-Command -Name $FilePath -ErrorAction SilentlyContinue)) {
            # With stderr merged, a missing program would only be a log line
            # and $LASTEXITCODE would keep an older value.
            $line = "The program could not be started because it was not found: $FilePath"
            $writer.WriteLine($line)
            $writer.WriteLine('> exit code -1')
            $line
            $global:LASTEXITCODE = -1
            return
        }
        & $FilePath @Arguments 2>&1 | ForEach-Object {
            $line = if ($_ -is [Management.Automation.ErrorRecord]) {
                # ToString() of an empty stderr line is the exception type
                # name, so read the line itself.
                if ($_.TargetObject -is [string]) { $_.TargetObject }
                elseif ($null -ne $_.Exception) { [string]$_.Exception.Message }
                else { $_.ToString() }
            }
            else { [string]$_ }
            $writer.WriteLine($line)
            $line
        }
        $code = $LASTEXITCODE
        $writer.WriteLine("> exit code $code")
    }
    finally { $writer.Dispose() }
    $global:LASTEXITCODE = $code
}

# Plain-language causes for failures whose fix is outside the build itself.
function Get-Fm4FailureHint {
    param([AllowEmptyCollection()] [string[]]$Lines = @())
    $text = $Lines -join "`n"
    if ($text -match '(?i)No space left on device|not enough space on the disk|There is not enough space|disk (is )?full|LNK1180|ENOSPC') {
        return 'The drive ran out of free space. Free at least 30 GB on the drive that holds FM4 Static Recomp, then run setup again.'
    }
    if ($text -match '(?i)out of memory|bad_alloc|not enough memory|Allocation failed|paging file is too small|insufficient system resources|0xC0000017|0xC000012D') {
        return 'The compiler ran out of memory. Close other programs, or let Windows manage the paging file size, then run setup again; it resumes where it stopped.'
    }
    if ($text -match '(?i)contains a virus|potentially unwanted software|Operation did not complete successfully because the file') {
        return 'Antivirus blocked a file the build produced. Allow the FM4 Static Recomp folder in your antivirus, then run setup again.'
    }
    if ($text -match '(?i)Error copying file|is being used by another process|Access is denied|Permission denied|LNK1104|LNK1168|cannot open (output )?file|failed to write the output file|unable to (open|remove) .* for writing|Could not replace .* beside|open in another program') {
        return 'A file could not be written because another program has it open. Close FM4 Static Recomp and any tool started from its out\build folder, wait for antivirus scanning to finish (or allow the FM4 Static Recomp folder in your antivirus), then run setup again.'
    }
    if ($text -match '(?i)PLEASE submit a bug report|frontend command failed due to signal|clang.*crashed|Exception Code: 0x|Stack dump:') {
        return 'The compiler process crashed. This is most often caused by running out of memory or by antivirus stopping the compiler. Close other programs and run setup again; it resumes where it stopped.'
    }
    if ($text -match '(?i)No CMAKE_(C|CXX)_COMPILER could be found|is not a full path to an existing compiler|Could not find any instance of Visual Studio|Unable to initialize the Microsoft x64 build environment|vcvarsall|The C(XX)? compiler identification is unknown') {
        return 'The C++ build tools are missing or incomplete. Restart Windows after installing the Microsoft C++ Build Tools, then run setup again.'
    }
    $null
}

# Finds the lines that explain a failed native command in its complete log:
# the first Ninja FAILED: block, CMake Error blocks, ninja: error lines, or
# error-looking lines near the end. In a parallel build the first failure is
# usually followed by the output of every job that was still running, so the
# tail of the log alone does not identify it.
function Get-Fm4CommandFailureDetail {
    param(
        [Parameter(Mandatory)] [AllowEmptyCollection()] [AllowEmptyString()] [string[]]$Lines,
        [ValidateRange(8, 400)] [int]$MaximumLines = 60
    )
    # Drop the command and exit-code lines Invoke-Fm4LoggedCommand adds.
    $Lines = @($Lines | Where-Object { $_ -notmatch '^> \S' })
    $errorPattern = '(?i)(\berror\b|\bfatal\b|FAILED|LNK\d{4}|undefined (symbol|reference)|cannot |could not |unable to |denied|not found|out of memory|crashed|Stack dump:)'
    $kind = 'none'
    $excerpt = [Collections.Generic.List[string]]::new()
    $failedCount = @($Lines | Where-Object { $_ -match '^FAILED: ' }).Count

    $first = -1
    for ($i = 0; $i -lt $Lines.Count; $i++) {
        if ($Lines[$i] -match '^FAILED: ') { $first = $i; break }
    }
    if ($first -ge 0) {
        $kind = 'ninja'
        $block = [Collections.Generic.List[string]]::new()
        for ($i = $first; $i -lt $Lines.Count; $i++) {
            $line = $Lines[$i]
            if ($i -gt $first -and $line -match '^(FAILED: |\[\d+/\d+\] |ninja: (build stopped|error|warning))') { break }
            if ($i -eq $first + 1 -and $line.Length -gt 600) {
                $line = $line.Substring(0, 600) + ' ...'
            }
            $block.Add($line)
        }
        while ($block.Count -gt 1 -and [string]::IsNullOrWhiteSpace($block[$block.Count - 1])) {
            $block.RemoveAt($block.Count - 1)
        }
        if ($block.Count -le $MaximumLines) {
            $excerpt.AddRange($block)
        }
        else {
            # Keep the FAILED line, the command and the lines around each
            # error instead of a run of warnings.
            $keep = [Collections.Generic.SortedSet[int]]::new()
            [void]$keep.Add(0); if ($block.Count -gt 1) { [void]$keep.Add(1) }
            for ($i = 2; $i -lt $block.Count; $i++) {
                if ($block[$i] -match $errorPattern -and $block[$i] -notmatch '(?i)\bwarning\b') {
                    for ($j = [Math]::Max(2, $i - 1); $j -le [Math]::Min($block.Count - 1, $i + 4); $j++) {
                        [void]$keep.Add($j)
                    }
                }
                if ($keep.Count -ge $MaximumLines) { break }
            }
            if ($keep.Count -le 2) {
                for ($i = 2; $i -lt $MaximumLines; $i++) { [void]$keep.Add($i) }
            }
            $previous = -1
            foreach ($index in $keep) {
                if ($excerpt.Count -ge $MaximumLines) { break }
                if ($previous -ge 0 -and $index -ne $previous + 1) { $excerpt.Add('  ...') }
                $excerpt.Add($block[$index])
                $previous = $index
            }
        }
        if ($failedCount -gt 1) {
            $excerpt.Add("($($failedCount - 1) more command(s) failed; the first failure is shown.)")
        }
    }
    else {
        for ($i = 0; $i -lt $Lines.Count -and $excerpt.Count -lt $MaximumLines; $i++) {
            if ($Lines[$i] -notmatch '^CMake Error') { continue }
            $kind = 'cmake'
            if ($excerpt.Count -gt 0) { $excerpt.Add('') }
            $excerpt.Add($Lines[$i])
            for ($j = $i + 1; $j -lt $Lines.Count -and $excerpt.Count -lt $MaximumLines; $j++) {
                if ($Lines[$j] -match '^(-- |CMake (Error|Warning|Deprecation))') { break }
                $excerpt.Add($Lines[$j])
            }
            $i = $j - 1
        }
        while ($excerpt.Count -gt 0 -and [string]::IsNullOrWhiteSpace($excerpt[$excerpt.Count - 1])) {
            $excerpt.RemoveAt($excerpt.Count - 1)
        }
        $ninjaErrors = @($Lines | Where-Object { $_ -match '^ninja: (error|fatal):' })
        foreach ($line in $ninjaErrors) {
            if ($excerpt.Count -ge $MaximumLines) { break }
            if ($kind -eq 'none') { $kind = 'ninja' }
            $excerpt.Add($line)
        }
    }
    if ($excerpt.Count -eq 0) {
        $meaningful = @($Lines | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
        $errors = @($meaningful | Where-Object { $_ -match $errorPattern -and $_ -notmatch '(?i)\bwarning\b' })
        $source = @(if ($errors.Count -gt 0) { $kind = 'errors'; $errors } else { $kind = 'tail'; $meaningful })
        $start = [Math]::Max(0, $source.Count - $MaximumLines)
        for ($i = $start; $i -lt $source.Count; $i++) { $excerpt.Add($source[$i]) }
    }
    [pscustomobject]@{
        Kind = $kind
        Excerpt = [string[]]$excerpt.ToArray()
        Hint = Get-Fm4FailureHint -Lines $excerpt.ToArray()
        FailedCommands = $failedCount
    }
}

function Read-Fm4LogLines {
    param([Parameter(Mandatory)] [string]$LogPath)
    if (-not (Test-Path -LiteralPath $LogPath -PathType Leaf)) { return [string[]]@() }
    # The game's and the build's logs are UTF-8; Windows PowerShell would read them as ANSI.
    try { [string[]]@(Get-Content -LiteralPath $LogPath -Encoding UTF8 -ErrorAction Stop) }
    catch { [string[]]@() }
}

# An exception describing a failed native step, with the facts
# setup-preview.ps1 records in .local/logs/setup-error.json.
function New-Fm4CommandFailure {
    param(
        [Parameter(Mandatory)] [string]$FailureMessage,
        [Parameter(Mandatory)] [string]$Step,
        [Parameter(Mandatory)] [string]$LogPath,
        [Parameter(Mandatory)] [int]$ExitCode,
        [string]$CommandLine
    )
    $detail = Get-Fm4CommandFailureDetail -Lines @(Read-Fm4LogLines -LogPath $LogPath)
    $failure = [Exception]::new("$FailureMessage Exit code: $ExitCode. Build log: $LogPath")
    $failure.Data['step'] = $Step
    $failure.Data['build_log'] = $LogPath
    $failure.Data['exit_code'] = $ExitCode
    if ($CommandLine) { $failure.Data['command'] = $CommandLine }
    $failure.Data['error_kind'] = $detail.Kind
    $failure.Data['error_excerpt'] = $detail.Excerpt
    if ($detail.Hint) { $failure.Data['hint'] = $detail.Hint }
    $failure
}

function Invoke-Fm4BuildCommand {
    param(
        [Parameter(Mandatory)] [string]$FilePath,
        [Parameter(Mandatory)] [string[]]$Arguments,
        [Parameter(Mandatory)] [string]$LogPath,
        [Parameter(Mandatory)] [string]$FailureMessage,
        [string]$Step
    )
    if (-not $Step) { $Step = [IO.Path]::GetFileNameWithoutExtension($LogPath) }
    Invoke-Fm4LoggedCommand -FilePath $FilePath -Arguments $Arguments -LogPath $LogPath
    $code = $LASTEXITCODE
    if ($code -ne 0) {
        throw (New-Fm4CommandFailure -FailureMessage $FailureMessage -Step $Step -LogPath $LogPath `
            -ExitCode $code -CommandLine (Format-Fm4CommandLine -FilePath $FilePath -Arguments $Arguments))
    }
}

# A short failure report: what failed, its exit code and log, the error lines
# from the log and a hint. Also written to .local/logs/setup-error.json.
function New-Fm4FailureRecord {
    param(
        [Parameter(Mandatory)] [Management.Automation.ErrorRecord]$ErrorRecord,
        [int]$TailLines = 40
    )
    $exception = $ErrorRecord.Exception
    $data = $exception.Data
    $record = [ordered]@{
        schema_version = 2
        created_utc = [DateTime]::UtcNow.ToString('o')
        message = $exception.Message
        category = [string]$ErrorRecord.CategoryInfo.Category
        script = $ErrorRecord.InvocationInfo.ScriptName
        line = $ErrorRecord.InvocationInfo.ScriptLineNumber
        script_stack = [string]$ErrorRecord.ScriptStackTrace
    }
    foreach ($key in @('step', 'command', 'exit_code', 'build_log', 'error_kind', 'hint')) {
        if ($data.Contains($key) -and $null -ne $data[$key]) { $record[$key] = $data[$key] }
    }
    $logLines = [string[]]@()
    if ($record.Contains('build_log')) { $logLines = @(Read-Fm4LogLines -LogPath $record.build_log) }
    if ($data.Contains('error_excerpt') -and $null -ne $data['error_excerpt']) {
        $record.error_excerpt = [string[]]@($data['error_excerpt'])
    }
    elseif ($logLines.Count -gt 0) {
        $detail = Get-Fm4CommandFailureDetail -Lines $logLines
        $record.error_kind = $detail.Kind
        $record.error_excerpt = $detail.Excerpt
        if ($detail.Hint -and -not $record.Contains('hint')) { $record.hint = $detail.Hint }
    }
    if (-not $record.Contains('hint')) {
        $hintLines = @($exception.Message)
        if ($record.Contains('error_excerpt')) { $hintLines += @($record.error_excerpt) }
        $hint = Get-Fm4FailureHint -Lines $hintLines
        if ($hint) { $record.hint = $hint }
    }
    if ($logLines.Count -gt 0) {
        $start = [Math]::Max(0, $logLines.Count - $TailLines)
        $record.output_tail = [string[]]$logLines[$start..($logLines.Count - 1)]
    }
    $record
}

function Get-Fm4TotalMemoryBytes {
    try {
        [int64](Get-CimInstance -ClassName Win32_ComputerSystem -ErrorAction Stop).TotalPhysicalMemory
    }
    catch { [int64]0 }
}

# Parallel compile jobs for this machine. Logical processors alone overcommit
# memory on many-thread, low-memory machines: the largest generated
# translation units peak near 0.9 GB each in clang at -O3, and a link or other
# programs need room too, so budget 1.5 GB per job after 2 GB for Windows.
function Get-Fm4BuildJobCount {
    param(
        [int]$LogicalProcessors = [Environment]::ProcessorCount,
        [int64]$MemoryBytes = -1
    )
    if ($MemoryBytes -lt 0) { $MemoryBytes = Get-Fm4TotalMemoryBytes }
    $jobs = [Math]::Max(2, [Math]::Min(16, $LogicalProcessors - 1))
    if ($MemoryBytes -gt 0) {
        $byMemory = [int][Math]::Floor(($MemoryBytes / 1GB - 2) / 1.5)
        $jobs = [Math]::Max(1, [Math]::Min($jobs, $byMemory))
    }
    $jobs
}

# Machine facts that explain most setup failures (memory, disk, threads).
function Get-Fm4SystemSummary {
    param([Parameter(Mandatory)] [string]$Root)
    $summary = [ordered]@{
        windows_build = [Environment]::OSVersion.Version.Build
        powershell = $PSVersionTable.PSVersion.ToString()
        logical_processors = [Environment]::ProcessorCount
    }
    $memory = Get-Fm4TotalMemoryBytes
    if ($memory -gt 0) { $summary.memory_gb = [Math]::Round($memory / 1GB, 1) }
    try {
        $drive = [IO.DriveInfo]::new([IO.Path]::GetPathRoot([IO.Path]::GetFullPath($Root)))
        $summary.free_disk_gb = [Math]::Round($drive.AvailableFreeSpace / 1GB, 1)
    }
    catch { }
    $summary
}

function Format-Fm4FailureRecord {
    param([Parameter(Mandatory)] [Collections.IDictionary]$Record)
    $lines = [Collections.Generic.List[string]]::new()
    $lines.Add('==================== SETUP FAILED ====================')
    $lines.Add("Error: $($Record.message)")
    if ($Record.Contains('step')) { $lines.Add("Failed step: $($Record.step)") }
    if ($Record.Contains('exit_code')) { $lines.Add("Exit code: $($Record.exit_code)") }
    if ($Record.Contains('build_log')) { $lines.Add("Full log: $($Record.build_log)") }
    if ($Record.Contains('error_excerpt') -and @($Record.error_excerpt).Count -gt 0) {
        $lines.Add('First error from the log:')
        foreach ($line in @($Record.error_excerpt)) { $lines.Add("    $line") }
    }
    if ($Record.Contains('hint')) { $lines.Add("What to try: $($Record.hint)") }
    $lines.Add('When reporting this, attach .local\logs\setup-error.json' +
        $(if ($Record.Contains('build_log')) { ' and the full log named above.' } else { '.' }))
    $lines.Add('======================================================')
    [string[]]$lines.ToArray()
}

function Enter-Fm4BuildEnvironment {
    $config = Get-Fm4ReleaseToolchain
    $toolset = Get-Fm4BuildToolset
    if ($null -eq $toolset) {
        $failure = [Exception]::new('The Microsoft C++ runtime and Windows SDK files are missing. Run setup ' +
            'with -AcceptMicrosoftLicense, or with -ToolchainSource VisualStudio to use Microsoft''s ' +
            'installer (asks for administrator permission once).')
        $failure.Data['step'] = 'msvc'
        throw $failure
    }
    $llvm = Resolve-Fm4ToolchainPath -RelativePath $config.llvm.install_path
    $cmake = Get-Fm4CMake
    $ninja = Get-Fm4Ninja
    $variables = Get-Fm4BuildVariables -Toolset $toolset -LlvmBin (Join-Path $llvm 'bin') `
        -NinjaDir (Split-Path $ninja -Parent) -CMakeBin (Split-Path $cmake -Parent) -InheritedPath $env:PATH
    foreach ($name in $variables.Keys) {
        [Environment]::SetEnvironmentVariable($name, $variables[$name], 'Process')
    }
    [pscustomobject]@{ CMake = $cmake; Ninja = $ninja; LlvmRoot = $llvm; Toolset = $toolset }
}

# A CMake build tree records its absolute location, and CMake refuses to configure one
# that was moved (a portable install taken to another drive or PC). Such a tree is
# configured afresh; the build then recompiles what the new paths invalidate. Returns
# whether the cache was removed.
function Reset-Fm4RelocatedCMakeCache {
    param([Parameter(Mandatory)] [string]$BuildDirectory)
    $cache = Join-Path $BuildDirectory 'CMakeCache.txt'
    if (-not (Test-Path -LiteralPath $cache -PathType Leaf)) { return $false }
    $line = Select-String -LiteralPath $cache -Pattern '^CMAKE_CACHEFILE_DIR:INTERNAL=(.+)$' |
        Select-Object -First 1
    if ($null -eq $line) { return $false }
    $recorded = [IO.Path]::GetFullPath($line.Matches[0].Groups[1].Value.Trim()).TrimEnd('\', '/')
    $current = [IO.Path]::GetFullPath($BuildDirectory).TrimEnd('\', '/')
    if ([string]::Equals($recorded, $current, [StringComparison]::OrdinalIgnoreCase)) { return $false }
    Remove-Item -LiteralPath $cache -Force
    $cmakeFiles = Join-Path $BuildDirectory 'CMakeFiles'
    if (Test-Path -LiteralPath $cmakeFiles) { Remove-Item -LiteralPath $cmakeFiles -Recurse -Force }
    return $true
}

function Get-Fm4Git {
    $config = Get-Fm4ReleaseToolchain
    $candidate = Join-Path (Resolve-Fm4ToolchainPath -RelativePath $config.git.install_path) $config.git.executable
    if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
        throw 'Git is not available. Run provision-toolchain.ps1 first.'
    }
    $candidate
}

# Windows PowerShell 5.1 under 'Stop' can turn a native command's stderr into a terminating
# error even when it is redirected, and a fetch that failed after `git init` leaves an unborn
# HEAD that git complains about. Both queries decide from their output alone.
function Get-Fm4GitHead {
    param(
        [Parameter(Mandatory)] [string]$Git,
        [Parameter(Mandatory)] [string]$Repository
    )
    $ErrorActionPreference = 'Continue'
    $head = @(& $Git -C $Repository rev-parse --verify --quiet HEAD 2>$null)
    if ($LASTEXITCODE -eq 0 -and $head.Count -eq 1) { $head[0] } else { $null }
}

function Test-Fm4GitDirty {
    param(
        [Parameter(Mandatory)] [string]$Git,
        [Parameter(Mandatory)] [string]$Repository
    )
    $ErrorActionPreference = 'Continue'
    @(& $Git -C $Repository status --porcelain --ignore-submodules=dirty 2>$null).Count -ne 0
}

# The launcher draws its Settings panel from graphics-settings.json beside the executable.
function Write-Fm4GraphicsSchema {
    param(
        [Parameter(Mandatory)] [string]$ExecutableDirectory,
        [Parameter(Mandatory)] [string]$GameRoot,
        [Parameter(Mandatory)] [string]$LogPath
    )
    $schema = Join-Path $ExecutableDirectory 'graphics-settings.json'
    # A previous build's file (a rerun, an update's seeded folder) must not pass for this one's.
    if (Test-Path -LiteralPath $schema) { Remove-Item -LiteralPath $schema -Force }
    $process = Start-Process -FilePath (Join-Path $ExecutableDirectory 'fm4_static_recomp.exe') -Wait -PassThru `
        -ArgumentList (@("--game_data_root=$GameRoot", "--fm4_write_graphics_schema=$schema",
            "--log_file=$LogPath") | ForEach-Object { ConvertTo-Fm4QuotedArgument $_ })
    if (-not (Test-Path -LiteralPath $schema -PathType Leaf)) {
        throw (New-Fm4CommandFailure -FailureMessage 'The graphics settings file was not written.' `
            -Step 'schema' -LogPath $LogPath -ExitCode $process.ExitCode)
    }
}

# Adds the shaders of the shipped list (config\shaders), taken from the player's Disc 1, and its
# pipelines to the shader cache under the cache root; the next launch compiles them. Returns the
# result line.
function Write-Fm4ShaderCache {
    param(
        [Parameter(Mandatory)] [string]$ExecutableDirectory,
        [Parameter(Mandatory)] [string]$GameRoot,
        [Parameter(Mandatory)] [string]$CacheRoot,
        [Parameter(Mandatory)] [string]$UserRoot,
        [Parameter(Mandatory)] [string]$LogPath
    )
    if (Test-Path -LiteralPath $LogPath) { Remove-Item -LiteralPath $LogPath -Force }
    $shareable = Join-Path $CacheRoot 'shaders\shareable'
    $process = Start-Process -FilePath (Join-Path $ExecutableDirectory 'fm4_static_recomp.exe') -Wait -PassThru `
        -ArgumentList (@("--game_data_root=$GameRoot", "--fm4_build_shader_cache=$shareable",
            "--fm4_shader_list_dir=$(Join-Path (Get-Fm4RepoRoot) 'config\shaders')",
            "--user_data_root=$UserRoot", "--cache_root=$CacheRoot",
            "--log_file=$LogPath") | ForEach-Object { ConvertTo-Fm4QuotedArgument $_ })
    $result = @(Read-Fm4LogLines -LogPath $LogPath | Where-Object { $_ -match '\] Shader cache: ' } |
        ForEach-Object { $_ -replace '^.*?\] (?=Shader cache: )', '' }) | Select-Object -Last 1
    if ($process.ExitCode -ne 0 -or -not $result) {
        $message = if ($result) { "$result." } else { 'The shader cache was not written.' }
        throw (New-Fm4CommandFailure -FailureMessage $message -Step 'shaders' -LogPath $LogPath `
            -ExitCode $process.ExitCode)
    }
    $result
}

# The disc's UI fonts, decompressed, for the launcher's headings (it cannot decode XMem LZX).
# Returns the result line.
function Write-Fm4UiFonts {
    param(
        [Parameter(Mandatory)] [string]$ExecutableDirectory,
        [Parameter(Mandatory)] [string]$GameRoot,
        [Parameter(Mandatory)] [string]$OutDirectory,
        [Parameter(Mandatory)] [string]$LogPath
    )
    if (Test-Path -LiteralPath $LogPath) { Remove-Item -LiteralPath $LogPath -Force }
    $process = Start-Process -FilePath (Join-Path $ExecutableDirectory 'fm4_static_recomp.exe') -Wait -PassThru `
        -ArgumentList (@("--game_data_root=$GameRoot", "--fm4_export_ui_fonts=$OutDirectory",
            "--log_file=$LogPath") | ForEach-Object { ConvertTo-Fm4QuotedArgument $_ })
    $result = @(Read-Fm4LogLines -LogPath $LogPath | Where-Object { $_ -match '\] UI fonts: ' } |
        ForEach-Object { $_ -replace '^.*?\] (?=UI fonts: )', '' }) | Select-Object -Last 1
    if ($process.ExitCode -ne 0 -or -not $result -or $result -notmatch '^UI fonts: exported ') {
        $message = if ($result) { "$result." } else { 'The UI fonts were not exported.' }
        throw (New-Fm4CommandFailure -FailureMessage $message -Step 'fonts' -LogPath $LogPath `
            -ExitCode $process.ExitCode)
    }
    $result
}
