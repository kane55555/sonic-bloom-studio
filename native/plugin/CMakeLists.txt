cmake_minimum_required(VERSION 3.22)

if(POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
endif()

# VS 2026/MSVC 14.51 has been crashing in CloseTypeServerPDB/mspdbsrv when
# JUCE modules use Program Database debug info. Set this before project() so
# generated .vcxproj files use OldStyle /Z7 embedded debug info instead of /Zi.
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "Embedded" CACHE STRING "" FORCE)

project(DIDITAGAIN_STUDIO VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Visual Studio 18/2026 Insider's MSVC 14.51 compiler currently hits multiple
# internal compiler errors inside JUCE itself (VST3 SDK json.h, audio metadata
# templates, and juce_RenderingHelpers.h). Do not keep retrying cl.exe; use the
# ClangCL toolset that ships with the VS C++ workload instead.
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" AND MSVC_VERSION GREATER_EQUAL 1950)
    option(DIDITAGAIN_ALLOW_MSVC_1451 "Force use of the known-broken MSVC 14.51 compiler" OFF)
    if(NOT DIDITAGAIN_ALLOW_MSVC_1451)
        message(FATAL_ERROR
            "MSVC ${MSVC_VERSION} / Visual Studio 2026 Insiders is currently crashing while compiling JUCE. "
            "Install the 'C++ Clang tools for Windows' component in Visual Studio Installer, delete the build folder, "
            "then reconfigure with: cmake -S . -B build -G \"Visual Studio 18 2026\" -A x64 -T ClangCL -DJUCE_DIR=C:/JUCE")
    endif()
endif()

# Keep MSVC preview/stable toolchains away from shared PDB/type-server paths.
# The VS 2026 Insider linker has been crashing in PDB/type-server and LTCG
# passes while building JUCE wrappers, so prefer embedded debug info.
if(MSVC)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "Embedded" CACHE STRING "" FORCE)

    # JUCE 8's Sheenbidi sources contain non-ASCII characters; MSVC needs
    # /utf-8 to parse them correctly (otherwise C2065 in ScriptLookup.c).
    add_compile_options(/utf-8)

    # Strip /GL (whole-program optimization) and /LTCG from ALL configs and ALL
    # targets globally, BEFORE add_subdirectory(JUCE). The MSVC ICE in
    # juce_FileChooser.cpp happens during the LTCG "Generating code" pass, so
    # JUCE's own object libraries must also be compiled without /GL.
    foreach(flag_var
            CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_RELEASE CMAKE_CXX_FLAGS_RELWITHDEBINFO
            CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_DEBUG
            CMAKE_C_FLAGS CMAKE_C_FLAGS_RELEASE CMAKE_C_FLAGS_RELWITHDEBINFO
            CMAKE_C_FLAGS_MINSIZEREL CMAKE_C_FLAGS_DEBUG)
        string(REGEX REPLACE "/Z[iI]" "/Z7" ${flag_var} "${${flag_var}}")
        string(REGEX REPLACE "/GL(-)?" "" ${flag_var} "${${flag_var}}")
        string(REGEX REPLACE "/LTCG(:[A-Za-z]+)?" "" ${flag_var} "${${flag_var}}")
        set(${flag_var} "${${flag_var}} /Z7 /GL-" CACHE STRING "" FORCE)
    endforeach()
    foreach(flag_var
            CMAKE_EXE_LINKER_FLAGS CMAKE_EXE_LINKER_FLAGS_RELEASE
            CMAKE_SHARED_LINKER_FLAGS CMAKE_SHARED_LINKER_FLAGS_RELEASE
            CMAKE_MODULE_LINKER_FLAGS CMAKE_MODULE_LINKER_FLAGS_RELEASE
            CMAKE_STATIC_LINKER_FLAGS CMAKE_STATIC_LINKER_FLAGS_RELEASE)
        string(REGEX REPLACE "/LTCG(:[A-Za-z]+)?" "" ${flag_var} "${${flag_var}}")
        set(${flag_var} "${${flag_var}} /LTCG:OFF" CACHE STRING "" FORCE)
    endforeach()

    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE OFF)
endif()

# --- JUCE ---
# Set JUCE_DIR to your local JUCE installation path
# e.g. cmake -DJUCE_DIR=C:/JUCE ..
if(NOT DEFINED JUCE_DIR)
    if(EXISTS "C:/JUCE/CMakeLists.txt")
        set(JUCE_DIR "C:/JUCE" CACHE PATH "Path to JUCE framework")
    else()
        set(JUCE_DIR "${CMAKE_SOURCE_DIR}/../../third_party/JUCE" CACHE PATH "Path to JUCE framework")
    endif()
endif()

add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/juce EXCLUDE_FROM_ALL)

# --- VST3 SDK ---
# Typically bundled with JUCE, but if separate:
# set(VST3_SDK_DIR "C:/VST_SDK/VST3_SDK")

