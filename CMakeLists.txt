cmake_minimum_required(VERSION 3.22)

# Xcode 27 no longer builds for anything older than macOS 12.
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")

project(HolterdipolterFilter VERSION 1.0.0 LANGUAGES C CXX)

# Link the MSVC runtime statically so the plugin does not depend on an installed redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(HDP_BUILD_TESTS "Build the DSP tests" ON)
option(HDP_BUILD_TOOLS "Build the UI snapshot tool (development only)" OFF)
set(HDP_JUCE_PATH "" CACHE PATH "Path to a local JUCE checkout. Leave empty to fetch JUCE from GitHub.")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

#===============================================================================
# JUCE

# Order of preference: an explicit path, a JUCE folder next to this file
# (the offline build package ships one), and only then a download from GitHub.
if(HDP_JUCE_PATH)
    file(TO_CMAKE_PATH "${HDP_JUCE_PATH}" HDP_JUCE_DIR)
    add_subdirectory("${HDP_JUCE_DIR}" juce EXCLUDE_FROM_ALL)
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    message(STATUS "Using the bundled JUCE in ${CMAKE_CURRENT_SOURCE_DIR}/JUCE")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/JUCE" juce EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        9.0.5
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

#===============================================================================
# Plugin

set(HDP_SOURCES
    Source/EditHistory.cpp
    Source/Params.cpp
    Source/Presets.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/ui/Controls.cpp
    Source/ui/EqDisplay.cpp
    Source/ui/BandStrip.cpp
    Source/ui/PresetBar.cpp)

juce_add_plugin(HolterdipolterFilter
    COMPANY_NAME                "holterdipolter"
    BUNDLE_ID                   "com.holterdipolter.filter"
    PLUGIN_MANUFACTURER_CODE    Hdpl
    PLUGIN_CODE                 Hdfl
    FORMATS                     VST3
    PRODUCT_NAME                "holterdipolter Filter"
    DESCRIPTION                 "24-band parametric equalizer"
    VST3_CATEGORIES             Fx EQ
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    NEEDS_WEB_BROWSER           FALSE
    NEEDS_CURL                  FALSE)

target_sources(HolterdipolterFilter PRIVATE ${HDP_SOURCES})

target_compile_definitions(HolterdipolterFilter
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(HolterdipolterFilter
    PRIVATE
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

#===============================================================================
# DSP tests (plain C++, no JUCE)

if(HDP_BUILD_TESTS)
    enable_testing()
    add_executable(hdp_dsp_tests Tests/dsp_test.cpp)
    add_test(NAME dsp COMMAND hdp_dsp_tests)
endif()

#===============================================================================
# Renders the editor to a PNG without a host. Used to check the UI during development.

if(HDP_BUILD_TOOLS)
    juce_add_console_app(hdp_snapshot PRODUCT_NAME "hdp_snapshot")

    target_sources(hdp_snapshot PRIVATE Tools/snapshot.cpp ${HDP_SOURCES})

    target_compile_definitions(hdp_snapshot
        PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_MODAL_LOOPS_PERMITTED=1)

    target_link_libraries(hdp_snapshot
        PRIVATE
            juce::juce_audio_processors
            juce::juce_dsp
            juce::juce_gui_basics
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()
