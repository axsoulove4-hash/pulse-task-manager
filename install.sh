#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
if [[ $(uname -s) != Linux ]]; then echo 'Pulse nécessite Linux.' >&2; exit 1; fi
if [[ ${1:-} == --install-deps ]]; then ./packaging/dependencies.sh --install; fi
if ! command -v cmake >/dev/null; then ./packaging/dependencies.sh; exit 1; fi
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j "$(nproc)"
# Replace atomically, including when an older version is running.
mkdir -p "$HOME/.local/bin"
install -m755 build/pulse "$HOME/.local/bin/pulse.new"
mv -f "$HOME/.local/bin/pulse.new" "$HOME/.local/bin/pulse"
install -Dm644 logo.svg "$HOME/.local/share/icons/hicolor/scalable/apps/pulse.svg"
mkdir -p "$HOME/.local/share/applications"
cat > "$HOME/.local/share/applications/pulse.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=Pulse
GenericName=Gestionnaire de tâches
Comment=Processus, performances et personnalisation
Exec="$HOME/.local/bin/pulse"
Icon=$HOME/.local/share/icons/hicolor/scalable/apps/pulse.svg
Terminal=false
Categories=System;Monitor;
Keywords=task;processus;CPU;RAM;monitor;
StartupNotify=true
DESKTOP
if command -v update-desktop-database >/dev/null; then update-desktop-database "$HOME/.local/share/applications"; fi

if command -v kbuildsycoca6 >/dev/null; then kbuildsycoca6 >/dev/null 2>&1 || true; fi