juce_add_plugin(DIDITAGAIN_STUDIO
    PRODUCT_NAME "DIDITAGAIN STUDIO"
    COMPANY_NAME "DIDITAGAIN"
    BUNDLE_ID "com.diditagain.studio"
    PLUGIN_MANUFACTURER_CODE Dida
    PLUGIN_CODE Dida
    FORMATS VST3 Standalone
    # VS 2026/MSVC 14.51 currently ICEs while compiling JUCE's VST3
    # moduleinfo helper around the VST3 SDK json.h wrapper. Disabling the
    # automatic manifest skips that helper; hosts can still scan the VST3.
    VST3_AUTO_MANIFEST FALSE
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    # Disabled: auto-copy to "C:\Program Files\Common Files\VST3" requires
    # an elevated (admin) MSBuild. Use the manual xcopy step from the build
    # instructions instead, or run cmake/MSBuild from an Administrator shell.
    COPY_PLUGIN_AFTER_BUILD FALSE
)

juce_generate_juce_header(DIDITAGAIN_STUDIO)

target_sources(DIDITAGAIN_STUDIO PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/DSP/SynthEngine.cpp
    Source/DSP/Voice.cpp
    Source/DSP/Oscillator.cpp
    Source/DSP/FilterBlock.cpp
    Source/DSP/Envelope.cpp
    Source/DSP/LFO.cpp
    Source/DSP/ModMatrix.cpp
    Source/DSP/FxChain.cpp
    Source/DSP/Saturation.cpp
    Source/DSP/ChorusBlock.cpp
    Source/DSP/DelayBlock.cpp
    Source/DSP/ReverbBlock.cpp
    Source/DSP/UtilityDSP.cpp
    Source/DSP/SampleLibrary.cpp
    Source/Presets/PresetManager.cpp
    Source/Presets/FactoryPresets.cpp
    Source/Presets/PresetMigration.cpp
    Source/Presets/PresetIndex.cpp
    Source/Presets/PresetTemplateFactory.cpp
    Source/Presets/HybridPresetGenerator.cpp
    Source/Presets/HybridPresetApplier.cpp
    Source/Presets/MacroMapper.cpp
    Source/Licensing/LicenseClient.cpp
    Source/Licensing/AccountState.cpp
    Source/UI/Theme.cpp
    Source/UI/BrowserComponent.cpp
    Source/UI/KnobLookAndFeel.cpp
    Source/UI/MainSynthPanel.cpp
    Source/UI/PresetBrowser.cpp
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
        juce::juce_audio_processors
        juce::juce_core
        juce::juce_data_structures
        juce::juce_dsp
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_warning_flags
)

if(NOT MSVC)
    target_link_libraries(DIDITAGAIN_STUDIO PUBLIC juce::juce_recommended_config_flags)
endif()

# Disable LTO — MSVC ICE on FactoryPresets.cpp raw-string literals during link-time codegen
set_target_properties(DIDITAGAIN_STUDIO PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION FALSE
    INTERPROCEDURAL_OPTIMIZATION_RELEASE FALSE
)

# VS 2026 Insiders currently ICEs in Release on both the huge embedded preset
# translation unit and JUCE's generated VST3 wrapper when aggressive codegen is
# enabled. Keep DSP/shared code optimized, but compile only the fragile preset
# TU and thin format wrappers without optimisation/LTCG.
if(MSVC)
    target_compile_options(DIDITAGAIN_STUDIO PUBLIC
        /EHsc
        /GL-
        /Gy-
        /Gw-
        /Z7
        $<$<CONFIG:Release>:/Od>
        $<$<CONFIG:Release>:/Ob0>
    )

    target_link_options(DIDITAGAIN_STUDIO PUBLIC
        /LTCG:OFF
        /DEBUG:NONE
    )

    set_source_files_properties(Source/Presets/FactoryPresets.cpp PROPERTIES
        COMPILE_OPTIONS "/Od;/Ob0;/GL-;/Gy-;/Gw-;/Z7"
    )

    foreach(dida_format_target DIDITAGAIN_STUDIO DIDITAGAIN_STUDIO_VST3 DIDITAGAIN_STUDIO_Standalone)
        if(TARGET ${dida_format_target})
            set_target_properties(${dida_format_target} PROPERTIES
                INTERPROCEDURAL_OPTIMIZATION FALSE
                INTERPROCEDURAL_OPTIMIZATION_RELEASE FALSE
                MSVC_DEBUG_INFORMATION_FORMAT Embedded
            )
            target_compile_options(${dida_format_target} PRIVATE
                /GL-
                /Gy-
                /Gw-
                /Z7
            )
            target_link_options(${dida_format_target} PRIVATE
                /LTCG:OFF
            )
        endif()
    endforeach()

    if(TARGET DIDITAGAIN_STUDIO_VST3)
        target_compile_options(DIDITAGAIN_STUDIO_VST3 PRIVATE
            $<$<CONFIG:Release>:/Od>
            $<$<CONFIG:Release>:/Ob0>
        )
    endif()
endif()
