# Packaging

Scripts and manifests to produce installable packages for ocapi.

| Platform | Format | Script |
|---|---|---|
| Linux | Flatpak bundle | `flatpak/build-flatpak.sh` |
| Linux (Fedora/RHEL/openSUSE) | RPM | `rpm/build-rpm.sh` |
| Linux (Debian/Ubuntu) | DEB | `deb/build-deb.sh` |
| Windows | MSIX | `windows/build-msix.ps1` |
| Windows | MSI (WiX via CPack) | `windows/build-msi.ps1` |
| Windows | EXE installer (NSIS via CPack) | `windows/build-exe.ps1` |

All artifacts are written to `dist/` by default (override with the second
argument on the Linux scripts or `-OutDir` on the Windows scripts).

If no version is passed, the scripts default to a **date-based version**
(`YYYY.MM.DD`, UTC).

## Automated builds (GitHub Actions)

The repository has a manually triggered workflow,
[`.github/workflows/release.yml`](../.github/workflows/release.yml), that
builds all six package formats. It is **not** part of the CI pipeline
(`ci.yml` only builds and tests); trigger it from the GitHub UI:
**Actions → Release packages → Run workflow**.

- The version defaults to the current UTC date (`YYYY.MM.DD`); override it
  with the `version` input.
- Artifacts (`.flatpak`, `.rpm`, `.deb`, `.msix`, `.msi`, `.exe`) are
  uploaded to the workflow run as downloadable artifacts.
- If the "attach to release" option is enabled (default) and a GitHub
  release exists for the tag `v<version>`, the artifacts are attached to
  that release; otherwise a draft release is created.

## Signing

None of the produced packages are signed. Signing requires a
code-signing certificate (Windows) or a GPG key registered with the
distribution target (Linux repositories), which is environment-specific
and therefore not automated here:

- **Flatpak**: `flatpak build-bundle` produces an unsigned bundle;
  repository distribution requires GPG signing of the OSTree repo.
- **RPM**: sign with `rpm --addsign <pkg>.rpm` after configuring
  `%_gpg_name` in `~/.rpmmacros`.
- **DEB**: sign with `debsign <pkg>.deb` (or `dpkg-sig`) for repository
  upload; a plain unsigned `.deb` installs fine via `apt install ./pkg.deb`.
- **MSIX**: sign with `signtool sign` using a trusted certificate;
  installing an unsigned MSIX requires Windows developer mode.
- **MSI / EXE**: sign with `signtool sign /fd SHA256 /a ...` after the
  build; unsigned installers trigger SmartScreen warnings.

## Linux

### Flatpak

```sh
flatpak install flathub org.kde.Platform//6.10 org.kde.Sdk//6.10
packaging/flatpak/build-flatpak.sh 0.1.0 dist
flatpak install dist/dev.sschmidhuber.ocapi-0.1.0.flatpak
```

The manifest (`flatpak/dev.sschmidhuber.ocapi.yml`) builds from the git
checkout. It uses the KDE Platform/Sdk 6.10 runtime (Qt 6.10). The
optional poppler/tesseract/libarchive integrations are not bundled; the
app works without them (attachment text extraction is disabled).

### RPM

```sh
# Fedora:  sudo dnf install rpm-build cmake ninja-build gcc-c++ qt6-qtbase-devel
packaging/rpm/build-rpm.sh 0.1.0 dist
sudo dnf install dist/ocapi-0.1.0-1.*.rpm
```

The spec is `rpm/ocapi.spec`. To also enable attachment text extraction,
add `poppler-cpp-devel tesseract-devel libarchive-devel` to
`BuildRequires` and install them before building.

### DEB

```sh
# Debian/Ubuntu:  sudo apt-get install build-essential cmake ninja-build \
#     qt6-base-dev qt6-base-dev-tools libqt6sql6-sqlite libgl-dev dpkg-dev
packaging/deb/build-deb.sh 0.1.0 dist
sudo apt install ./dist/ocapi_0.1.0-1_amd64.deb
```

## Windows

All three scripts are PowerShell and expect a Qt 6 (MSVC 2022 x64)
installation. Pass `-QtDir` (e.g. `C:\Qt\6.8.2\msvc2022_64`) so CMake and
`windeployqt` can be found.

### MSIX

```powershell
# Needs: Windows SDK (makeappx.exe), WiX not required
packaging\windows\build-msix.ps1 -Version 0.1.0 -QtDir C:\Qt\6.8.2\msvc2022_64
```

Produces `dist\ocapi-0.1.0-x64.msix`. The manifest is
`windows/Package.appxmanifest` (identity `sschmidhuber.ocapi`, full-trust
desktop app). Store logos under `windows/Assets/` should be added as real
PNGs before publishing to the Store.

### MSI (WiX)

```powershell
# Needs: dotnet tool install --global wix
dotnet tool update --global wix
packaging\windows\build-msi.ps1 -Version 0.1.0 -QtDir C:\Qt\6.8.2\msvc2022_64
```

Produces `dist\ocapi-0.1.0-x64.msi` via CPack's WiX generator. The
upgrade GUID and product icon are configured in the top-level
`CMakeLists.txt`.

### EXE installer (NSIS)

```powershell
# Needs: winget install NSIS.NSIS
packaging\windows\build-exe.ps1 -Version 0.1.0 -QtDir C:\Qt\6.8.2\msvc2022_64
```

Produces `dist\ocapi-0.1.0-windows-x64.exe`, a self-contained installer
that bundles the Qt runtime via `windeployqt`.

## Files

```
packaging/
  README.md                              this file
  linux/dev.sschmidhuber.ocapi.appdata.xml   AppStream metadata (rpm/deb)
  flatpak/dev.sschmidhuber.ocapi.yml     Flatpak manifest
  flatpak/build-flatpak.sh               Flatpak bundle builder
  rpm/ocapi.spec                         RPM spec
  rpm/build-rpm.sh                       RPM builder
  deb/build-deb.sh                       DEB builder
  windows/Package.appxmanifest           MSIX manifest
  windows/build-msix.ps1                 MSIX builder
  windows/build-msi.ps1                  MSI builder (WiX/CPack)
  windows/build-exe.ps1                  EXE installer builder (NSIS/CPack)
```
