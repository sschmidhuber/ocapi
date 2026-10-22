# Sandbox Setup Guide: Build & Test ocapi (Qt 6 / C++23)

Step-by-step guide to set up a fresh sandbox (Debian-based, no root) so that
ocapi can be built and tested. All artifacts live under `/home/appuser/`.

> This guide is for AI coding agent sandboxes. It is not needed for normal
> development on a desktop Linux machine — see [Building](../README.md#building)
> in the README instead.

---

## 1. Prerequisites

The sandbox has:

- Python 3 with `pip`
- `cmake`, `g++`, `make`, `git`, `curl` (system packages)
- **No root access** — everything is installed into `/home/appuser/`
- **No system Qt** — Qt 6.8.2 is installed via `aqtinstall`
- **No system OpenGL/GL dev headers** — downloaded as `.deb` packages

Key directories:

```
/home/appuser/qt/          # Qt 6.8.2 installation (aqtinstall)
/home/appuser/sysroot/     # Extracted .deb packages (OpenGL, X11, audio, etc.)
/home/appuser/debs/        # Downloaded .deb files (for reference/re-install)
/home/appuser/apt/         # Custom apt root (sources.list, cache, lists)
```

## 2. Install Qt 6.8.2 via aqtinstall

```bash
pip install aqtinstall
aqt install-qt linux desktop 6.8.2 linux_gcc_64 \
    -m qtmultimedia qtwebsockets \
    --outputdir /home/appuser/qt
```

This installs Qt to `/home/appuser/qt/6.8.2/gcc_64/`.

## 3. Download & Extract System Dependencies as .deb Files

Since there's no root, download `.deb` packages and extract them with
`dpkg-deb -x` into a sysroot.

### 3a. Set up a custom apt root

```bash
mkdir -p /home/appuser/apt/{etc/apt/sources.list.d,etc/apt/preferences.d,var/lib/apt/lists/partial,var/cache/apt/archives/partial,var/lib/dpkg}
cat > /home/appuser/apt/etc/apt/apt.conf <<'EOF'
APT::Architecture "amd64";
Dir::Etc::sourcelist "/home/appuser/apt/etc/apt/sources.list";
Dir::Etc::sourceparts "/home/appuser/apt/etc/apt/sources.list.d";
Dir::Etc::preferences "/home/appuser/apt/etc/apt/preferences";
Dir::Etc::preferencesparts "/home/appuser/apt/etc/apt/preferences.d";
Dir::State::lists "/home/appuser/apt/var/lib/apt/lists";
Dir::Cache "/home/appuser/apt/var/cache/apt";
Dir::State::status "/home/appuser/apt/var/lib/dpkg/status";
APT::Get::Download-Only "true";
APT::Get::AllowUnauthenticated "true";
EOF
echo "deb http://deb.debian.org/debian bookworm main" > /home/appuser/apt/etc/apt/sources.list
touch /home/appuser/apt/var/lib/dpkg/status
apt-get -c /home/appuser/apt/etc/apt/apt.conf update
```

### 3b. Download the required .deb packages

```bash
mkdir -p /home/appuser/debs && cd /home/appuser/debs
apt-get -c /home/appuser/apt/etc/apt/apt.conf download \
    libgl1-mesa-dev libgl-dev libegl-dev libegl1 libegl-mesa0 \
    libglx-dev libglx0 libglx-mesa0 libglvnd-dev libopengl-dev libopengl0 \
    libgl1 libglib2.0-0 libfontconfig1 libfreetype6 libxkbcommon0 \
    libdbus-1-3 libx11-6 libxcb1 libxcb-render0 libxcb-shm0 libxcb-xfixes0 \
    libxcb-randr0 libxcb-image0 libxcb-icccm4 libxcb-keysyms1 libxcb-sync1 \
    libxcb-xkb1 libxcb-shape0 libxcb-xinerama0 libxcb-render-util0 \
    libicu72 libpcre2-16-0 zlib1g libpng16-16 libharfbuzz0b libbrotli1 \
    libmd0 libbsd0 libexpat1 libpulse0 libasyncns0 libsndfile1 libflac12 \
    libogg0 libvorbis0a libvorbisenc2 libmpg123-0 libmp3lame0 libopus0 \
    libglapi-mesa libgl1-mesa-dri libglvnd0
```

### 3c. Extract all .deb files into the sysroot

```bash
mkdir -p /home/appuser/sysroot
cd /home/appuser/debs
for f in *.deb; do
    dpkg-deb -x "$f" /home/appuser/sysroot
done
```

## 4. Install Python Tools (clang-format, cppcheck, clang-tidy)

```bash
pip install clang-format cppcheck clang-tidy
```

- `clang-format` — version 23.x (pip package `clang-format`)
- `cppcheck` — version 2.22 (pip package `cppcheck`)
- `clang-tidy` — version 23.x (pip package `clang-tidy`)

All end up in `/home/appuser/.local/bin/`.

## 5. Configure CMake

```bash
cd /workspace/github__sschmidhuber__ocapi   # or your repo path
cmake -B build \
    -DCMAKE_PREFIX_PATH="/home/appuser/qt/6.8.2/gcc_64;/home/appuser/sysroot/usr" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DOPENGL_opengl_LIBRARY=/home/appuser/sysroot/usr/lib/x86_64-linux-gnu/libOpenGL.so \
    -DOPENGL_glx_LIBRARY=/home/appuser/sysroot/usr/lib/x86_64-linux-gnu/libGLX.so \
    -DOPENGL_INCLUDE_DIR=/home/appuser/sysroot/usr/include
```

For clang-tidy (needs `compile_commands.json`):

```bash
cmake -B build-tidy \
    -DCMAKE_PREFIX_PATH="/home/appuser/qt/6.8.2/gcc_64;/home/appuser/sysroot/usr" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DOPENGL_opengl_LIBRARY=/home/appuser/sysroot/usr/lib/x86_64-linux-gnu/libOpenGL.so \
    -DOPENGL_glx_LIBRARY=/home/appuser/sysroot/usr/lib/x86_64-linux-gnu/libGLX.so \
    -DOPENGL_INCLUDE_DIR=/home/appuser/sysroot/usr/include \
    -DOCAPI_BUILD_TESTS=ON
```

## 6. Build

```bash
cmake --build build --parallel $(nproc)
```

## 7. Run Tests

```bash
export LD_LIBRARY_PATH=/home/appuser/sysroot/usr/lib/x86_64-linux-gnu:/home/appuser/sysroot/usr/lib/x86_64-linux-gnu/pulseaudio:/home/appuser/sysroot/lib/x86_64-linux-gnu:/home/appuser/qt/6.8.2/gcc_64/lib
export QT_QPA_PLATFORM=offscreen
ctest --test-dir build --output-on-failure
```

Expected: **28/28 tests pass** (as of the markdown + copy PR).

## 8. Static Analysis (CI-equivalent)

### clang-format

```bash
find src tests -name '*.cpp' -o -name '*.h' | xargs clang-format --dry-run --Werror
```

### cppcheck

```bash
cppcheck --enable=warning,style,performance,portability \
    --inline-suppr --std=c++20 --error-exitcode=1 \
    -I src --suppress=missingIncludeSystem --suppress=unknownMacro \
    --suppress=shadowFunction --suppress=useStlAlgorithm \
    src/ tests/
```

### clang-tidy (on changed files)

```bash
export LD_LIBRARY_PATH=/home/appuser/sysroot/usr/lib/x86_64-linux-gnu:/home/appuser/sysroot/usr/lib/x86_64-linux-gnu/pulseaudio:/home/appuser/sysroot/lib/x86_64-linux-gnu:/home/appuser/qt/6.8.2/gcc_64/lib
# Generate .moc files needed by clang-tidy
cmake --build build-tidy --target ocapi-core_autogen ocapi_autogen
# Run clang-tidy on a changed file
clang-tidy -p build-tidy --warnings-as-errors='*' src/core/markdown.cpp
```

Or use the repo's script:

```bash
scripts/clang-tidy-changed.sh
```

## 9. Environment Variables (save these)

Add to `~/.bashrc` or export in each session:

```bash
export PATH="/home/appuser/.local/bin:$PATH"
export LD_LIBRARY_PATH=/home/appuser/sysroot/usr/lib/x86_64-linux-gnu:/home/appuser/sysroot/usr/lib/x86_64-linux-gnu/pulseaudio:/home/appuser/sysroot/lib/x86_64-linux-gnu:/home/appuser/qt/6.8.2/gcc_64/lib
export QT_QPA_PLATFORM=offscreen
```

## 10. Quick Start (if sandbox already has /home/appuser/ populated)

If `/home/appuser/qt`, `/home/appuser/sysroot`, and `/home/appuser/.local/bin/`
already exist (persisted from a previous session), skip to:

```bash
export PATH="/home/appuser/.local/bin:$PATH"
export LD_LIBRARY_PATH=/home/appuser/sysroot/usr/lib/x86_64-linux-gnu:/home/appuser/sysroot/usr/lib/x86_64-linux-gnu/pulseaudio:/home/appuser/sysroot/lib/x86_64-linux-gnu:/home/appuser/qt/6.8.2/gcc_64/lib
export QT_QPA_PLATFORM=offscreen
cd /workspace/github__sschmidhuber__ocapi   # or your repo path
cmake -B build \
    -DCMAKE_PREFIX_PATH="/home/appuser/qt/6.8.2/gcc_64;/home/appuser/sysroot/usr" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DOPENGL_opengl_LIBRARY=/home/appuser/sysroot/usr/lib/x86_64-linux-gnu/libOpenGL.so \
    -DOPENGL_glx_LIBRARY=/home/appuser/sysroot/usr/lib/x86_64-linux-gnu/libGLX.so \
    -DOPENGL_INCLUDE_DIR=/home/appuser/sysroot/usr/include
cmake --build build --parallel $(nproc)
ctest --test-dir build --output-on-failure
```

## 11. Troubleshooting

| Problem | Solution |
|---|---|
| `cmake` can't find Qt | Ensure `CMAKE_PREFIX_PATH` includes both `/home/appuser/qt/6.8.2/gcc_64` and `/home/appuser/sysroot/usr` |
| Linker can't find `-lGL` / `-lEGL` | Pass `-DOPENGL_opengl_LIBRARY` and `-DOPENGL_glx_LIBRARY` pointing to sysroot `.so` files |
| Tests fail with "cannot open shared object" | Check `LD_LIBRARY_PATH` includes all sysroot lib dirs + Qt lib dir |
| `clang-tidy` can't find `.moc` files | Run `cmake --build build-tidy --target ocapi-core_autogen ocapi_autogen` first |
| `clang-format` not found | `pip install clang-format` → binary in `/home/appuser/.local/bin/` |
| `dpkg-deb` not found | It's part of `dpkg`; if missing, use `ar x` + `tar xf` as fallback |

## 12. Repo-Specific Notes

- **CI uses Qt 6.8.2** — match this version exactly.
- **CI uses Ninja** (`-G Ninja`) but Makefiles work fine locally.
- **Tests use QtTest** (`QTEST_GUILESS_MAIN`) — no display needed, but `QT_QPA_PLATFORM=offscreen` is required.
- **Pre-commit hooks** run clang-format, whitespace checks, EOF checks — install with `pip install pre-commit && pre-commit install` if needed.
- **clang-tidy config** is in `.clang-tidy` at repo root (checks: bugprone, cppcoreguidelines, modernize, performance, readability).
- **cppcheck suppressions** are inline in `.github/workflows/ci.yml` — match them exactly.
