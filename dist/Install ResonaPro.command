#!/bin/bash
#
# ResonaPro installer
#
# Double-click this file to install, or run it from Terminal:
#
#     ./Install\ ResonaPro.command
#
# It copies the three plug-in bundles that sit next to it into your user plug-in
# folders. Everything goes inside your home folder, except the standalone app,
# which goes to /Applications when that is writable and to ~/Applications when it
# is not. No administrator password is needed.
#
# Anything replaced is kept, not deleted, in:
#     ~/Library/Application Support/ResonaPro/
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

VST3_SRC="${ROOT}/ResonaPro.vst3"
VST2_SRC="${ROOT}/ResonaPro.vst"
AU_SRC="${ROOT}/ResonaPro.component"
APP_SRC="${ROOT}/ResonaPro.app"

VST3_DST="${HOME}/Library/Audio/Plug-Ins/VST3"
VST2_DST="${HOME}/Library/Audio/Plug-Ins/VST"
AU_DST="${HOME}/Library/Audio/Plug-Ins/Components"

if [ -w "/Applications" ]; then
    APP_DST="/Applications"
else
    APP_DST="${HOME}/Applications"
fi

printf '\n  ResonaPro installer\n\n'

missing=0
if [ ! -d "${VST3_SRC}" ]; then echo "  missing: ResonaPro.vst3";        missing=1; fi
if [ ! -d "${AU_SRC}" ];   then echo "  missing: ResonaPro.component";  missing=1; fi
if [ "${missing}" = "1" ]; then
    printf '\n  Keep this script in the same folder as the plug-ins and run it again.\n\n'
    if [ -t 0 ]; then read -r -p "  Press return to close. " _; fi
    exit 1
fi

mkdir -p "${VST3_DST}" "${AU_DST}" "${APP_DST}"
# VST2 is a legacy format and is only present when the build was given a VST2
# SDK. Most hosts no longer need it, so it is optional here.
[ -d "${VST2_SRC}" ] && mkdir -p "${VST2_DST}"

stamp="$(date +%Y%m%d-%H%M%S)"
backup="${HOME}/Library/Application Support/ResonaPro/replaced-${stamp}"

install_bundle () {
    local src="$1" dst="$2" name="$3"
    local stage="${dst}/${name}.installing-${stamp}"

    rm -rf "${stage}"
    ditto "${src}" "${stage}"

    # Files that arrive by download carry a quarantine flag, and macOS refuses to
    # load a plug-in that has one. Clear it on the copy we are about to install.
    xattr -cr "${stage}" 2>/dev/null || true

    # The bundles are ad-hoc signed rather than Developer ID signed, so simply
    # copying them can leave the signature stale. Sign the installed copy again.
    codesign --force --sign - "${stage}" >/dev/null 2>&1 || true

    # Move the previous copy aside rather than overwriting it, so a reinstall can
    # always be undone.
    if [ -e "${dst}/${name}" ]; then
        mkdir -p "${backup}"
        mv "${dst}/${name}" "${backup}/${name}"
    fi

    mv "${stage}" "${dst}/${name}"
    printf '  installed  %-20s %s\n' "${name}" "${dst}/${name}"
}

install_bundle "${VST3_SRC}" "${VST3_DST}" "ResonaPro.vst3"
if [ -d "${VST2_SRC}" ]; then
    install_bundle "${VST2_SRC}" "${VST2_DST}" "ResonaPro.vst"
fi
install_bundle "${AU_SRC}"   "${AU_DST}"   "ResonaPro.component"

if [ -d "${APP_SRC}" ]; then
    install_bundle "${APP_SRC}" "${APP_DST}" "ResonaPro.app"
fi

# The Audio Unit registry caches component metadata, so nudge it to notice the
# new build instead of waiting for the next restart.
LSREG="/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister"
"${LSREG}" -f "${AU_DST}/ResonaPro.component" >/dev/null 2>&1 || true
killall -9 AudioComponentRegistrar >/dev/null 2>killall AudioComponentRegistrar >/dev/null 2>&11 || true || true

cat <<REPORT

  Done.

    VST3    ${VST3_DST}/ResonaPro.vst3
    AU      ${AU_DST}/ResonaPro.component
REPORT

if [ -d "${VST2_SRC}" ]; then
    printf '    VST2    %s/ResonaPro.vst\n' "${VST2_DST}"
fi

if [ -d "${APP_SRC}" ]; then
    printf '    App     %s/ResonaPro.app\n' "${APP_DST}"
fi

cat <<REPORT

  In your DAW:

    1. Open the plug-in manager and rescan the plug-in folders.
    2. ResonaPro appears under the manufacturer ResonAudio.
    3. The version text under the title should read v2.3.0.

  Anything this replaced was kept in:
    ${backup}

REPORT

if [ -t 0 ]; then read -r -p "  Press return to close. " _; fi
