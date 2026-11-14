# RPM spec for okapi.
#
# Build with:  packaging/rpm/build-rpm.sh
# or manually:
#   rpmbuild -bb packaging/rpm/okapi.spec \
#     --define "_topdir $(pwd)/build/rpm" --define "version 0.1.0"
#
# Dependencies are the Qt 6 packages; the optional poppler-cpp, tesseract
# and libarchive integrations are enabled automatically when the -devel
# packages are installed at build time.

Name:           okapi
Version:        %{?version}%{!?version:0.1.0}
Release:        1%{?dist}
Summary:        A desktop chat client for local and cloud LLM inference backends

License:        GPL-3.0-or-later
URL:            https://github.com/sschmidhuber/__REPOOKAPI__
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  cmake >= 3.21
BuildRequires:  gcc-c++ >= 13
BuildRequires:  ninja-build
BuildRequires:  qt6-qtbase-devel
BuildRequires:  qt6-qtbase-gui
# Optional attachment text extraction (auto-detected by CMake)
# BuildRequires: poppler-cpp-devel tesseract-devel libarchive-devel

Requires:       qt6-qtbase
Requires:       qt6-qtbase-gui
Requires:       hicolor-icon-theme

%description
okapi is a desktop chat client for agentic work. It talks to local
inference servers (Ollama, llama.cpp, vLLM) and cloud APIs (OpenAI,
Anthropic, Google Gemini, Mistral AI, OpenRouter, and any
OpenAI-compatible endpoint), locally or on remote machines.

Features:
* Streaming chat over Server-Sent Events with live token throughput (t/s)
* Reasoning models: thinking phases shown as collapsible elements in the chat
* MCP (Model Context Protocol) integration: connect HTTP/SSE or stdio
  servers and use their tools, resources and prompts, with per-tool
  permissions (off / ask / allow); tool calls and results are shown
  inline in the chat
* File attachments: images, PDFs, office documents, archives, e-mails
  and text files, with thumbnail previews and automatic text extraction
  (Poppler, Tesseract OCR, libarchive; optional at build time)
* Speech-to-text dictation with live transcription while you speak
* Text-to-speech: assistant responses can be read aloud, synthesized
  sentence-by-sentence while the response streams in
* Prompt templates, insertable via slash commands in the composer
* Chat history grouped by date in the sidebar; everything stored locally
  in an SQLite database in the XDG data directory; messages can be
  edited and resent
* KDE Plasma look & feel, works on GNOME and other desktops; follows
  freedesktop standards (XDG desktop entry)

%prep
%autosetup -n %{name}-%{version}

%build
%cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DOKAPI_BUILD_TESTS=OFF \
    -DCMAKE_INSTALL_PREFIX=%{_prefix}
%cmake_build

%install
%cmake_install

%files
%license LICENSE
%{_bindir}/okapi
%{_datadir}/applications/dev.sschmidhuber.okapi.desktop
%{_datadir}/icons/hicolor/*/apps/okapi.png
%{_datadir}/metainfo/dev.sschmidhuber.okapi.appdata.xml

%changelog
* Wed Oct 21 2026 okapi <okapi@sschmidhuber.dev> - 0.1.0-1
- Initial package
