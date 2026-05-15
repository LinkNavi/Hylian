# install.ps1 — Hylian toolchain installer for Windows
# Usage:  irm hylian.lol/install.ps1 | iex
#
# Installs:
#   %LOCALAPPDATA%\hylian\bin\hylian.exe   — compiler binary
#   %LOCALAPPDATA%\hylian\bin\linkle.bat   — build system wrapper
#   %LOCALAPPDATA%\hylian\lib\std\         — stdlib .o and .hyi files
#   %LOCALAPPDATA%\hylian\lib\linkle.py    — build system source
#
# Adds %LOCALAPPDATA%\hylian\bin to the current user's PATH if needed.

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# ── Colours ───────────────────────────────────────────────────────────────────

function Write-Ok   { param($msg) Write-Host "  $([char]0x2713) $msg" -ForegroundColor Green  }
function Write-Fail { param($msg) Write-Host "  $([char]0x2717) $msg" -ForegroundColor Red    }
function Write-Info { param($msg) Write-Host "  $([char]0xBB) $msg"   -ForegroundColor Cyan   }
function Write-Warn { param($msg) Write-Host "  ! $msg"               -ForegroundColor Yellow }
function Write-Step { param($msg) Write-Host "`n$msg"                 -ForegroundColor White  }

function Write-Header {
    Write-Host ""
    Write-Host "  ██╗  ██╗██╗   ██╗██╗     ██╗ █████╗ ███╗  ██╗" -ForegroundColor Cyan
    Write-Host "  ██║  ██║╚██╗ ██╔╝██║     ██║██╔══██╗████╗ ██║" -ForegroundColor Cyan
    Write-Host "  ███████║ ╚████╔╝ ██║     ██║███████║██╔██╗██║" -ForegroundColor Cyan
    Write-Host "  ██╔══██║  ╚██╔╝  ██║     ██║██╔══██║██║╚████║" -ForegroundColor Cyan
    Write-Host "  ██║  ██║   ██║   ███████╗██║██║  ██║██║ ╚███║" -ForegroundColor Cyan
    Write-Host "  ╚═╝  ╚═╝   ╚═╝   ╚══════╝╚═╝╚═╝  ╚═╝╚═╝  ╚══╝" -ForegroundColor Cyan
    Write-Host ""
}

# ── Paths ─────────────────────────────────────────────────────────────────────

$Base       = Join-Path $env:LOCALAPPDATA "hylian"
$BinDir     = Join-Path $Base "bin"
$LibDir     = Join-Path $Base "lib"
$StdDir     = Join-Path $LibDir "std"
$PlatDir    = Join-Path $StdDir "platform"
$TmpDir     = Join-Path ([System.IO.Path]::GetTempPath()) "hylian-install"
$Registry   = if ($env:HYLIAN_REGISTRY) { $env:HYLIAN_REGISTRY } else { "https://www.hylian.lol" }

# ── Helpers ───────────────────────────────────────────────────────────────────

function Get-LatestVersion {
    try {
        $resp = Invoke-RestMethod "$Registry/api/release/latest?channel=stable" -ErrorAction Stop
        return $resp.version
    } catch {
        return $null
    }
}

function Download-Component {
    param($Component, $Version, $Dest)
    $url = "$Registry/api/release/download?channel=stable&version=$Version&component=$Component"
    Write-Info "Downloading $Component $Version..."
    Invoke-WebRequest -Uri $url -OutFile $Dest -UseBasicParsing
}

function Verify-Checksum {
    param($FilePath, $Expected)
    $hash = (Get-FileHash $FilePath -Algorithm SHA256).Hash.ToLower()
    if ($hash -ne $Expected.ToLower()) {
        Write-Fail "Checksum mismatch for $FilePath"
        Write-Fail "  expected: $Expected"
        Write-Fail "  got:      $hash"
        throw "Checksum verification failed"
    }
    Write-Ok "Checksum verified"
}

function Expand-Tarball {
    param($TarPath, $OutDir)
    # Use tar.exe (available in Windows 10 1803+)
    if (Get-Command tar -ErrorAction SilentlyContinue) {
        & tar -xzf $TarPath -C $OutDir
    } else {
        # Fallback: 7-Zip if available
        $7z = "C:\Program Files\7-Zip\7z.exe"
        if (Test-Path $7z) {
            & $7z x $TarPath -o"$OutDir" -y | Out-Null
        } else {
            throw "Cannot extract tarball: neither tar.exe nor 7-Zip found. Install Windows 10 1803+ or 7-Zip."
        }
    }
}

function Add-ToPath {
    param($Dir)
    $currentPath = [System.Environment]::GetEnvironmentVariable("PATH", "User")
    if ($currentPath -notlike "*$Dir*") {
        [System.Environment]::SetEnvironmentVariable(
            "PATH",
            "$currentPath;$Dir",
            "User"
        )
        Write-Ok "Added $Dir to your PATH (takes effect in new terminals)"
    } else {
        Write-Info "$Dir is already in PATH"
    }
}

# ── Main ──────────────────────────────────────────────────────────────────────

Write-Header
Write-Host "  Installing Hylian toolchain to $Base" -ForegroundColor White

# Check prerequisites
Write-Step "Checking prerequisites"

if (-not (Get-Command python3 -ErrorAction SilentlyContinue) -and
    -not (Get-Command python  -ErrorAction SilentlyContinue)) {
    Write-Warn "Python 3 not found in PATH."
    Write-Warn "Download it from https://python.org and re-run this script."
    Write-Warn "Continuing anyway — some features may not work."
}

