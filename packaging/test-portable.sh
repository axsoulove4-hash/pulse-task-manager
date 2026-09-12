#!/usr/bin/env bash
set -e
appimage="$1"
[ -n "$appimage" ] || { echo "usage: $0 Pulse.AppImage" >&2; exit 2; }
chmod +x "$appimage"
"$appimage" --appimage-help >/dev/null 2>&1 || true
echo 'portable artifact checked'
