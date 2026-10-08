#!/bin/bash
#
# ResonaPro installer
#
# Copies the freshly built AU and VST3 plug-ins into the current user's plug-in
# folders. The old bundles are retained with timestamped .previous suffixes
# instead of deleted before the new copy is complete.
#
#   ./install.sh
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${ROOT}/.work/build/ResonaPro_artefacts/Release"

# Superseded builds are parked here, deliberately outside any folder that a host
# scans. An earlier version of this script renamed the old bundle to
# "ResonaPro.vst3.previous-<stamp>" in place, which left 27 stale bundles sitting
# in the plug-in folders. A plug-in folder should contain plug-ins, and anything
# else in there is a chance for a scanner to pick up the wrong one.
ROLLBACK_DIR="${ROOT}/.work/backups/rollbacks"

VST3_SRC="${BUILD_DIR}/VST3/ResonaPro.vst3"
VST2_SRC="${BUILD_DIR}/VST/ResonaPro.vst"
AU_SRC="${BUILD_DIR}/AU/ResonaPro.component"
STANDALONE_SRC="${BUILD_DIR}/Standalone/ResonaPro.app"

VST3_DST="${HOME}/Library/Audio/Plug-Ins/VST3"
VST2_DST="${HOME}/Library/Audio/Plug-Ins/VST"
AU_DST="${HOME}/Library/Audio/Plug-Ins/Components"

# The standalone build goes where a person can actually launch it. Spotlight and
# Launchpad only look inside the Applications folders, so an app left sitting in
# the project directory is effectively invisible even though it works.
if [ -w "/Applications" ]; then
    STANDALONE_DST="/Applications"
else
    STANDALONE_DST="${HOME}/Applications"
fi

if [ ! -d "${VST3_SRC}" ] || [ ! -d "${AU_SRC}" ]; then
    echo "error: build artefacts not found."
    echo "       Run the build first:  ./.work/scripts/build.sh build"
    exit 1
fi

echo "Installing ResonaPro..."
mkdir -p "${VST3_DST}" "${AU_DST}" "${STANDALONE_DST}"
[ -d "${VST2_SRC}" ] && mkdir -p "${VST2_DST}"

entries=("${VST3_SRC}|${VST3_DST}|ResonaPro.vst3"
         "${AU_SRC}|${AU_DST}|ResonaPro.component")
# VST2 is only present when the build was given a VST2 SDK, so treat it as
# optional rather than expecting it.
if [ -d "${VST2_SRC}" ]; then
    entries+=("${VST2_SRC}|${VST2_DST}|ResonaPro.vst")
fi
if [ -d "${STANDALONE_SRC}" ]; then
    entries+=("${STANDALONE_SRC}|${STANDALONE_DST}|ResonaPro.app")
else
    echo "  note: no standalone build found, skipping the app"
fi

stamp="$(date +%Y%m%d-%H%M%S)"
for entry in "${entries[@]}"; do
    IFS='|' read -r src dst name <<< "${entry}"
    stage="${dst}/${name}.staging-${stamp}"
    if [ "${name}" = "ResonaPro.vst3" ] || [ "${name}" = "ResonaPro.vst" ]; then
        mkdir -p "${stage}"
        # Synced Documents folders can sprout ghost "moduleinfo 2.json" files.
        # They are not VST3 metadata and can make ditto fail with EDEADLK.
        rsync -a --exclude='moduleinfo [0-9]*.json' "${src}/" "${stage}/"
    else
        ditto "${src}" "${stage}"
    fi
    xattr -cr "${stage}"
    # Local ad-hoc signature. This is not Developer ID signing/notarization,
    # but avoids stale signatures after rebuilding or copying a bundle.
    codesign --force --sign - "${stage}"
    if [ -d "${dst}/${name}" ]; then
        mkdir -p "${ROLLBACK_DIR}"
        mv "${dst}/${name}" "${ROLLBACK_DIR}/${name}.previous-${stamp}"
    fi
    mv "${stage}" "${dst}/${name}"
done

# Clear out any leftovers from earlier installs that renamed in place, so the
# plug-in folders hold nothing but the current build.
for dir in "${VST3_DST}" "${AU_DST}" "${STANDALONE_DST}"; do
    for old in "${dir}"/ResonaPro.vst3.previous-* "${dir}"/ResonaPro.component.previous-* "${dir}"/ResonaPro.app.previous-*; do
        [ -e "${old}" ] || continue
        mkdir -p "${ROLLBACK_DIR}"
        mv "${old}" "${ROLLBACK_DIR}/"
    done
done

# The AU registry caches plug-in metadata; nudge it so hosts see the new build.
/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister \
    -f "${AU_DST}/ResonaPro.component" >/dev/null 2>&1 || true
killall AudioComponentRegistrar >/dev/null 2>&1 || true

echo "  VST3 -> ${VST3_DST}/ResonaPro.vst3"
[ -d "${VST2_SRC}" ] && echo "  VST2 -> ${VST2_DST}/ResonaPro.vst"
echo "  AU   -> ${AU_DST}/ResonaPro.component"
if [ -d "${STANDALONE_SRC}" ]; then
    echo "  App  -> ${STANDALONE_DST}/ResonaPro.app"
fi
echo
echo "Done. Rescan plug-ins in your DAW."
