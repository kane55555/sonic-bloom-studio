cmake_minimum_required(VERSION 3.22)

project(DIDITAGAIN_STUDIO VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Hard-disable every optional JUCE plugin-client variant we do not want.
# These MUST be set BEFORE add_subdirectory(JUCE) so JUCE never generates
# the headless / ARA / LV2 helper targets.
# ---------------------------------------------------------------------------
set(JUCE_BUILD_EXTRAS    OFF CACHE BOOL "" FORCE)
set(JUCE_BUILD_EXAMPLES  OFF CACHE BOOL "" FORCE)

# Disable ARA support globally
set(JUCE_ENABLE_ARA      OFF CACHE BOOL "" FORCE)
set(JUCE_PLUGINHOST_ARA  OFF CACHE BOOL "" FORCE)

# Disable LV2 helper libraries
set(JUCE_PLUGINHOST_LV2  OFF CACHE BOOL "" FORCE)

# Disable the "headless" audio_processors variant (some JUCE forks build this
# automatically). Forcing the option OFF prevents the headless module +
# its ARA / LV2 helper targets from being generated.
set(JUCE_BUILD_HEADLESS_PLUGIN_CLIENT OFF CACHE BOOL "" FORCE)
set(JUCE_BUILD_AUDIO_PROCESSORS_HEADLESS OFF CACHE BOOL "" FORCE)

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

add_subdirectory("${JUCE_DIR}" JUCE EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# Plugin target: VST3 only. No AU/AAX/LV2/Unity/AUv3/ARA/headless.
# ---------------------------------------------------------------------------
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
    VST3_AUTO_MANIFEST      FALSE
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
    JUCE_PLUGINHOST_ARA=0
    JUCE_PLUGINHOST_LV2=0
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
        juce::juce_recommended_warning_flags
)

# ---------------------------------------------------------------------------
# ClangCL workaround: the optimizer has been crashing on Release builds.
# Force optimizations OFF and strip LTO/PGO/debug-info flags that have
# triggered the crashes. Applies to every target in this directory tree.
# ---------------------------------------------------------------------------
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
    set(_DIDA_BAD_FLAGS "/O2" "/Ox" "/Ob1" "/Ob2" "/Ob3" "/GL" "/GL-" "/LTCG" "/Zi" "/Z7" "/Oi" "/Ot")
    set(_DIDA_CFG_VARS
        CMAKE_C_FLAGS CMAKE_C_FLAGS_RELEASE CMAKE_C_FLAGS_RELWITHDEBINFO CMAKE_C_FLAGS_MINSIZEREL
        CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_RELEASE CMAKE_CXX_FLAGS_RELWITHDEBINFO CMAKE_CXX_FLAGS_MINSIZEREL
        CMAKE_EXE_LINKER_FLAGS_RELEASE CMAKE_SHARED_LINKER_FLAGS_RELEASE CMAKE_MODULE_LINKER_FLAGS_RELEASE)
    foreach(_v ${_DIDA_CFG_VARS})
        foreach(_bad ${_DIDA_BAD_FLAGS})
            string(REPLACE "${_bad}" "" ${_v} "${${_v}}")
        endforeach()
        string(REGEX REPLACE " +" " " ${_v} "${${_v}}")
        set(${_v} "${${_v}}" CACHE STRING "" FORCE)
    endforeach()

    target_compile_options(DIDITAGAIN_STUDIO PRIVATE /Od /Ob0)
endif()
