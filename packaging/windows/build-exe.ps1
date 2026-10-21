# Build a self-contained EXE installer for ocapi using CPack's NSIS generator.
#
# Requirements (Windows 10/11):
#   - Visual Studio with the "C++ CMake tools for Windows" component, or a
#     standalone CMake + Ninja + MSVC toolchain
#   - Qt 6 (MSVC 2022 x64), e.g. via the Qt online installer or aqtinstall
#   - NSIS (installer script compiler):  winget install NSIS.NSIS
#     (CPack finds makensis.exe via PATH)
#
# Usage (PowerShell):
#   packaging\windows\build-exe.ps1 [-Version 0.1.0] [-QtDir C:\Qt\6.8.2\msvc2022_64]
#
# Produces: dist\ocapi-<Version>-windows-x64.exe
#
# The installer is NOT signed. Signing the produced .exe for distribution
# requires a code-signing certificate; sign afterwards with:
#   signtool sign /fd SHA256 /a /tr http://timestamp.digicert.com /td SHA256 ocapi-<Version>-windows-x64.exe
param(
    [string]$Version = "",
    [string]$QtDir = "$env:Qt6_DIR",
    [string]$OutDir = "$PSScriptRoot\..\..\dist"
)

# Default to a date-based version (YYYY.MM.DD) when none is given.
if (-not $Version) { $Version = (Get-Date).ToUniversalTime().ToString("yyyy.MM.dd") }

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path "$PSScriptRoot\..\.."
$BuildDir = Join-Path $env:TEMP "ocapi-exe-build"
$ExeFile = Join-Path $OutDir "ocapi-$Version-windows-x64.exe"

foreach ($tool in @("cmake", "ninja", "cpack", "makensis")) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        Write-Error "Required tool '$tool' not found in PATH."
    }
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
Remove-Item -Recurse -Force $BuildDir -ErrorAction SilentlyContinue

Write-Host ">> Configuring ocapi $Version (CPack NSIS generator)"
$cmakeArgs = @(
    "-S", $RepoRoot,
    "-B", $BuildDir,
    "-G", "Ninja",
    "-DCMAKE_BUILD_TYPE=Release",
    "-DOCAPI_BUILD_TESTS=OFF",
    "-DCPACK_GENERATOR=NSIS64",
    "-DCPACK_PACKAGE_VERSION=$Version",
    "-DCPACK_NSIS_MUI_ICON=$RepoRoot\resources\icons\ocapi.ico",
    "-DCPACK_NSIS_MUI_UNIICON=$RepoRoot\resources\icons\ocapi.ico",
    "-DCPACK_NSIS_INSTALLED_ICON_NAME=bin\\ocapi.exe",
    "-DCPACK_NSIS_DISPLAY_NAME=ocapi",
    "-DCPACK_NSIS_HELP_LINK=https://github.com/sschmidhuber/ocapi",
    "-DCPACK_NSIS_URL_INFO_ABOUT=https://github.com/sschmidhuber/ocapi",
    "-DCPACK_NSIS_CONTACT=https://github.com/sschmidhuber/ocapi",
    "-DCPACK_NSIS_MODIFY_PATH=ON"
)
if ($QtDir) { $cmakeArgs += "-DCMAKE_PREFIX_PATH=$QtDir" }
& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { Write-Error "CMake configure failed" }

Write-Host ">> Building"
& cmake --build $BuildDir --config Release
if ($LASTEXITCODE -ne 0) { Write-Error "Build failed" }

Write-Host ">> Deploying Qt runtime (windeployqt) into the CPack install tree"
$windeployqt = Get-Command windeployqt -ErrorAction SilentlyContinue
if (-not $windeployqt -and $QtDir) {
    $windeployqt = Get-Item (Join-Path $QtDir "bin\windeployqt.exe") -ErrorAction SilentlyContinue
}
if (-not $windeployqt) { Write-Error "windeployqt not found; pass -QtDir pointing at your Qt installation" }
$AppExe = Join-Path $BuildDir "src\Release\ocapi.exe"
if (-not (Test-Path $AppExe)) { $AppExe = Join-Path $BuildDir "src\ocapi.exe" }
$InstallRoot = Join-Path $BuildDir "_CPack_Packages\Windows\NSIS"
# windeployqt into the directory CPack will package: install the app first.
& cmake --install $BuildDir --prefix "" --config Release
$DeployTarget = Join-Path $BuildDir "bin"
if (-not (Test-Path (Join-Path $DeployTarget "ocapi.exe"))) {
    # Fallback: deploy next to the built exe and let CPack's install rules pick it up.
    $DeployTarget = Split-Path $AppExe -Parent
}
& $windeployqt.Source --release --no-translations --dir $DeployTarget $AppExe
if ($LASTEXITCODE -ne 0) { Write-Error "windeployqt failed" }

Write-Host ">> Creating EXE installer with CPack"
Push-Location $BuildDir
try {
    & cpack -G NSIS64 -C Release -R $Version
    if ($LASTEXITCODE -ne 0) { Write-Error "CPack failed" }
    $built = Get-ChildItem -Filter "ocapi-$Version-*.exe" | Select-Object -First 1
    if (-not $built) { Write-Error "CPack did not produce an EXE installer" }
    Copy-Item $built.FullName -Destination $ExeFile -Force
} finally {
    Pop-Location
}

Write-Host ">> Done: $ExeFile"
Write-Host ">> The installer is unsigned; sign it with signtool before distribution."
