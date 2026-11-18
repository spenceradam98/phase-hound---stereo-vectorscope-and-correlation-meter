cmake_minimum_required(VERSION 3.22)

# macOS 11 (Big Sur) on Intel is the minimum; build universal (x86_64 + arm64) on CI.
# No -march flags anywhere: the DSP is scalar C++ and runs on any x86-64 (no AVX2 needed).
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "macOS architectures")
endif()
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")  # static CRT: no VC++ redist needed
endif()
project(PhaseHound VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Core engine (no JUCE dependency, unit-testable) ----
add_library(phasehound_engine STATIC Source/engine/Engine.cpp)
target_include_directories(phasehound_engine PUBLIC Source/engine)
set_target_properties(phasehound_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)

option(PHASEHOUND_BUILD_TESTS "Build engine tests" ON)
if(PHASEHOUND_BUILD_TESTS)
    add_executable(phasehound_tests tests/EngineTests.cpp)
    target_link_libraries(phasehound_tests PRIVATE phasehound_engine)
    enable_testing()
    add_test(NAME engine COMMAND phasehound_tests)
endif()

option(PHASEHOUND_BUILD_PLUGIN "Build the JUCE plugin" ON)
if(NOT PHASEHOUND_BUILD_PLUGIN)
    return()
endif()

# ---- JUCE ----
set(JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (optional)")
if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(PH_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND PH_FORMATS AU)
endif()

juce_add_plugin(PhaseHound
    COMPANY_NAME "YourBrand"
    PLUGIN_MANUFACTURER_CODE Ybrd
    PLUGIN_CODE Phd1
    FORMATS ${PH_FORMATS}
    PRODUCT_NAME "Phase Hound"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Analyzer
    AU_MAIN_TYPE kAudioUnitType_Effect)

set(PH_PLUGIN_SOURCES
    Source/plugin/PluginProcessor.cpp
    Source/plugin/PluginEditor.cpp
    Source/plugin/ScopeView.cpp
    Source/plugin/Themes.cpp)

target_sources(PhaseHound PRIVATE ${PH_PLUGIN_SOURCES})

target_compile_definitions(PhaseHound PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(PhaseHound
    PRIVATE
        phasehound_engine
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---- optional: render the editor in every theme to PNGs (docs). Run under xvfb-run on Linux. ----
option(PHASEHOUND_BUILD_SNAPSHOTS "Build the screenshot renderer" OFF)
if(PHASEHOUND_BUILD_SNAPSHOTS)
    juce_add_console_app(phasehound_snapshots PRODUCT_NAME "Phase Hound Snapshots")
    target_sources(phasehound_snapshots PRIVATE tools/Snapshots.cpp ${PH_PLUGIN_SOURCES})
    target_compile_definitions(phasehound_snapshots PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        JucePlugin_Name="Phase Hound" JUCE_STANDALONE_APPLICATION=1)
    target_link_libraries(phasehound_snapshots PRIVATE phasehound_engine juce::juce_audio_utils
        PUBLIC juce::juce_recommended_config_flags)
endif()
