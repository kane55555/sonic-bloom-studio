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
set(JUCE_GLOBAL_ARA_SDK_PATH "" CACHE PATH "" FORCE)

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

# JUCE 8 makes juce_audio_processors depend on an implementation-only split
# module and also exposes optional ARA/LV2 support headers through that target.
# This plugin is VST3-only, so keep the public JUCE module links below but stop
# CMake from adding those optional helper compile units/include paths.
if(TARGET juce_audio_processors_headless)
    set_property(TARGET juce_audio_processors_headless PROPERTY INTERFACE_SOURCES "")

    get_target_property(_dida_headless_links juce_audio_processors_headless INTERFACE_LINK_LIBRARIES)
    if(_dida_headless_links)
        list(REMOVE_ITEM _dida_headless_links juce_lilv_headers juce::juce_lilv_headers juce_ara_headers juce::juce_ara_headers)
        set_property(TARGET juce_audio_processors_headless PROPERTY INTERFACE_LINK_LIBRARIES "${_dida_headless_links}")
    endif()
endif()

if(TARGET juce_audio_processors)
    set_property(TARGET juce_audio_processors PROPERTY INTERFACE_SOURCES "")
    set_property(TARGET juce_audio_processors PROPERTY INTERFACE_LINK_LIBRARIES "juce_gui_extra")
endif()

foreach(_dida_forbidden_headers IN ITEMS juce_lilv_headers juce_ara_headers)
    if(TARGET ${_dida_forbidden_headers})
        set_property(TARGET ${_dida_forbidden_headers} PROPERTY INTERFACE_INCLUDE_DIRECTORIES "")
        set_property(TARGET ${_dida_forbidden_headers} PROPERTY INTERFACE_LINK_LIBRARIES "")
    endif()
endforeach()

if(TARGET juce_vst3_headers)
    set(_dida_vst3_sdk_src "${JUCE_DIR}/modules/juce_audio_processors_headless/format_types/VST3_SDK")
    set(_dida_vst3_sdk_dst "${CMAKE_CURRENT_BINARY_DIR}/juce_vst3_sdk_headers")
    if(EXISTS "${_dida_vst3_sdk_src}")
        file(COPY "${_dida_vst3_sdk_src}/" DESTINATION "${_dida_vst3_sdk_dst}")
        set_property(TARGET juce_vst3_headers PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${_dida_vst3_sdk_dst}")
    endif()
endif()

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

set(_dida_audio_processor_runtime "${CMAKE_CURRENT_BINARY_DIR}/DidaJuceAudioProcessorRuntime.cpp")
file(WRITE "${_dida_audio_processor_runtime}" "#include <juce_audio_processors_headless/juce_audio_processors_headless.cpp>\n#include <juce_audio_processors/juce_audio_processors.cpp>\n")
target_sources(DIDITAGAIN_STUDIO PRIVATE "${_dida_audio_processor_runtime}")

target_include_directories(DIDITAGAIN_STUDIO PRIVATE
    Source
    "${JUCE_DIR}/modules/juce_audio_processors/format_types/VST3_SDK"
    "${JUCE_DIR}/modules/juce_audio_processors_headless/format_types/VST3_SDK"
)

# Apply the VST3 SDK include path to the generated VST3 wrapper target as well.
# The juce_add_plugin VST3 target is created during configure, so defer with a
# generator-friendly approach: add it after target creation if it exists, and
# also propagate via the plugin client module target.
foreach(_dida_vst3_inc_target IN ITEMS DIDITAGAIN_STUDIO_VST3 juce_audio_plugin_client_VST3 juce_vst3_headers)
    if(TARGET ${_dida_vst3_inc_target})
        target_include_directories(${_dida_vst3_inc_target} INTERFACE
            "${JUCE_DIR}/modules/juce_audio_processors/format_types/VST3_SDK"
            "${JUCE_DIR}/modules/juce_audio_processors_headless/format_types/VST3_SDK"
        )
        target_include_directories(${_dida_vst3_inc_target} PRIVATE
            "${JUCE_DIR}/modules/juce_audio_processors/format_types/VST3_SDK"
            "${JUCE_DIR}/modules/juce_audio_processors_headless/format_types/VST3_SDK"
        )
    endif()
endforeach()

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
)

function(_dida_remove_forbidden_defs target_name)
    if(TARGET ${target_name})
        foreach(_dida_defs_property IN ITEMS COMPILE_DEFINITIONS INTERFACE_COMPILE_DEFINITIONS)
            get_target_property(_dida_defs ${target_name} ${_dida_defs_property})
            if(_dida_defs)
                list(FILTER _dida_defs EXCLUDE REGEX "ARA|LV2")
                set_property(TARGET ${target_name} PROPERTY ${_dida_defs_property} "${_dida_defs}")
            endif()
        endforeach()
    endif()
endfunction()

foreach(_dida_defs_target IN ITEMS DIDITAGAIN_STUDIO DIDITAGAIN_STUDIO_VST3 juce_audio_plugin_client_VST3)
    _dida_remove_forbidden_defs(${_dida_defs_target})
endforeach()

# ---------------------------------------------------------------------------
# ClangCL workaround: the optimizer has been crashing on Release builds.
# Force optimizations OFF and strip LTO/PGO/debug-info flags that have
# triggered the crashes. Applies to every target in this directory tree.
# ---------------------------------------------------------------------------
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND (CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC" OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"))
    set(_DIDA_BAD_FLAGS "-O2" "-O3" "-Ox" "/O2" "/O3" "/Ox" "/Ob1" "/Ob2" "/Ob3" "/GL" "/GL-" "/LTCG" "/Zi" "/Z7" "/Oi" "/Ot")
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

    if(TARGET juce_recommended_config_flags)
        set_property(TARGET juce_recommended_config_flags PROPERTY INTERFACE_COMPILE_OPTIONS "")
        set_property(TARGET juce_recommended_config_flags PROPERTY INTERFACE_LINK_LIBRARIES "")
    endif()

    foreach(_dida_noopt_target IN ITEMS DIDITAGAIN_STUDIO DIDITAGAIN_STUDIO_VST3)
        if(TARGET ${_dida_noopt_target})
            target_compile_options(${_dida_noopt_target} PRIVATE /Od /Ob0)
        endif()
    endforeach()
endif()