$PythonCmd = if (Get-Command python3 -ErrorAction SilentlyContinue) { "python3" } else { "python" }
Write-Ok "Python: $PythonCmd"

# Fetch latest stable version
Write-Step "Fetching latest stable release info"
$LatestVersion = Get-LatestVersion
if (-not $LatestVersion) {
    Write-Fail "Could not reach $Registry — check your internet connection."
    exit 1
}
Write-Ok "Latest stable: $LatestVersion"

# Fetch checksums
$MetaResp = Invoke-RestMethod "$Registry/api/release/latest?channel=stable"
$ChecksumMap = @{}
foreach ($comp in $MetaResp.components) {
    $ChecksumMap[$comp.component] = $comp.checksum
}

# Create directories
Write-Step "Creating directories"
foreach ($d in @($BinDir, $LibDir, $StdDir, $PlatDir, $TmpDir)) {
    if (-not (Test-Path $d)) {
        New-Item -ItemType Directory -Path $d -Force | Out-Null
        Write-Ok "Created $d"
    }
}

# Download & extract components
Write-Step "Downloading compiler"
$CompilerTar = Join-Path $TmpDir "compiler.tar.gz"
$CompilerOut = Join-Path $TmpDir "compiler"
New-Item -ItemType Directory -Path $CompilerOut -Force | Out-Null
Download-Component "compiler" $LatestVersion $CompilerTar
if ($ChecksumMap["compiler"]) { Verify-Checksum $CompilerTar $ChecksumMap["compiler"] }
Expand-Tarball $CompilerTar $CompilerOut

# Find and install hylian binary (could be hylian or hylian.exe)
$HylianBin = Get-ChildItem $CompilerOut -Recurse -Filter "hylian*" | Select-Object -First 1
if (-not $HylianBin) {
    Write-Fail "Compiler binary not found in tarball."
    exit 1
}
Copy-Item $HylianBin.FullName (Join-Path $BinDir "hylian.exe") -Force
Write-Ok "hylian  →  $BinDir\hylian.exe"

Write-Step "Downloading stdlib"
$StdlibTar = Join-Path $TmpDir "stdlib.tar.gz"
$StdlibOut = Join-Path $TmpDir "stdlib"
New-Item -ItemType Directory -Path $StdlibOut -Force | Out-Null
Download-Component "stdlib" $LatestVersion $StdlibTar
if ($ChecksumMap["stdlib"]) { Verify-Checksum $StdlibTar $ChecksumMap["stdlib"] }
Expand-Tarball $StdlibTar $StdlibOut

# Copy runtime tree into $StdDir
$RuntimeSrc = Join-Path $StdlibOut "runtime"
if (Test-Path $RuntimeSrc) {
    Copy-Item "$RuntimeSrc\*" $StdDir -Recurse -Force
    Write-Ok "stdlib  →  $StdDir"
} else {
    Write-Warn "Could not find runtime/ in stdlib tarball."
}

Write-Step "Downloading Linkle"
$LinkleTar = Join-Path $TmpDir "linkle.tar.gz"
$LinkleOut = Join-Path $TmpDir "linkle"
New-Item -ItemType Directory -Path $LinkleOut -Force | Out-Null
Download-Component "linkle" $LatestVersion $LinkleTar
if ($ChecksumMap["linkle"]) { Verify-Checksum $LinkleTar $ChecksumMap["linkle"] }
Expand-Tarball $LinkleTar $LinkleOut

$LinklePy = Get-ChildItem $LinkleOut -Recurse -Filter "linkle.py" | Select-Object -First 1
if (-not $LinklePy) {
    Write-Fail "linkle.py not found in tarball."
    exit 1
}
Copy-Item $LinklePy.FullName (Join-Path $LibDir "linkle.py") -Force
Write-Ok "linkle.py  →  $LibDir\linkle.py"

# Write linkle.bat wrapper
$LinkleBat = @"
@echo off
set HYLIAN_LIB=$LibDir
$PythonCmd "$LibDir\linkle.py" %*
"@
Set-Content (Join-Path $BinDir "linkle.bat") $LinkleBat -Encoding ASCII
Write-Ok "linkle.bat  →  $BinDir\linkle.bat"

# Persist config dir and write version
$ConfigDir = Join-Path $env:USERPROFILE ".hylian"
if (-not (Test-Path $ConfigDir)) {
    New-Item -ItemType Directory -Path $ConfigDir -Force | Out-Null
}
Set-Content (Join-Path $ConfigDir "version") "channel=stable`nversion=$LatestVersion" -Encoding UTF8

# Add to PATH
Write-Step "Updating PATH"
Add-ToPath $BinDir

# Cleanup
Remove-Item $TmpDir -Recurse -Force -ErrorAction SilentlyContinue

# Done
Write-Host ""
Write-Host "══════════════════════════════════════════" -ForegroundColor Green
Write-Host "  Hylian $LatestVersion installed successfully!" -ForegroundColor Green
Write-Host "══════════════════════════════════════════" -ForegroundColor Green
Write-Host ""
Write-Host "  Compiler:  $BinDir\hylian.exe"
Write-Host "  Build sys: $BinDir\linkle.bat"
Write-Host "  Stdlib:    $StdDir"
Write-Host ""
Write-Host "  Open a new terminal and run:" -ForegroundColor Cyan
Write-Host "    linkle new myapp" -ForegroundColor DarkGray
Write-Host "    cd myapp" -ForegroundColor DarkGray
Write-Host "    linkle run" -ForegroundColor DarkGray
Write-Host ""
