<#
.SYNOPSIS
    Build OpenBoard-ML for Windows x64 and package it as an installer and a portable zip.

.DESCRIPTION
    Does everything from a clean checkout; the GitHub Actions workflow (.github/workflows/windows.yml)
    only calls this script.

      1. Visual Studio 2022 environment (vcvars64), unless the shell already has one.
      2. Qt (official MSVC 2022 x64 binaries, with Qt WebEngine) through aqtinstall, unless -QtDir
         points at an existing Qt.
      3. poppler (with its private headers), OpenSSL and zlib through vcpkg in manifest mode
         (release_scripts/windows/vcpkg.json, pinned baseline, release-only DLLs).
      4. QuaZip sources (pinned version and SHA-256); OpenBoard.pro compiles them into the program.
      5. qmake + nmake (or jom) in a shadow build directory.
      6. Deployed tree: windeployqt, the vcpkg DLLs, the MSVC runtime DLLs (app-local), the
         translations (lrelease) and OpenBoard's data folders.
      7. Import check: every DLL imported by any exe/dll in the tree must be in the tree or be a
         Windows system DLL (dumpbin /dependents).
      8. OpenBoard-ML-<version>-Windows-x64-Portable.zip and, with Inno Setup,
         OpenBoard-ML-<version>-Windows-x64-Setup.exe, plus SHA256SUMS.txt.

    Requirements on your own PC: Visual Studio 2022 (or the Build Tools) with "Desktop development
    with C++", Git, Python 3 (for aqtinstall, only if Qt has to be installed), vcpkg
    (git clone https://github.com/microsoft/vcpkg; .\vcpkg\bootstrap-vcpkg.bat) and Inno Setup 6.3+
    (winget install JRSoftware.InnoSetup). PowerShell 7 is recommended; Windows PowerShell 5.1 works.

.EXAMPLE
    pwsh -File release_scripts\windows\build.ps1 -VcpkgRoot C:\src\vcpkg

.EXAMPLE
    pwsh -File release_scripts\windows\build.ps1 -QtDir C:\Qt\6.10.3\msvc2022_64 -VcpkgRoot C:\src\vcpkg -NoInstaller
#>
[CmdletBinding()]
param(
    # Dependencies: Qt, vcpkg libraries, QuaZip sources. Application: compile, deploy, package.
    [ValidateSet('All', 'Dependencies', 'Application')]
    [string]$Phase = 'All',

    # Qt for MSVC 2022 x64 (the folder that contains bin\qmake.exe). Default: <DepsDir>\Qt\<QtVersion>\msvc2022_64,
    # installed there with aqtinstall when it is missing.
    [string]$QtDir,
    [string]$QtVersion = '6.10.3',
    [string]$AqtVersion = '3.3.0',

    # vcpkg checkout (bootstrapped). Default: $env:VCPKG_ROOT, then $env:VCPKG_INSTALLATION_ROOT (GitHub runners).
    [string]$VcpkgRoot,

    # Downloaded and built dependencies. Default: <repo>\build\win32-deps
    [string]$DepsDir,
    # qmake shadow build. Default: <repo>\build\win32-msvc
    [string]$BuildDir,
    # Deployed tree, zip and installer. Default: <repo>\install\win32
    [string]$OutDir,

    # Remove the build directory first.
    [switch]$Clean,
    # Make the zip only, no Inno Setup installer.
    [switch]$NoInstaller
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'   # Invoke-WebRequest is very slow with a progress bar
[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12

# ---------------------------------------------------------------------------------------------
# Pinned versions
# ---------------------------------------------------------------------------------------------
$QuaZipVersion = '1.7.2'
$QuaZipSha256  = '5240f4a3475648773ba10af8b0ebe549cbfc5d6de00470a517330a80068a4f78'   # GitHub tag archive v1.7.2.tar.gz
$QuaZipUrl     = "https://github.com/stachenov/quazip/archive/refs/tags/v$QuaZipVersion.tar.gz"
# Qt modules on top of qtbase/qtdeclarative/qtsvg/qttools/qttranslations (OpenBoard.pro: QT += ...)
$QtModules     = @('qtwebengine', 'qtwebchannel', 'qtpositioning', 'qtmultimedia', 'qt5compat')
$QtArch        = 'win64_msvc2022_64'
$VcpkgTriplet  = 'x64-windows-release'   # release_scripts/windows/vcpkg-triplets

$ScriptDir = $PSScriptRoot
$RepoRoot  = (Resolve-Path (Join-Path $ScriptDir '..\..')).Path
if (-not $DepsDir)  { $DepsDir  = Join-Path $RepoRoot 'build\win32-deps' }
if (-not $BuildDir) { $BuildDir = Join-Path $RepoRoot 'build\win32-msvc' }
if (-not $OutDir)   { $OutDir   = Join-Path $RepoRoot 'install\win32' }
$AppName  = 'OpenBoard-ML'
$InGitHub = [bool]$env:GITHUB_ACTIONS

# ---------------------------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------------------------
$script:StepWatch = $null
function Write-Step([string]$Title) {
    $now = Get-Date -Format 'HH:mm:ss'
    if ($script:StepWatch) { Write-Host ("   (previous step took {0:n1} min)" -f $script:StepWatch.Elapsed.TotalMinutes) }
    $script:StepWatch = [Diagnostics.Stopwatch]::StartNew()
    Write-Host ''
    Write-Host "==== [$now] $Title"
    $script:CurrentStep = $Title
}

function Write-Warn([string]$Message) {
    if ($InGitHub) { Write-Host "::warning::$Message" } else { Write-Warning $Message }
}

# Run a native program; stop with a clear message if it fails.
function Invoke-Native {
    param(
        [Parameter(Mandatory)] [string]$FilePath,
        [string[]]$Arguments = @(),
        [string]$What = (Split-Path -Leaf $FilePath)
    )
    Write-Host "> `"$FilePath`" $($Arguments -join ' ')"
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$What failed (exit code $LASTEXITCODE)" }
}

# Run a native program whose stderr is redirected or whose failure is expected: Windows
# PowerShell 5.1 turns redirected stderr lines into terminating errors under 'Stop'.
function Invoke-Lenient([scriptblock]$Block) {
    $saved = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { & $Block } finally { $ErrorActionPreference = $saved }
}

function Get-Tool([string]$Name) {
    $cmd = Get-Command $Name -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($cmd) { return $cmd.Source }
    return $null
}

function Assert-Path([string]$Path, [string]$Message) {
    if (-not (Test-Path -LiteralPath $Path)) { throw "$Message (missing: $Path)" }
}

function Get-AppVersion {
    $text = Get-Content -LiteralPath (Join-Path $RepoRoot 'version.txt')
    $fields = @{}
    foreach ($line in $text) {
        if ($line -match '^\s*(VERSION_[A-Z]+)\s*=\s*([^\s#]+)') { $fields[$Matches[1]] = $Matches[2] }
    }
    foreach ($k in 'VERSION_MAJ', 'VERSION_MIN', 'VERSION_PATCH', 'VERSION_TYPE') {
        if (-not $fields.ContainsKey($k)) { throw "version.txt has no $k" }
    }
    $numeric = '{0}.{1}.{2}' -f $fields['VERSION_MAJ'], $fields['VERSION_MIN'], $fields['VERSION_PATCH']
    $full = $numeric   # same rule as OpenBoard.pro
    if ($fields['VERSION_TYPE'] -ne 'r') { $full = "$numeric-$($fields['VERSION_TYPE']).$($fields['VERSION_BUILD'])" }
    return [pscustomobject]@{ Full = $full; Numeric = "$numeric.0" }
}

# Import the environment of vcvars64.bat into this process, unless a x64 MSVC environment is active.
function Initialize-Msvc {
    if ($env:VSCMD_ARG_TGT_ARCH -eq 'x64' -and (Get-Tool 'cl.exe')) {
        Write-Host "MSVC environment already set up ($env:VCToolsInstallDir)"
        return
    }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    Assert-Path $vswhere 'vswhere.exe not found: install Visual Studio 2022 or its Build Tools with "Desktop development with C++"'
    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vsPath) { throw 'No Visual Studio with the x64 C++ tools found (component Microsoft.VisualStudio.Component.VC.Tools.x86.x64)' }
    $vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
    Assert-Path $vcvars 'vcvars64.bat not found'
    Write-Host "Using $vcvars"
    $tmp = Join-Path ([IO.Path]::GetTempPath()) ("vcvars-{0}.cmd" -f [Guid]::NewGuid())
    Set-Content -LiteralPath $tmp -Encoding ASCII -Value @("@call `"$vcvars`" >nul", '@if errorlevel 1 exit /b 1', '@set')
    try {
        $lines = & cmd.exe /d /c $tmp
        if ($LASTEXITCODE -ne 0) { throw "vcvars64.bat failed (exit code $LASTEXITCODE)" }
    } finally {
        Remove-Item -LiteralPath $tmp -ErrorAction SilentlyContinue
    }
    foreach ($line in $lines) {
        if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process') }
    }
    if (-not (Get-Tool 'cl.exe')) { throw 'cl.exe is not on PATH after vcvars64.bat' }
}

# Python 3 as an array: the program and its leading arguments (python.exe, or py.exe -3).
# The Microsoft Store stub python.exe fails the version test and is skipped.
function Find-Python {
    $candidates = @()
    $python = Get-Tool 'python.exe'
    if ($python) { $candidates += , @($python) }
    $py = Get-Tool 'py.exe'
    if ($py) { $candidates += , @($py, '-3') }
    foreach ($candidate in $candidates) {
        $exe = $candidate[0]
        $extra = @($candidate | Select-Object -Skip 1)
        $out = Invoke-Lenient { & $exe @extra -c 'import sys; print(sys.version_info[0])' 2>$null }
        if ($LASTEXITCODE -eq 0 -and "$out".Trim() -eq '3') { return , $candidate }
    }
    return $null
}

function Install-Qt {
    $qtRoot = Join-Path $DepsDir 'Qt'
    $python = Find-Python
    if (-not $python) { throw "Qt is not installed at $QtDir and Python 3 was not found to install it with aqtinstall. Install Python 3, or pass -QtDir <Qt>\$QtVersion\msvc2022_64 (Qt Online Installer: MSVC 2022 64-bit with Qt WebEngine, Qt Multimedia, Qt Positioning, Qt WebChannel, Qt 5 Compatibility)." }
    Write-Host "Installing Qt with aqtinstall $AqtVersion into $qtRoot (Python: $($python -join ' '))"
    $venv = Join-Path $DepsDir 'aqt-venv'
    $venvPython = Join-Path $venv 'Scripts\python.exe'
    if (-not (Test-Path -LiteralPath $venvPython)) {
        $pyArgs = @($python | Select-Object -Skip 1) + @('-m', 'venv', $venv)
        Invoke-Native $python[0] $pyArgs 'python -m venv'
    }
    Invoke-Native $venvPython @('-m', 'pip', 'install', '--disable-pip-version-check', '--quiet', "aqtinstall==$AqtVersion") 'pip install aqtinstall'
    $aqtArgs = @('-m', 'aqt', 'install-qt', 'windows', 'desktop', $QtVersion, $QtArch, '-m') + $QtModules + @('-O', $qtRoot)
    Invoke-Native $venvPython $aqtArgs 'aqt install-qt'
}

function Expand-TarGz([string]$Archive, [string]$Destination) {
    # Windows' own bsdtar: Git's GNU tar on PATH would read "C:" as a remote host name.
    $tar = Join-Path $env:SystemRoot 'System32\tar.exe'
    Assert-Path $tar 'tar.exe (part of Windows 10 and newer) not found'
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    Invoke-Native $tar @('-xzf', $Archive, '-C', $Destination) 'tar'
}

function Get-FileSizeMB([string]$Path) { return [math]::Round((Get-Item -LiteralPath $Path).Length / 1MB, 1) }

# DLL names a PE file imports: dumpbin /dependents. Returns objects with Name and Delay (delay-loaded).
function Get-PEImports([string]$File) {
    $out = & dumpbin.exe /NOLOGO /DEPENDENTS $File
    if ($LASTEXITCODE -ne 0) { throw "dumpbin /dependents failed for $File" }
    $section = ''
    foreach ($line in $out) {
        if ($line -match 'Image has the following dependencies') { $section = 'normal'; continue }
        if ($line -match 'Image has the following delay load dependencies') { $section = 'delay'; continue }
        if ($line -match '^\s*Summary\s*$') { $section = ''; continue }
        if ($section -and $line -match '^\s+(\S+\.(dll|exe|drv|sys|ocx))\s*$') {
            [pscustomobject]@{ Name = $Matches[1]; Delay = ($section -eq 'delay') }
        }
    }
}

# Copy into $Tree every non-system DLL that a PE file of the tree imports and that is in one of
# $SearchDirs; repeat until nothing changes. Then report anything still unresolved.
# The Visual C++ runtime DLLs are also in System32 of a build machine but not of a clean Windows,
# so they always have to be in the tree.
# Returns the number of unresolved (non-delay-load) imports.
function Resolve-TreeImports([string]$Tree, [string[]]$SearchDirs) {
    $system32 = Join-Path $env:SystemRoot 'System32'
    $vcRuntime = '^((msvcp|vcruntime|concrt|vccorlib|vcomp|vcamp|mfc)1\d\d|ucrtbased\.dll$)'
    $unresolved = @{}
    for ($pass = 1; $pass -le 10; $pass++) {
        $local = @{}
        Get-ChildItem -LiteralPath $Tree -File | ForEach-Object { $local[$_.Name.ToLowerInvariant()] = $true }
        $pe = @(Get-ChildItem -LiteralPath $Tree -Recurse -File | Where-Object { $_.Extension -in '.exe', '.dll' })
        $copied = 0
        $unresolved = @{}
        foreach ($file in $pe) {
            foreach ($imp in @(Get-PEImports $file.FullName)) {
                $name = $imp.Name.ToLowerInvariant()
                if ($name -like 'api-ms-*' -or $name -like 'ext-ms-*') { continue }   # API sets
                # DLLs are found next to the exe, so a DLL deeper in the tree does not help
                if ($local.ContainsKey($name)) { continue }
                $mustBeLocal = $name -match $vcRuntime
                $source = $null
                foreach ($dir in $SearchDirs) {
                    $candidate = Join-Path $dir $imp.Name
                    if (Test-Path -LiteralPath $candidate) { $source = $candidate; break }
                }
                if (-not $source -and -not $mustBeLocal -and (Test-Path -LiteralPath (Join-Path $system32 $imp.Name))) { continue }   # Windows
                if ($source) {
                    Write-Host "   + $($imp.Name)  (imported by $($file.Name), from $(Split-Path -Parent $source))"
                    Copy-Item -LiteralPath $source -Destination $Tree
                    $local[$name] = $true
                    $copied++
                } else {
                    $key = "$name|$($imp.Delay)"
                    if (-not $unresolved.ContainsKey($key)) { $unresolved[$key] = New-Object System.Collections.ArrayList }
                    [void]$unresolved[$key].Add($file.FullName.Substring($Tree.Length + 1))
                }
            }
        }
        Write-Host "   import pass ${pass}: $($pe.Count) PE files, $copied DLLs added"
        if ($copied -eq 0) { break }
    }
    # Media Foundation / Windows Media: part of desktop Windows, but optional on Windows Server
    # (the build machine) and absent on "N" editions without the Media Feature Pack
    $mediaFeature = '^(mf|mfplat|mfreadwrite|mfcore|mfsensorgroup|mfsrcsnk|evr|dxva2|wmvcore|msmpeg2vdec|msdmo|ksuser|quartz)\.dll$'
    $hard = 0
    foreach ($key in ($unresolved.Keys | Sort-Object)) {
        $parts = $key.Split('|')
        $users = ($unresolved[$key] | Select-Object -First 5) -join ', '
        if ($parts[1] -eq 'True') {
            Write-Warn "delay-loaded DLL $($parts[0]) is neither in the tree nor in System32 (used by $users)"
        } elseif ($parts[0] -match $mediaFeature) {
            Write-Warn "$($parts[0]) (Windows media feature) is not on this build machine; desktop Windows has it (used by $users)"
        } else {
            Write-Host "::error::DLL $($parts[0]) is neither in the tree nor a Windows system DLL (imported by $users)"
            $hard++
        }
    }
    return $hard
}

# ---------------------------------------------------------------------------------------------
# Phases
# ---------------------------------------------------------------------------------------------
function Get-FullPath([string]$Path) {
    # absolute, without a trailing backslash (a trailing \ before a closing quote breaks iscc/cmd arguments)
    return [IO.Path]::GetFullPath($Path).TrimEnd('\')
}

function Get-Paths {
    $script:DepsDir = Get-FullPath $DepsDir
    $script:BuildDir = Get-FullPath $BuildDir
    $script:OutDir = Get-FullPath $OutDir
    $script:QtDirGiven = [bool]$QtDir
    if ($QtDir) { $script:QtDir = Get-FullPath $QtDir }
    else { $script:QtDir = Join-Path $DepsDir "Qt\$QtVersion\msvc2022_64" }
    if (-not $VcpkgRoot) { $script:VcpkgRoot = $env:VCPKG_ROOT }
    if (-not $VcpkgRoot) { $script:VcpkgRoot = $env:VCPKG_INSTALLATION_ROOT }
    $script:VcpkgInstallRoot = Join-Path $DepsDir 'vcpkg_installed'
    $script:Prefix = Join-Path $VcpkgInstallRoot $VcpkgTriplet
    $script:QuaZipSrc = Join-Path $DepsDir "quazip-$QuaZipVersion\quazip"
}

function Invoke-DependenciesPhase {
    New-Item -ItemType Directory -Force -Path $DepsDir | Out-Null

    Write-Step "Qt $QtVersion ($QtArch)"
    if (-not (Test-Path -LiteralPath (Join-Path $QtDir 'bin\qmake.exe'))) {
        if ($QtDirGiven) { throw "-QtDir $QtDir has no bin\qmake.exe" }
        Install-Qt
    } else {
        Write-Host "Qt found at $QtDir"
    }
    Assert-Path (Join-Path $QtDir 'bin\qmake.exe') 'qmake.exe not found in the Qt directory'
    foreach ($dll in 'Qt6WebEngineCore.dll', 'Qt6Multimedia.dll', 'Qt6Core5Compat.dll', 'Qt6WebChannel.dll', 'Qt6Positioning.dll', 'Qt6Svg.dll') {
        Assert-Path (Join-Path $QtDir "bin\$dll") "Qt module missing in $QtDir"
    }
    Assert-Path (Join-Path $QtDir 'bin\QtWebEngineProcess.exe') 'Qt WebEngine is incomplete'

    Write-Step 'vcpkg: poppler, OpenSSL, zlib'
    if (-not $VcpkgRoot) { throw 'vcpkg not found: pass -VcpkgRoot <dir> or set VCPKG_ROOT (git clone https://github.com/microsoft/vcpkg; .\vcpkg\bootstrap-vcpkg.bat)' }
    $vcpkg = Join-Path $VcpkgRoot 'vcpkg.exe'
    if (-not (Test-Path -LiteralPath $vcpkg)) {
        Invoke-Native (Join-Path $VcpkgRoot 'bootstrap-vcpkg.bat') @('-disableMetrics') 'bootstrap-vcpkg'
    }
    $manifest = Get-Content -Raw -LiteralPath (Join-Path $ScriptDir 'vcpkg.json') | ConvertFrom-Json
    $baseline = $manifest.'builtin-baseline'
    Invoke-Lenient { & git -C $VcpkgRoot cat-file -e "$baseline^{commit}" 2>$null }
    if ($LASTEXITCODE -ne 0) {
        throw "The vcpkg checkout in $VcpkgRoot does not contain the baseline commit $baseline of release_scripts\windows\vcpkg.json. Update it: git -C `"$VcpkgRoot`" pull"
    }
    $vcpkgHead = Invoke-Lenient { & git -C $VcpkgRoot rev-parse --short HEAD 2>$null }
    Write-Host "vcpkg $VcpkgRoot (HEAD $vcpkgHead), baseline $baseline"
    if ($env:VCPKG_DEFAULT_BINARY_CACHE) {
        New-Item -ItemType Directory -Force -Path $env:VCPKG_DEFAULT_BINARY_CACHE | Out-Null
        Write-Host "vcpkg binary cache: $env:VCPKG_DEFAULT_BINARY_CACHE"
    }
    $vcpkgArgs = @(
        'install',
        "--triplet=$VcpkgTriplet",
        "--host-triplet=$VcpkgTriplet",
        "--overlay-triplets=$(Join-Path $ScriptDir 'vcpkg-triplets')",
        "--x-manifest-root=$ScriptDir",
        "--x-install-root=$VcpkgInstallRoot",
        "--x-buildtrees-root=$(Join-Path $DepsDir 'vcpkg-buildtrees')",
        '--clean-after-build',
        '--disable-metrics'
    )
    Invoke-Native $vcpkg $vcpkgArgs 'vcpkg install'
    foreach ($f in 'include\poppler\PDFDoc.h', 'include\poppler\cpp\poppler-version.h', 'include\openssl\evp.h', 'include\zlib.h',
                   'lib\poppler.lib', 'lib\libcrypto.lib', 'lib\libssl.lib', 'lib\zlib.lib', 'bin\poppler.dll') {
        Assert-Path (Join-Path $Prefix $f) 'vcpkg did not install what OpenBoard.pro needs'
    }
    $popplerVersion = 'unknown'
    $m = @(Select-String -LiteralPath (Join-Path $Prefix 'include\poppler\cpp\poppler-version.h') -Pattern 'POPPLER_VERSION\s+"([^"]+)"')
    if ($m.Count -gt 0) { $popplerVersion = $m[0].Matches[0].Groups[1].Value }
    Write-Host "poppler $popplerVersion; DLLs in ${Prefix}\bin:"
    Get-ChildItem -LiteralPath (Join-Path $Prefix 'bin') -Filter *.dll | ForEach-Object { Write-Host "   $($_.Name)" }

    Write-Step "QuaZip $QuaZipVersion sources"
    if (-not (Test-Path -LiteralPath (Join-Path $QuaZipSrc 'quazip.h'))) {
        $archive = Join-Path $DepsDir "quazip-$QuaZipVersion.tar.gz"
        Write-Host "Downloading $QuaZipUrl"
        Invoke-WebRequest -Uri $QuaZipUrl -OutFile $archive -UseBasicParsing
        $hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($hash -ne $QuaZipSha256) { throw "QuaZip archive has SHA-256 $hash, expected $QuaZipSha256" }
        Expand-TarGz $archive $DepsDir
        Remove-Item -LiteralPath $archive
    }
    Assert-Path (Join-Path $QuaZipSrc 'quazip.h') 'QuaZip sources incomplete'
    Write-Host "QuaZip sources in $QuaZipSrc"
}

function Invoke-ApplicationPhase {
    $version = Get-AppVersion
    Write-Host "OpenBoard-ML version $($version.Full)"
    foreach ($p in (Join-Path $QtDir 'bin\qmake.exe'), (Join-Path $Prefix 'include\poppler\PDFDoc.h'), (Join-Path $QuaZipSrc 'quazip.h')) {
        Assert-Path $p 'Dependencies are missing: run this script with -Phase Dependencies (or All) first'
    }
    $qtBin = Join-Path $QtDir 'bin'
    $env:PATH = "$qtBin;$env:PATH"
    $env:QTDIR = $QtDir
    # Make sure OpenBoard.pro does not pick up values from the environment of the caller
    Remove-Item Env:\THIRD_PARTY_PATH -ErrorAction SilentlyContinue

    Write-Step 'Tool versions'
    Invoke-Native (Join-Path $qtBin 'qmake.exe') @('-query', 'QT_VERSION') 'qmake -query'
    Invoke-Lenient { & cl.exe 2>&1 } | Select-Object -First 1 | ForEach-Object { Write-Host "$_" }
    Write-Host "VCToolsVersion $env:VCToolsVersion, Windows SDK $env:WindowsSDKVersion"
    $wmvcore = @($env:LIB -split ';' | Where-Object { $_ } | ForEach-Object { Join-Path $_ 'WMVCore.lib' } | Where-Object { Test-Path -LiteralPath $_ })
    if ($wmvcore.Count -eq 0) { Write-Warn 'WMVCore.lib (Windows Media Format SDK, for the podcast encoder) was not found in the LIB paths; linking will fail' }
    else { Write-Host "WMVCore.lib: $($wmvcore[0])" }

    Write-Step 'qmake'
    if ($Clean -and (Test-Path -LiteralPath $BuildDir)) { Remove-Item -Recurse -Force -LiteralPath $BuildDir }
    New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
    Push-Location $BuildDir
    try {
        $qmakeArgs = @(
            (Join-Path $RepoRoot 'OpenBoard.pro'),
            '-spec', 'win32-msvc',
            "WIN_LIB_PREFIX=$($Prefix -replace '\\', '/')",
            "QUAZIP_SRC=$($QuaZipSrc -replace '\\', '/')"
        )
        Invoke-Native (Join-Path $qtBin 'qmake.exe') $qmakeArgs 'qmake'

        Write-Step 'Compile (release)'
        $jom = Get-Tool 'jom.exe'
        if ($jom) {
            Invoke-Native $jom @('/NOLOGO', 'release') 'jom'
        } else {
            Invoke-Native 'nmake.exe' @('/NOLOGO', 'release') 'nmake'
        }
    } finally {
        Pop-Location
    }
    $exe = Join-Path $BuildDir "build\win32\release\product\$AppName.exe"
    Assert-Path $exe 'The build did not produce the program'
    Write-Host "Built $exe ($(Get-FileSizeMB $exe) MB)"

    Write-Step 'Deploy: Qt (windeployqt)'
    $tree = Join-Path $OutDir $AppName
    if (Test-Path -LiteralPath $tree) { Remove-Item -Recurse -Force -LiteralPath $tree }
    New-Item -ItemType Directory -Force -Path $tree | Out-Null
    Copy-Item -LiteralPath $exe -Destination $tree
    # windeployqt before the data folders are copied, so that it scans nothing but the program
    Invoke-Native (Join-Path $qtBin 'windeployqt.exe') @('--release', '--no-compiler-runtime', '--verbose', '1', (Join-Path $tree "$AppName.exe")) 'windeployqt'

    # Qt WebEngine: windeployqt normally does this; make sure, since web mode needs all of it
    if (-not (Test-Path -LiteralPath (Join-Path $tree 'QtWebEngineProcess.exe'))) {
        Write-Warn 'windeployqt did not deploy QtWebEngineProcess.exe; copying it'
        Copy-Item -LiteralPath (Join-Path $qtBin 'QtWebEngineProcess.exe') -Destination $tree
    }
    if (-not (Get-ChildItem -LiteralPath (Join-Path $tree 'resources') -Filter '*.pak' -ErrorAction SilentlyContinue)) {
        Write-Warn 'windeployqt did not deploy the Qt WebEngine resources; copying them'
        New-Item -ItemType Directory -Force -Path (Join-Path $tree 'resources') | Out-Null
        Copy-Item -Path (Join-Path $QtDir 'resources\*') -Destination (Join-Path $tree 'resources') -Recurse -Force
    }
    $locales = Join-Path $tree 'translations\qtwebengine_locales'
    if (-not (Get-ChildItem -LiteralPath $locales -Filter '*.pak' -ErrorAction SilentlyContinue)) {
        Write-Warn 'windeployqt did not deploy qtwebengine_locales; copying them'
        New-Item -ItemType Directory -Force -Path $locales | Out-Null
        Copy-Item -Path (Join-Path $QtDir 'translations\qtwebengine_locales\*') -Destination $locales -Force
    }

    Write-Step 'Deploy: poppler, OpenSSL, zlib and their DLLs'
    foreach ($dll in (Get-ChildItem -LiteralPath (Join-Path $Prefix 'bin') -Filter '*.dll')) {
        $target = Join-Path $tree $dll.Name
        if (Test-Path -LiteralPath $target) { Write-Warn "$($dll.Name) is both in Qt and in vcpkg; keeping Qt's"; continue }
        Copy-Item -LiteralPath $dll.FullName -Destination $tree
        Write-Host "   $($dll.Name)"
    }

    Write-Step 'Deploy: Microsoft C++ runtime (app-local)'
    if (-not $env:VCToolsRedistDir) { throw 'VCToolsRedistDir is not set (MSVC environment incomplete)' }
    $crtDir = Get-ChildItem -LiteralPath (Join-Path $env:VCToolsRedistDir 'x64') -Directory -Filter 'Microsoft.VC14*.CRT' |
              Sort-Object Name | Select-Object -Last 1
    if (-not $crtDir) { throw "No Microsoft.VC14*.CRT folder in $($env:VCToolsRedistDir)x64" }
    Write-Host "from $($crtDir.FullName)"
    foreach ($dll in (Get-ChildItem -LiteralPath $crtDir.FullName -Filter '*.dll')) {
        Copy-Item -LiteralPath $dll.FullName -Destination $tree -Force
        Write-Host "   $($dll.Name)"
    }

    Write-Step 'Deploy: OpenBoard data and translations'
    foreach ($dir in 'etc', 'library', 'startupHints', 'customizations', 'fonts') {
        Copy-Item -LiteralPath (Join-Path $RepoRoot "resources\$dir") -Destination (Join-Path $tree $dir) -Recurse
    }
    Copy-Item -LiteralPath (Join-Path $RepoRoot 'resources\windows\xpdfrc') -Destination $tree
    foreach ($f in 'LICENSE', 'COPYRIGHT') { Copy-Item -LiteralPath (Join-Path $RepoRoot $f) -Destination (Join-Path $tree "$f.txt") }
    $i18n = Join-Path $tree 'i18n'
    New-Item -ItemType Directory -Force -Path $i18n | Out-Null
    $lrelease = Join-Path $qtBin 'lrelease.exe'
    $tsFiles = @(Get-ChildItem -LiteralPath (Join-Path $RepoRoot 'resources\i18n') -Filter 'OpenBoard_*.ts')
    foreach ($ts in $tsFiles) {
        Invoke-Native $lrelease @('-silent', $ts.FullName, '-qm', (Join-Path $i18n ($ts.BaseName + '.qm'))) 'lrelease'
    }
    Write-Host "$($tsFiles.Count) translations compiled"

    Write-Step 'Check: imports of every exe and dll in the tree'
    # Qt, vcpkg, and every redistributable folder of MSVC (CRT, OpenMP, ...)
    $searchDirs = @($qtBin, (Join-Path $Prefix 'bin')) +
                  @(Get-ChildItem -LiteralPath (Join-Path $env:VCToolsRedistDir 'x64') -Directory | ForEach-Object FullName)
    $missing = Resolve-TreeImports $tree $searchDirs
    if ($missing -gt 0) { throw "$missing imported DLL(s) cannot be found; the program would not start on a clean Windows" }

    Write-Step 'Check: required files'
    $required = @(
        "$AppName.exe", 'QtWebEngineProcess.exe',
        'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'Qt6Network.dll', 'Qt6Svg.dll', 'Qt6Core5Compat.dll',
        'Qt6Multimedia.dll', 'Qt6WebEngineCore.dll', 'Qt6WebEngineWidgets.dll', 'Qt6WebChannel.dll', 'Qt6Positioning.dll',
        'platforms\qwindows.dll', 'imageformats\qsvg.dll', 'imageformats\qjpeg.dll', 'tls\qschannelbackend.dll',
        'poppler.dll', 'libcrypto-3-x64.dll', 'libssl-3-x64.dll', 'zlib1.dll',
        'vcruntime140.dll', 'vcruntime140_1.dll', 'msvcp140.dll',
        'etc\OpenBoard.config', 'startupHints\index.html', 'i18n\OpenBoard_en.qm', 'i18n\OpenBoard_nl.qm', 'fonts', 'library', 'customizations'
    )
    $absent = @($required | Where-Object { -not (Test-Path -LiteralPath (Join-Path $tree $_)) })
    if ($absent.Count -gt 0) { throw "Deployed tree is incomplete, missing: $($absent -join ', ')" }
    $multimedia = @(Get-ChildItem -LiteralPath (Join-Path $tree 'multimedia') -Filter '*.dll' -ErrorAction SilentlyContinue)
    if ($multimedia.Count -eq 0) { throw 'No Qt Multimedia backend plugin in multimedia\' }
    Write-Host "multimedia backends: $(($multimedia | ForEach-Object Name) -join ', ')"
    $treeFiles = @(Get-ChildItem -LiteralPath $tree -Recurse -File)
    $treeMB = [math]::Round(($treeFiles | Measure-Object -Property Length -Sum).Sum / 1MB)
    Write-Host "Deployed tree: $($treeFiles.Count) files, $treeMB MB"
    Write-Host 'Top level:'
    Get-ChildItem -LiteralPath $tree | Sort-Object { -not $_.PSIsContainer }, Name | ForEach-Object {
        if ($_.PSIsContainer) { Write-Host "   $($_.Name)\" } else { Write-Host ("   {0}  ({1:n1} MB)" -f $_.Name, ($_.Length / 1MB)) }
    }

    Write-Step 'Package: portable zip'
    $base = "$AppName-$($version.Full)-Windows-x64"
    $zip = Join-Path $OutDir "$base-Portable.zip"
    if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::CreateFromDirectory($tree, $zip, [IO.Compression.CompressionLevel]::Optimal, $true)
    Write-Host "$zip ($(Get-FileSizeMB $zip) MB)"
    $products = @($zip)

    $setup = $null
    if (-not $NoInstaller) {
        Write-Step 'Package: installer (Inno Setup)'
        $iscc = Get-Tool 'iscc.exe'
        if (-not $iscc) {
            foreach ($dir in ${env:ProgramFiles(x86)}, $env:ProgramFiles, (Join-Path $env:LOCALAPPDATA 'Programs')) {
                if (-not $dir) { continue }
                $candidate = Join-Path $dir 'Inno Setup 6\ISCC.exe'
                if (Test-Path -LiteralPath $candidate) { $iscc = $candidate; break }
            }
        }
        if (-not $iscc) { throw 'Inno Setup 6 (ISCC.exe) not found: winget install JRSoftware.InnoSetup, or choco install innosetup, or run with -NoInstaller' }
        $isccArgs = @(
            '/Qp',
            "/DAppVersion=$($version.Full)",
            "/DNumericVersion=$($version.Numeric)",
            "/DSourceDir=$tree",
            "/DOutputDir=$OutDir",
            "/DOutputBaseFilename=$base-Setup",
            "/DProjectRoot=$RepoRoot",
            (Join-Path $ScriptDir 'OpenBoard-ML.iss')
        )
        Invoke-Native $iscc $isccArgs 'Inno Setup'
        $setup = Join-Path $OutDir "$base-Setup.exe"
        Assert-Path $setup 'Inno Setup did not write the installer'
        Write-Host "$setup ($(Get-FileSizeMB $setup) MB)"
        $products = @($setup) + $products
    }

    Write-Step 'Check: products'
    $minMB = 60   # the WebEngine DLLs alone compress to more than this
    foreach ($p in $products) {
        $mb = Get-FileSizeMB $p
        if ($mb -lt $minMB) { throw "$p is only $mb MB (expected at least $minMB MB); something is missing" }
    }
    $sums = Join-Path $OutDir 'SHA256SUMS.txt'
    $products | ForEach-Object { '{0}  {1}' -f (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash.ToLowerInvariant(), (Split-Path -Leaf $_) } |
        Set-Content -LiteralPath $sums -Encoding ASCII
    Get-Content -LiteralPath $sums | ForEach-Object { Write-Host "   $_" }

    if ($env:GITHUB_OUTPUT) {
        Add-Content -LiteralPath $env:GITHUB_OUTPUT -Encoding UTF8 -Value @(
            "version=$($version.Full)",
            "zip=$zip",
            "setup=$setup",
            "sums=$sums"
        )
    }
    if ($env:GITHUB_STEP_SUMMARY) {
        $summary = @('## OpenBoard-ML ' + $version.Full + ' for Windows x64', '', '| File | MB | SHA-256 |', '|---|---:|---|')
        foreach ($p in $products) {
            $summary += '| {0} | {1} | `{2}` |' -f (Split-Path -Leaf $p), (Get-FileSizeMB $p), (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()
        }
        $summary += ''
        $summary += "Qt $QtVersion, $treeMB MB deployed in $($treeFiles.Count) files."
        Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Encoding UTF8 -Value $summary
    }
}

# ---------------------------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------------------------
$total = [Diagnostics.Stopwatch]::StartNew()
$script:CurrentStep = 'start'
try {
    Write-Host "OpenBoard-ML Windows build, phase $Phase"
    Write-Host "repo $RepoRoot"
    Write-Host "PowerShell $($PSVersionTable.PSVersion), $([Environment]::OSVersion.VersionString), $([Environment]::ProcessorCount) CPUs"
    Get-Paths
    Write-Step 'Visual Studio environment'
    Initialize-Msvc
    if ($Phase -in 'All', 'Dependencies') { Invoke-DependenciesPhase }
    if ($Phase -in 'All', 'Application') { Invoke-ApplicationPhase }
    Write-Step 'Done'
    Write-Host ("Total {0:n1} min" -f $total.Elapsed.TotalMinutes)
} catch {
    Write-Host ''
    Write-Host "::error::Windows build failed in step '$($script:CurrentStep)': $($_.Exception.Message)"
    Write-Host ($_.ScriptStackTrace)
    exit 1
}
