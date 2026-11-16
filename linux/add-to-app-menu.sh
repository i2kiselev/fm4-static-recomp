#!/usr/bin/env bash
set -euo pipefail
# Adds FM4 Static Recomp to the desktop's application menu for the current user; run it again after moving the folder; "--remove" takes the entry out.
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
apps="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
entry="$apps/fm4-static-recomp.desktop"

if [ "${1:-}" = "--remove" ]; then
    rm -f "$entry"
    echo "Removed FM4 Static Recomp from the app menu."
    if command -v update-desktop-database >/dev/null 2>&1; then
        update-desktop-database "$apps" || true
    fi
    exit 0
fi

if [ ! -x "$here/FM4StaticRecompLauncher" ]; then
    echo "Error: Run this script from the unpacked launcher folder." >&2
    exit 1
fi

mkdir -p "$apps"
cat > "$entry" << EOF
[Desktop Entry]
Type=Application
Name=FM4 Static Recomp
Comment=Install and play your own Forza Motorsport 4 discs
Exec="$here/FM4StaticRecompLauncher"
Path=$here
Icon=$here/fm4-static-recomp.png
Terminal=false
Categories=Game;
EOF

chmod 644 "$entry"
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$apps" || true
fi

echo "Added FM4 Static Recomp to the app menu (entry: $entry)."
echo "Moved this folder? Run this script again. Remove with: $0 --remove"
