#pragma once

// The version the interface announces.
//
// It is set by CMake from the project version, so the label in the editor
// cannot drift away from the version stamped into the plug-in bundle.
//
// This used to be a literal inside the drawing code, and it fell a release
// behind: a freshly installed build still announced itself as the previous
// version, which makes a correct install look like a stale one. The one thing a
// version label has to do is tell the truth about which binary is running.
//
// The fallback matters too. The plug-in target gets JUCE's own version macro,
// but the headless test target compiles the same editor source without it, so
// the definition has to come from the build rather than from JUCE.
#ifndef RESONAPRO_VERSION_STRING
    #define RESONAPRO_VERSION_STRING "dev"
#endif