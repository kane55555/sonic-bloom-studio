cmake_minimum_required(VERSION 3.22)
project(DIDITAGAIN_STUDIO VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- JUCE ---
# Set JUCE_DIR to your local JUCE installation path
# e.g. cmake -DJUCE_DIR=C:/JUCE ..
if(NOT DEFINED JUCE_DIR)
    set(JUCE_DIR "${CMAKE_SOURCE_DIR}/../../third_party/JUCE" CACHE PATH "Path to JUCE framework")
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
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD TRUE
)

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
    Source/Presets/PresetManager.cpp
    Source/Presets/FactoryPresets.cpp
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
