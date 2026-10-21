# Build an MSIX package for ocapi.
#
# Requirements (Windows 10/11, run in an elevated or developer-mode shell):
#   - Visual Studio with the "C++ CMake tools for Windows" component, or a
#     standalone CMake + Ninja + MSVC toolchain
#   - Qt 6 (MSVC 2022 x64) installed, e.g. via the Qt online installer or
#     aqtinstall:  pip install aqtinstall && aqt install-qt windows desktop 6.8.2 win64_msvc2022_64
#   - Windows SDK (makeappx.exe, signtool.exe) - part of the "Desktop
#     development with C++" workload
#
# Usage (PowerShell):
#   packaging\windows\build-msix.ps1 [-Version 0.1.0] [-QtDir C:\Qt\6.8.2\msvc2022_64]
#
# Produces: dist\ocapi-<Version>-x64.msix
#
# The MSIX is NOT signed. Installing an unsigned MSIX requires developer
# mode (Settings > Privacy & security > For developers > Developer mode) or
# sideloading via Add-AppxPackage after signing. Signing for the Microsoft
# Store requires a code-signing certificate trusted by Windows; pass
# -CertPath/-CertPassword to sign the package after creation.
param(
    [string]$Version = "",
    [string]$QtDir = "$env:Qt6_DIR",
    [string]$OutDir = "$PSScriptRoot\..\..\dist",
    [string]$CertPath = "",
    [string]$CertPassword = ""
)

# Default to a date-based version (YYYY.MM.DD) when none is given.
if (-not $Version) { $Version = (Get-Date).ToUniversalTime().ToString("yyyy.MM.dd") }

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path "$PSScriptRoot\..\.."
$BuildDir = Join-Path $env:TEMP "ocapi-msix-build"
$PackageDir = Join-Path $env:TEMP "ocapi-msix-pkg"
$Manifest = Join-Path $PSScriptRoot "Package.appxmanifest"
$MsixFile = Join-Path $OutDir "ocapi-$Version-x64.msix"

foreach ($tool in @("cmake", "ninja", "makeappx")) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        Write-Error "Required tool '$tool' not found in PATH."
    }
}

if ($QtDir -and -not (Test-Path $QtDir)) {
    Write-Error "QtDir '$QtDir' does not exist."
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
Remove-Item -Recurse -Force $BuildDir, $PackageDir -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $BuildDir, $PackageDir | Out-Null

Write-Host ">> Configuring ocapi $Version"
$cmakeArgs = @(
    "-S", $RepoRoot,
    "-B", $BuildDir,
    "-G", "Ninja",
    "-DCMAKE_BUILD_TYPE=Release",
    "-DOCAPI_BUILD_TESTS=OFF"
)
if ($QtDir) { $cmakeArgs += "-DCMAKE_PREFIX_PATH=$QtDir" }
& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { Write-Error "CMake configure failed" }

Write-Host ">> Building"
& cmake --build $BuildDir --config Release
if ($LASTEXITCODE -ne 0) { Write-Error "Build failed" }

Write-Host ">> Deploying Qt runtime (windeployqt)"
$windeployqt = Get-Command windeployqt -ErrorAction SilentlyContinue
if (-not $windeployqt) {
    if ($QtDir) { $windeployqt = Get-Item (Join-Path $QtDir "bin\windeployqt.exe") -ErrorAction SilentlyContinue }
}
if (-not $windeployqt) { Write-Error "windeployqt not found; pass -QtDir pointing at your Qt installation" }
$AppExe = Join-Path $BuildDir "src\Release\ocapi.exe"
if (-not (Test-Path $AppExe)) { $AppExe = Join-Path $BuildDir "src\ocapi.exe" }
if (-not (Test-Path $AppExe)) { Write-Error "ocapi.exe not found after build" }
& $windeployqt.Source --release --no-translations --dir $PackageDir $AppExe
if ($LASTEXITCODE -ne 0) { Write-Error "windeployqt failed" }

Write-Host ">> Staging package layout"
Copy-Item $AppExe -Destination $PackageDir
Copy-Item (Join-Path $RepoRoot "resources\icons\ocapi.ico") -Destination (Join-Path $PackageDir "ocapi.ico")
$AssetsDir = Join-Path $PackageDir "Assets"
New-Item -ItemType Directory -Force -Path $AssetsDir | Out-Null
# Store logos: reuse the .ico for all required asset slots.
foreach ($asset in @("StoreLogo.png", "Square150x150Logo.png", "Square44x44Logo.png", "Wide310x150Logo.png")) {
    $src = Join-Path $AssetsDir $asset
    if (-not (Test-Path $src)) {
        Write-Warning "Placeholder asset $asset missing; MSIX packaging needs real PNG logos in Assets\."
        New-Item -ItemType File -Path $src | Out-Null
    }
}
Copy-Item $Manifest -Destination (Join-Path $PackageDir "Package.appxmanifest")
(Get-Content (Join-Path $PackageDir "Package.appxmanifest")) -replace 'Version="0.1.0.0"', "Version="$Version.0"" | Set-Content (Join-Path $PackageDir "Package.appxmanifest")

Write-Host ">> Creating MSIX: $MsixFile"
& makeappx pack /d $PackageDir /p $MsixFile /o
if ($LASTEXITCODE -ne 0) { Write-Error "makeappx pack failed" }

if ($CertPath) {
    Write-Host ">> Signing MSIX with $CertPath"
    & signtool sign /fd SHA256 /a /f $CertPath $(if ($CertPassword) { "/p $CertPassword" }) $MsixFile
    if ($LASTEXITCODE -ne 0) { Write-Error "signtool failed" }
} else {
    Write-Host ">> MSIX is unsigned. Sign it before distribution, or install with developer mode enabled."
}

Write-Host ">> Done: $MsixFile"
