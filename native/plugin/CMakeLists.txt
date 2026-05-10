cmake_minimum_required(VERSION 3.22)

project(DIDITAGAIN_STUDIO VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Locate JUCE
if(NOT DEFINED JUCE_DIR AND DEFINED ENV{JUCE_DIR})
    set(JUCE_DIR "$ENV{JUCE_DIR}")
endif()
if(NOT DEFINED JUCE_DIR)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../JUCE/CMakeLists.txt")
        set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../JUCE")
    elseif(EXISTS "C:/JUCE/CMakeLists.txt")
        set(JUCE_DIR "C:/JUCE")
    endif()
endif()
if(NOT DEFINED JUCE_DIR OR NOT EXISTS "${JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "JUCE_DIR is not set or invalid. Pass -DJUCE_DIR=<path-to-JUCE>.")
endif()

# Sanity check: vanilla JUCE must have the real audio_processors module.
if(NOT EXISTS "${JUCE_DIR}/modules/juce_audio_processors/juce_audio_processors.cpp")
    message(FATAL_ERROR
        "DIDITAGAIN: ${JUCE_DIR}/modules/juce_audio_processors/juce_audio_processors.cpp is missing. "
        "Your JUCE checkout looks like a stripped fork. Replace C:/JUCE with an official release "
        "(e.g. git clone --depth 1 --branch 8.0.4 https://github.com/juce-framework/JUCE.git C:/JUCE).")
endif()

# MSVC: JUCE's SheenBidi C sources contain UTF-8 literals.
if(MSVC)
    add_compile_options(/utf-8)
endif()

add_subdirectory("${JUCE_DIR}" JUCE EXCLUDE_FROM_ALL)

juce_add_plugin(DIDITAGAIN_STUDIO
    PRODUCT_NAME            "DIDITAGAIN STUDIO"
    COMPANY_NAME            "DIDITAGAIN"
    BUNDLE_ID               com.diditagain.studio
    PLUGIN_MANUFACTURER_CODE Dida
    PLUGIN_CODE             Dida
    FORMATS                 VST3
    IS_SYNTH                TRUE
    NEEDS_MIDI_INPUT        TRUE
    NEEDS_MIDI_OUTPUT       FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

juce_generate_juce_header(DIDITAGAIN_STUDIO)

target_sources(DIDITAGAIN_STUDIO PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
)

target_include_directories(DIDITAGAIN_STUDIO PRIVATE Source)

target_compile_definitions(DIDITAGAIN_STUDIO PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(DIDITAGAIN_STUDIO
    PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_plugin_client
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_data_structures
        juce::juce_dsp
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
