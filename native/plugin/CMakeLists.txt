cmake_minimum_required(VERSION 3.22)

project(DIDITAGAIN_STUDIO VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Locate JUCE
# ---------------------------------------------------------------------------
if(NOT DEFINED JUCE_DIR AND DEFINED ENV{JUCE_DIR})
    set(JUCE_DIR "$ENV{JUCE_DIR}")
endif()
if(NOT DEFINED JUCE_DIR)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../JUCE/CMakeLists.txt")
        set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../JUCE")
    elseif(EXISTS "C:/JUCE/CMakeLists.txt")
        set(JUCE_DIR "C:/JUCE")
    else()
        message(FATAL_ERROR "JUCE_DIR is not set. Pass -DJUCE_DIR=C:/JUCE")
    endif()
endif()

if(NOT EXISTS "${JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "JUCE_DIR is invalid: ${JUCE_DIR}")
endif()

# Sanity check: official JUCE must have the audio_processors module .cpp.
if(NOT EXISTS "${JUCE_DIR}/modules/juce_audio_processors/juce_audio_processors.cpp")
    message(FATAL_ERROR
        "DIDITAGAIN: ${JUCE_DIR}/modules/juce_audio_processors/juce_audio_processors.cpp is missing. "
        "Your JUCE checkout looks like a stripped fork. Replace it with an official JUCE release.")
endif()

# Sanity check: bundled VST3 SDK headers must exist.
if(NOT EXISTS "${JUCE_DIR}/modules/juce_audio_processors/format_types/VST3_SDK/pluginterfaces/base/fplatform.h")
    message(FATAL_ERROR
        "Your JUCE checkout is missing VST3 SDK headers. "
        "Missing: ${JUCE_DIR}/modules/juce_audio_processors/format_types/VST3_SDK/pluginterfaces/base/fplatform.h. "
        "Reinstall official JUCE 7.0.x or JUCE 8.x.")
endif()

# ---------------------------------------------------------------------------
# Global toolchain settings
# ---------------------------------------------------------------------------
if(MSVC)
    add_compile_options(/utf-8)
endif()

# Hard-disable LTO/IPO globally.
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE OFF)
set(CMAKE_UNITY_BUILD OFF)

add_subdirectory("${JUCE_DIR}" JUCE EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# Plugin target
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
    IS_MIDI_EFFECT          FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD  FALSE
    VST3_AUTO_MANIFEST       FALSE
)

# Defensive: if VST3_AUTO_MANIFEST is honored, the juce_vst3_helper target
# is never created. If an older JUCE still creates it, give it /O1 to avoid
# an MSVC 14.44 internal compiler error in <memory> when built with /Od.
if(TARGET juce_vst3_helper AND MSVC)
    target_compile_options(juce_vst3_helper PRIVATE /O1)
endif()

set(DIDA_JUCE_AUDIO_PROCESSORS_CPP
    "${JUCE_DIR}/modules/juce_audio_processors/juce_audio_processors.cpp"
)

if(NOT EXISTS "${DIDA_JUCE_AUDIO_PROCESSORS_CPP}")
    message(FATAL_ERROR "Missing JUCE audio processors implementation: ${DIDA_JUCE_AUDIO_PROCESSORS_CPP}")
endif()

target_sources(DIDITAGAIN_STUDIO PRIVATE
    "${DIDA_JUCE_AUDIO_PROCESSORS_CPP}"
)

set_property(TARGET DIDITAGAIN_STUDIO PROPERTY UNITY_BUILD OFF)

# Link JUCE modules directly to the main plugin target created by
# juce_add_plugin. This target produces DIDITAGAIN_STUDIO_SharedCode.lib, so
# JUCE module INTERFACE_SOURCES such as juce_audio_processors.cpp are compiled
# into the shared-code library before DIDITAGAIN_STUDIO_VST3 links.
set(DIDA_JUCE_MODULES
    juce::juce_audio_plugin_client
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_audio_formats
    juce::juce_audio_devices
    juce::juce_audio_basics
    juce::juce_dsp
    juce::juce_gui_extra
    juce::juce_gui_basics
    juce::juce_graphics
    juce::juce_events
    juce::juce_core
    juce::juce_data_structures
)

target_link_libraries(DIDITAGAIN_STUDIO
    PRIVATE
        ${DIDA_JUCE_MODULES}
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

if(TARGET DIDITAGAIN_STUDIO_SharedCode)
    target_link_libraries(DIDITAGAIN_STUDIO_SharedCode PRIVATE ${DIDA_JUCE_MODULES})
endif()

juce_generate_juce_header(DIDITAGAIN_STUDIO)

# ---------------------------------------------------------------------------
# Source collection
# ---------------------------------------------------------------------------
file(GLOB DIDA_CORE_SOURCES             CONFIGURE_DEPENDS Source/*.cpp)
file(GLOB_RECURSE DIDA_DSP_SOURCES        CONFIGURE_DEPENDS Source/DSP/*.cpp)
file(GLOB_RECURSE DIDA_PRESET_SOURCES     CONFIGURE_DEPENDS Source/Presets/*.cpp)
file(GLOB_RECURSE DIDA_UI_SOURCES         CONFIGURE_DEPENDS Source/UI/*.cpp)
file(GLOB_RECURSE DIDA_LICENSING_SOURCES  CONFIGURE_DEPENDS Source/Licensing/*.cpp)

set(DIDA_ALL_SOURCES
    ${DIDA_CORE_SOURCES}
    ${DIDA_DSP_SOURCES}
    ${DIDA_PRESET_SOURCES}
    ${DIDA_UI_SOURCES}
    ${DIDA_LICENSING_SOURCES}
)

# Hard exclusions: never compile generated wrappers or build/Tests output.
list(FILTER DIDA_ALL_SOURCES EXCLUDE REGEX ".*DidaJuceAudioProcessorRuntime\\.cpp$")
list(FILTER DIDA_ALL_SOURCES EXCLUDE REGEX ".*dida_juce_audio_processors_force\\.cpp$")
list(FILTER DIDA_ALL_SOURCES EXCLUDE REGEX ".*/build/.*")
list(FILTER DIDA_ALL_SOURCES EXCLUDE REGEX ".*/Tests/.*")

target_sources(DIDITAGAIN_STUDIO PRIVATE ${DIDA_ALL_SOURCES})

target_include_directories(DIDITAGAIN_STUDIO PRIVATE
    Source
    "${JUCE_DIR}/modules/juce_audio_processors/format_types/VST3_SDK"
)

target_compile_definitions(DIDITAGAIN_STUDIO PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_PLUGINHOST_ARA=0
    JUCE_PLUGINHOST_LV2=0
)

# ---------------------------------------------------------------------------
# Compiler options (no LTO, no /O2, no /Zi/Z7, debug-friendly)
# ---------------------------------------------------------------------------
if(MSVC OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(DIDITAGAIN_STUDIO PRIVATE
        /Od
        /Ob0
        /bigobj
        /FS
    )
    set_target_properties(DIDITAGAIN_STUDIO PROPERTIES
        INTERPROCEDURAL_OPTIMIZATION_RELEASE FALSE
        MSVC_DEBUG_INFORMATION_FORMAT ""
    )
endif()

# ---------------------------------------------------------------------------
# Diagnostics
# ---------------------------------------------------------------------------
message(STATUS "DIDITAGAIN source files:")
foreach(src ${DIDA_ALL_SOURCES})
    message(STATUS "  ${src}")
endforeach()
