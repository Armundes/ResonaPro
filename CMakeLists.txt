cmake_minimum_required(VERSION 3.22)

project(ResonaPro VERSION 2.14.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build for both Apple Silicon and Intel, so the plug-in loads on either Mac.
# Note: the variable is defined-but-empty by default, so test for emptiness
# rather than with DEFINED.
# Override with -DCMAKE_OSX_ARCHITECTURES=arm64 for a faster local build.
if(NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Target architectures" FORCE)
endif()

# Point to downloaded JUCE library
set(JUCE_SOURCE_DIR_DEFAULT "/Users/armundescarey/.gemini/antigravity-ide/scratch/tools/JUCE")
if(NOT DEFINED JUCE_SOURCE_DIR)
    set(JUCE_SOURCE_DIR "${JUCE_SOURCE_DIR_DEFAULT}")
endif()
if(NOT EXISTS "${JUCE_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "JUCE not found at '${JUCE_SOURCE_DIR}'. Re-run CMake with "
        "-DJUCE_SOURCE_DIR=/absolute/path/to/JUCE")
endif()

# Set to OFF to build into the build tree without installing into the plugin folders
option(RESONAPRO_COPY_PLUGIN "Copy built plugins into ~/Library/Audio/Plug-Ins" OFF)

add_subdirectory("${JUCE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE_build")

# ---------------------------------------------------------------------------
# Optional VST2.
#
# Steinberg stopped licensing and distributing the VST2 SDK in October 2018 and
# JUCE removed its bundled copy, so the headers have to be supplied by whoever
# builds this. Point VST2_SDK_DIR at them and VST2 joins the build; leave it
# unset and the build is AU, VST3 and standalone, which is what every current
# host including FL Studio loads.
#
# juce_add_plugin treats VST without a prior juce_set_vst2_sdk_path as a fatal
# error, so the decision is made here rather than left to the caller.
# ---------------------------------------------------------------------------
set(VST2_SDK_DIR "" CACHE PATH "Path to the VST2 SDK, if you have one (optional)")

set(RESONAPRO_FORMATS AU VST3 Standalone)

if(VST2_SDK_DIR)
    if(EXISTS "${VST2_SDK_DIR}/public.sdk/source/vst2.x/aeffect.h")
        juce_set_vst2_sdk_path("${VST2_SDK_DIR}")
        set(RESONAPRO_FORMATS AU VST VST3 Standalone)
        message(STATUS "ResonaPro: VST2 SDK found, building VST2 as well")
    else()
        message(WARNING
            "ResonaPro: VST2_SDK_DIR was set but "
            "${VST2_SDK_DIR}/public.sdk/source/vst2.x/aeffect.h is not there. "
            "Building without VST2.")
    endif()
endif()

juce_add_plugin(ResonaPro
    COMPANY_NAME "ResonaAudio"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD ${RESONAPRO_COPY_PLUGIN}
    PLUGIN_MANUFACTURER_CODE ResA
    PLUGIN_CODE RsnP
    BUNDLE_ID "com.resonaaudio.resonapro"
    VST3_BUNDLE_ID "com.resonaaudio.resonapro.vst3"
    AU_BUNDLE_ID "com.resonaaudio.resonapro.au"
    AAX_BUNDLE_ID "com.resonaaudio.resonapro.aax"
    FORMATS ${RESONAPRO_FORMATS}
    PRODUCT_NAME "ResonaPro"
)

target_sources(ResonaPro
    PRIVATE
        Source/Version.h
        Source/PluginProcessor.h
        Source/PluginProcessor.cpp
        Source/PluginEditor.h
        Source/PluginEditor.cpp
        Source/DSP/SpectralEngine.h
        Source/DSP/ResonanceDetector.h
        Source/DSP/DynamicSuppressor.h
        Source/DSP/TransientDetector.h
        Source/DSP/ParametricEQWeighting.h
        Source/DSP/LearnAnalyzer.h
        Source/UI/CustomLookAndFeel.h
        Source/UI/SpectralVisualizer.h
)

target_compile_definitions(ResonaPro
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        RESONAPRO_VERSION_STRING="${PROJECT_VERSION}"
)

target_link_libraries(ResonaPro
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_gui_extra
        "-framework Accelerate"
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# ---------------------------------------------------------------------------
# Headless validation harness (off by default).
#
#   cmake -S . -B .work/build-tests -DRESONAPRO_BUILD_TESTS=ON -DRESONAPRO_COPY_PLUGIN=OFF
#   cmake --build .work/build-tests --target ResonaProTests
#   ./.work/build-tests/ResonaProTests_artefacts/ResonaProTests
# ---------------------------------------------------------------------------
option(RESONAPRO_BUILD_TESTS "Build the headless processor validation harness" OFF)

if(RESONAPRO_BUILD_TESTS)
    juce_add_console_app(ResonaProTests PRODUCT_NAME "ResonaProTests")

    target_sources(ResonaProTests
        PRIVATE
            Tests/ProcessorTests.cpp
            Source/PluginProcessor.h
            Source/PluginProcessor.cpp
            Source/PluginEditor.h
            Source/PluginEditor.cpp
    )

    target_include_directories(ResonaProTests PRIVATE Source)

    target_compile_definitions(ResonaProTests
        PRIVATE
            JUCE_STANDALONE_APPLICATION=1
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_MODAL_LOOPS_PERMITTED=1
            RESONAPRO_VERSION_STRING="${PROJECT_VERSION}"
    )

    target_link_libraries(ResonaProTests
        PRIVATE
            juce::juce_audio_utils
            juce::juce_audio_processors
            juce::juce_dsp
            juce::juce_gui_extra
            "-framework Accelerate"
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
    )
endif()

# ---------------------------------------------------------------------------
# Offline analysis harness (roadmap Phase 0.2). Runs an audio file through the
# plugin and reports measurable change, so a build can be compared against a
# saved reference instead of against memory.
#
#   cmake -S . -B .work/build-tools -DRESONAPRO_BUILD_ANALYZE=ON
#   cmake --build .work/build-tools --target ResonaProAnalyze
#   ./.work/build-tools/ResonaProAnalyze_artefacts/ResonaProAnalyze take.wav
# ---------------------------------------------------------------------------
option(RESONAPRO_BUILD_ANALYZE "Build the offline analysis harness" OFF)

if(RESONAPRO_BUILD_ANALYZE)
    juce_add_console_app(ResonaProAnalyze PRODUCT_NAME "ResonaProAnalyze")

    target_sources(ResonaProAnalyze
        PRIVATE
            Tools/Analyze.cpp
            Source/PluginProcessor.h
            Source/PluginProcessor.cpp
            Source/PluginEditor.h
            Source/PluginEditor.cpp
    )

    target_include_directories(ResonaProAnalyze PRIVATE Source)

    target_compile_definitions(ResonaProAnalyze
        PRIVATE
            JUCE_STANDALONE_APPLICATION=1
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_MODAL_LOOPS_PERMITTED=1
            RESONAPRO_VERSION_STRING="${PROJECT_VERSION}"
    )

    target_link_libraries(ResonaProAnalyze
        PRIVATE
            juce::juce_audio_utils
            juce::juce_audio_formats
            juce::juce_audio_processors
            juce::juce_dsp
            juce::juce_gui_extra
            "-framework Accelerate"
        PUBLIC
            juce::juce_recommended_config_flags
    )
endif()

# ---------------------------------------------------------------------------
# DSP regression suite. This ran for a long time as a standalone clang++
# invocation in .work, which quietly stopped compiling against the headers and
# left a stale binary reporting ALL PASS. Building it here means the same
# compiler, the same JUCE and no chance of a stale result.
#
#   cmake -S . -B .work/build-tests -DRESONAPRO_BUILD_TESTS=ON -DRESONAPRO_BUILD_DSPTEST=ON
#   cmake --build .work/build-tests --target ResonaProDspTests
# ---------------------------------------------------------------------------
option(RESONAPRO_BUILD_DSPTEST "Build the DSP regression suite" OFF)

if(RESONAPRO_BUILD_DSPTEST)
    juce_add_console_app(ResonaProDspTests PRODUCT_NAME "ResonaProDspTests")

    target_sources(ResonaProDspTests PRIVATE Tests/DspTests.cpp)
    target_include_directories(ResonaProDspTests PRIVATE Source Source/DSP)

    target_compile_definitions(ResonaProDspTests
        PRIVATE
            JUCE_STANDALONE_APPLICATION=1
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
    )

    target_link_libraries(ResonaProDspTests
        PRIVATE
            juce::juce_audio_processors
            juce::juce_dsp
            "-framework Accelerate"
        PUBLIC
            juce::juce_recommended_config_flags
    )
endif()

# ---------------------------------------------------------------------------
# Diagnostic probe: load-time memory and a sweep through every engine
# combination. Not part of the shipped test suite.
#
#   cmake -S . -B .work/build-probe -DRESONAPRO_BUILD_MEMPROBE=ON
#   cmake --build .work/build-probe --target ResonaProMemProbe
# ---------------------------------------------------------------------------
option(RESONAPRO_BUILD_MEMPROBE "Build the load and memory diagnostic probe" OFF)

if(RESONAPRO_BUILD_MEMPROBE)
    juce_add_console_app(ResonaProMemProbe PRODUCT_NAME "ResonaProMemProbe")

    target_sources(ResonaProMemProbe
        PRIVATE
            .work/scripts/MemProbe.cpp
            Source/PluginProcessor.h
            Source/PluginProcessor.cpp
            Source/PluginEditor.h
            Source/PluginEditor.cpp
    )

    target_include_directories(ResonaProMemProbe PRIVATE Source)

    target_compile_definitions(ResonaProMemProbe
        PRIVATE
            JUCE_STANDALONE_APPLICATION=1
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_MODAL_LOOPS_PERMITTED=1
            RESONAPRO_VERSION_STRING="${PROJECT_VERSION}"
    )

    target_link_libraries(ResonaProMemProbe
        PRIVATE
            juce::juce_audio_utils
            juce::juce_audio_processors
            juce::juce_dsp
            juce::juce_gui_extra
            "-framework Accelerate"
        PUBLIC
            juce::juce_recommended_config_flags
    )
endif()

# ---------------------------------------------------------------------------
#   Real-vocal check: measures the de-esser on an actual take.
# ---------------------------------------------------------------------------
option(RESONAPRO_BUILD_VOCALCHECK "Build the real-vocal de-esser check" OFF)
if(RESONAPRO_BUILD_VOCALCHECK)
    juce_add_console_app(ResonaProVocalCheck PRODUCT_NAME "ResonaProVocalCheck")
    target_sources(ResonaProVocalCheck PRIVATE .work/scripts/vocal_check.cpp)
    target_include_directories(ResonaProVocalCheck PRIVATE Source Source/DSP)
    target_compile_definitions(ResonaProVocalCheck PRIVATE
        JUCE_STANDALONE_APPLICATION=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(ResonaProVocalCheck PRIVATE
        juce::juce_audio_processors juce::juce_dsp "-framework Accelerate")
endif()

if(RESONAPRO_BUILD_VOCALCHECK)
    juce_add_console_app(ResonaProDeessDiag PRODUCT_NAME "ResonaProDeessDiag")
    target_sources(ResonaProDeessDiag PRIVATE .work/scripts/deess_diag.cpp)
    target_include_directories(ResonaProDeessDiag PRIVATE Source Source/DSP)
    target_compile_definitions(ResonaProDeessDiag PRIVATE
        JUCE_STANDALONE_APPLICATION=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(ResonaProDeessDiag PRIVATE
        juce::juce_audio_processors juce::juce_dsp "-framework Accelerate")
endif()

# ---------------------------------------------------------------------------
# Learn scoring harness.
#
# Learn was reported to produce settings that sound thin or boxy, and the Init
# patch was reported to beat them. Neither claim could be checked, so neither
# could be acted on. This renders each take three ways -- dry, Init, and the
# learned profile applied -- and reports sibilance reduction against body
# damage. It is the control that any change to the learner has to beat.
#
#   cmake -S . -B .work/build-tools -DRESONAPRO_BUILD_ANALYZE=ON -DRESONAPRO_BUILD_LEARNSCORE=ON
#   cmake --build .work/build-tools --target ResonaProLearnScore
#   ./.work/build-tools/ResonaProLearnScore_artefacts/ResonaProLearnScore takes/*.wav
# ---------------------------------------------------------------------------
option(RESONAPRO_BUILD_LEARNSCORE "Build the learn scoring harness" OFF)

if(RESONAPRO_BUILD_LEARNSCORE)
    juce_add_console_app(ResonaProLearnScore PRODUCT_NAME "ResonaProLearnScore")

    target_sources(ResonaProLearnScore
        PRIVATE
            Tools/LearnScore.cpp
            Source/PluginProcessor.h
            Source/PluginProcessor.cpp
            Source/PluginEditor.h
            Source/PluginEditor.cpp
    )

    target_include_directories(ResonaProLearnScore PRIVATE Source)

    target_compile_definitions(ResonaProLearnScore
        PRIVATE
            JUCE_STANDALONE_APPLICATION=1
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_MODAL_LOOPS_PERMITTED=1
            RESONAPRO_VERSION_STRING="${PROJECT_VERSION}"
    )

    target_link_libraries(ResonaProLearnScore
        PRIVATE
            juce::juce_audio_utils
            juce::juce_audio_formats
            juce::juce_audio_processors
            juce::juce_dsp
            juce::juce_gui_extra
            "-framework Accelerate"
        PUBLIC
            juce::juce_recommended_config_flags
    )
endif()
