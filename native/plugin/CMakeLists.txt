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

# JUCE 8 splits part of juce_audio_processors into a headless implementation
# target. Keep those implementation sources, but remove this project's custom
# generated runtime files that trigger ClangCL codegen crashes.
function(_dida_filter_forbidden_sources target_name)
    if(TARGET ${target_name})
        foreach(_dida_sources_property IN ITEMS SOURCES INTERFACE_SOURCES)
            get_target_property(_dida_sources ${target_name} ${_dida_sources_property})
            if(_dida_sources)
                list(FILTER _dida_sources EXCLUDE REGEX "(DidaJuceAudioProcessorRuntime|FxChain|ReverbBlock)\\.cpp$")
                set_property(TARGET ${target_name} PROPERTY ${_dida_sources_property} "${_dida_sources}")
            endif()
        endforeach()

        get_target_property(_dida_links ${target_name} INTERFACE_LINK_LIBRARIES)
        if(_dida_links)
            list(REMOVE_ITEM _dida_links
                juce_lilv_headers juce::juce_lilv_headers
                juce_ara_headers juce::juce_ara_headers)
            set_property(TARGET ${target_name} PROPERTY INTERFACE_LINK_LIBRARIES "${_dida_links}")
        endif()
    endif()
endfunction()

_dida_filter_forbidden_sources(juce_audio_processors_headless)
_dida_filter_forbidden_sources(juce_audio_processors)

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

# Force-compile the juce_audio_processors implementation TU. JUCE's module
# .cpp uses an internal guard (JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED) that can
# silently turn it into an empty translation unit when added directly. We
# generate a wrapper that defines the guard before #include'ing the real .cpp.
set(_dida_ap_real "${JUCE_DIR}/modules/juce_audio_processors/juce_audio_processors.cpp")
if(NOT EXISTS "${_dida_ap_real}")
    set(_dida_ap_real "${JUCE_DIR}/modules/juce_audio_processors_headless/juce_audio_processors_headless.cpp")
endif()
if(NOT EXISTS "${_dida_ap_real}")
    message(FATAL_ERROR "DIDITAGAIN: cannot locate juce_audio_processors .cpp under ${JUCE_DIR}/modules")
endif()
message(STATUS "DIDITAGAIN: wrapping juce_audio_processors TU at ${_dida_ap_real}")

set(_dida_ap_wrapper "${CMAKE_CURRENT_BINARY_DIR}/dida_juce_audio_processors_force.cpp")
file(WRITE "${_dida_ap_wrapper}"
"// Auto-generated by DIDITAGAIN CMakeLists.txt. Forces compilation of the\n"
"// juce_audio_processors implementation translation unit even when the JUCE\n"
"// module system would otherwise skip it.\n"
"#define JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED 1\n"
"#include \"${_dida_ap_real}\"\n")
target_sources(DIDITAGAIN_STUDIO PRIVATE "${_dida_ap_wrapper}")
set_source_files_properties("${_dida_ap_wrapper}" PROPERTIES
    SKIP_PRECOMPILE_HEADERS ON
    SKIP_UNITY_BUILD_INCLUSION ON)

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
        get_target_property(_dida_vst3_target_type ${_dida_vst3_inc_target} TYPE)
        if(_dida_vst3_target_type STREQUAL "INTERFACE_LIBRARY")
            target_include_directories(${_dida_vst3_inc_target} INTERFACE
                "${JUCE_DIR}/modules/juce_audio_processors/format_types/VST3_SDK"
                "${JUCE_DIR}/modules/juce_audio_processors_headless/format_types/VST3_SDK"
            )
        else()
            target_include_directories(${_dida_vst3_inc_target} PRIVATE
                "${JUCE_DIR}/modules/juce_audio_processors/format_types/VST3_SDK"
                "${JUCE_DIR}/modules/juce_audio_processors_headless/format_types/VST3_SDK"
            )
        endif()
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
# VS 2026 Insiders workaround: both ClangCL 20.x and MSVC 19.51 have been
# crashing in code generation on this JUCE target. Build Release without the
# optimizer/LTCG path that triggers the compiler access violation.
# ---------------------------------------------------------------------------
if(MSVC OR (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND (CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC" OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")))
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE OFF CACHE BOOL "" FORCE)

    set(_DIDA_BAD_FLAGS "-O2" "-O3" "-Ox" "/O1" "/O2" "/O3" "/Ox" "/Ob1" "/Ob2" "/Ob3" "/GL" "/GL-" "/LTCG" "/Zi" "/Z7" "/Oi" "/Ot")
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
