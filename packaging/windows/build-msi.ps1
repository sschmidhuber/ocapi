# Build an MSI installer for ocapi using WiX v4/v5 and CPack.
#
# Requirements (Windows 10/11):
#   - Visual Studio with the "C++ CMake tools for Windows" component, or a
#     standalone CMake + Ninja + MSVC toolchain
#   - Qt 6 (MSVC 2022 x64), e.g. via the Qt online installer or aqtinstall
#   - WiX Toolset:  dotnet tool install --global wix
#     (provides wix.exe; CPack finds it via the WIX environment variable or PATH)
#
# Usage (PowerShell):
#   packaging\windows\build-msi.ps1 [-Version 0.1.0] [-QtDir C:\Qt\6.8.2\msvc2022_64]
#
# Produces: dist\ocapi-<Version>-x64.msi
#
# The MSI is NOT signed. Signing an MSI for distribution requires a
# code-signing certificate; sign afterwards with:
#   signtool sign /fd SHA256 /a /tr http://timestamp.digicert.com /td SHA256 ocapi-<Version>-x64.msi
param(
    [string]$Version = "",
    [string]$QtDir = "$env:Qt6_DIR",
    [string]$OutDir = "$PSScriptRoot\..\..\dist"
)

# Default to a date-based version (YYYY.MM.DD) when none is given.
if (-not $Version) { $Version = (Get-Date).ToUniversalTime().ToString("yyyy.MM.dd") }

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path "$PSScriptRoot\..\.."
$BuildDir = Join-Path $env:TEMP "ocapi-msi-build"
$MsiFile = Join-Path $OutDir "ocapi-$Version-x64.msi"

foreach ($tool in @("cmake", "ninja", "cpack")) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        Write-Error "Required tool '$tool' not found in PATH."
    }
}
$wix = Get-Command wix -ErrorAction SilentlyContinue
if (-not $wix) {
    Write-Error "WiX toolset not found. Install with: dotnet tool install --global wix"
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
Remove-Item -Recurse -Force $BuildDir -ErrorAction SilentlyContinue

Write-Host ">> Configuring ocapi $Version (CPack WiX generator)"
$cmakeArgs = @(
    "-S", $RepoRoot,
    "-B", $BuildDir,
    "-G", "Ninja",
    "-DCMAKE_BUILD_TYPE=Release",
    "-DOCAPI_BUILD_TESTS=OFF",
    "-DCPACK_GENERATOR=WIX",
    "-DCPACK_PACKAGE_VERSION=$Version",
    "-DCPACK_WIX_PRODUCT_ICON=$RepoRoot\resources\icons\ocapi.ico"
)
if ($QtDir) { $cmakeArgs += "-DCMAKE_PREFIX_PATH=$QtDir" }
& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { Write-Error "CMake configure failed" }

Write-Host ">> Building"
& cmake --build $BuildDir --config Release
if ($LASTEXITCODE -ne 0) { Write-Error "Build failed" }

Write-Host ">> Deploying Qt runtime (windeployqt)"
$windeployqt = Get-Command windeployqt -ErrorAction SilentlyContinue
if (-not $windeployqt -and $QtDir) {
    $windeployqt = Get-Item (Join-Path $QtDir "bin\windeployqt.exe") -ErrorAction SilentlyContinue
}
if (-not $windeployqt) { Write-Error "windeployqt not found; pass -QtDir pointing at your Qt installation" }
$AppExe = Join-Path $BuildDir "src\Release\ocapi.exe"
if (-not (Test-Path $AppExe)) { $AppExe = Join-Path $BuildDir "src\ocapi.exe" }
$InstallRoot = Join-Path $BuildDir "install"
New-Item -ItemType Directory -Force -Path $InstallRoot | Out-Null
& $windeployqt.Source --release --no-translations --dir $InstallRoot $AppExe
if ($LASTEXITCODE -ne 0) { Write-Error "windeployqt failed" }
# CPack WiX packages the CMake install tree; point it at the deployed one.
$env:OCAPI_WINDEPLOY_DIR = $InstallRoot

Write-Host ">> Creating MSI with CPack"
Push-Location $BuildDir
try {
    & cpack -G WIX -C Release -R $Version
    if ($LASTEXITCODE -ne 0) { Write-Error "CPack failed" }
    $built = Get-ChildItem -Filter "ocapi-$Version-*.msi" | Select-Object -First 1
    if (-not $built) { Write-Error "CPack did not produce an MSI" }
    Copy-Item $built.FullName -Destination $MsiFile -Force
} finally {
    Pop-Location
}

Write-Host ">> Done: $MsiFile"
Write-Host ">> The MSI is unsigned; sign it with signtool before distribution."
