#!/bin/bash
# Installs Peak Editor (app, AU and VST3) from the release zips and clears macOS's quarantine flags.
#
# Put this script in the same folder as the Peak-Editor-<version>-macOS-*.zip files from the
# release (your Downloads folder is fine), then in Terminal:
#
#     bash install-peak-editor.sh            (or: bash install-peak-editor.sh --dry-run)
#
# It installs
#     Peak Editor.app        -> /Applications
#     Peak Editor.component  -> ~/Library/Audio/Plug-Ins/Components   (AU)
#     Peak Editor.vst3       -> ~/Library/Audio/Plug-Ins/VST3         (VST3)
# replacing any earlier version, removes the quarantine flag macOS adds to downloads (so the app and
# plugins open without the "can't be opened" warning), and refreshes the system's AU list.
set -euo pipefail

dry_run=0
[[ "${1:-}" == "--dry-run" ]] && dry_run=1

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
apps="${PEAK_APPS_DIR:-/Applications}"                      # (overridable for testing)
plugins="${PEAK_PLUGINS_DIR:-$HOME/Library/Audio/Plug-Ins}"
run() { if (( dry_run )); then echo "  would run: $*"; else "$@"; fi; }

# The newest zips next to this script (or in Downloads).
find_zip() {
    local kind="$1" dir
    for dir in "$here" "$HOME/Downloads"; do
        local found
        found="$(ls -t "$dir"/Peak-Editor-*-macOS-"$kind".zip 2>/dev/null | head -n 1 || true)"
        [[ -n "$found" ]] && { echo "$found"; return; }
    done
}

app_zip="$(find_zip App)"; au_zip="$(find_zip AU)"; vst3_zip="$(find_zip VST3)"
if [[ -z "$app_zip$au_zip$vst3_zip" ]]; then
    echo "No Peak-Editor-*-macOS-*.zip files found next to this script or in ~/Downloads."
    echo "Download them from the release page, put them with this script, and run it again."
    exit 1
fi

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

install_bundle() {
    local zip="$1" name="$2" dest="$3"
    if [[ -z "$zip" ]]; then
        echo "- $name: no zip found, skipped"
        return
    fi
    echo "- $name  ($(basename "$zip"))"
    ditto -x -k "$zip" "$work/$name.unzipped"
    local bundle="$work/$name.unzipped/$name"
    [[ -d "$bundle" ]] || { echo "  $name isn't in $(basename "$zip")"; exit 1; }
    run mkdir -p "$dest"
    if [[ -e "$dest/$name" ]]; then
        run rm -rf "$dest/$name"
    fi
    run ditto "$bundle" "$dest/$name"
    run xattr -dr com.apple.quarantine "$dest/$name" 2>/dev/null || true
    echo "  installed in $dest"
}

echo "Installing Peak Editor"
if pgrep -f "Peak Editor.app/Contents/MacOS/Peak Editor" >/dev/null 2>&1; then
    echo "- quitting the running Peak Editor"
    run osascript -e 'quit app "Peak Editor"' || true
    sleep 2
fi
install_bundle "$app_zip" "Peak Editor.app" "$apps"
install_bundle "$au_zip" "Peak Editor.component" "$plugins/Components"
install_bundle "$vst3_zip" "Peak Editor.vst3" "$plugins/VST3"

# Let Logic, GarageBand and other AU hosts see the new AU straight away.
run killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true

echo
echo "Done. Open Peak Editor from Applications; in a DAW, rescan plugins if it doesn't appear."
(( dry_run )) && echo "(dry run: nothing was changed)"
exit 0
