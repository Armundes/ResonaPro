cmake_minimum_required(VERSION 3.22)

project(ResonaPro VERSION 2.2.0 LANGUAGES C CXX)

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
    FORMATS AU VST3 Standalone
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
