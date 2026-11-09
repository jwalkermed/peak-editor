cmake_minimum_required(VERSION 3.22)

# Universal Binary (Apple Silicon + Intel). Must be set before project().
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Target architectures")
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")

project(PeakEditor VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(PEAK_COPY_PLUGINS "Copy AU/VST3 into ~/Library/Audio/Plug-Ins after building" OFF)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.5
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# Plain C++ MIDI/patch logic shared by the plugin and the tests (no JUCE).
set(PEAK_CORE_SOURCES
    Source/peak/core/Sysex.cpp
    Source/peak/core/Nrpn.cpp
    Source/peak/core/MidiDescribe.cpp
    Source/peak/core/Params.cpp
    Source/peak/core/ParamTable.cpp
    Source/peak/core/ParamMidi.cpp
    Source/peak/core/ParamFormat.cpp
    Source/peak/core/Patch.cpp)

juce_add_plugin(PeakEditor
    COMPANY_NAME "Peak Editor"
    BUNDLE_ID io.github.peakeditor
    PLUGIN_MANUFACTURER_CODE Jwkr
    PLUGIN_CODE Pked
    FORMATS Standalone AU VST3
    PRODUCT_NAME "Peak Editor"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    VST3_CATEGORIES Instrument Tools
    COPY_PLUGIN_AFTER_BUILD ${PEAK_COPY_PLUGINS})

target_sources(PeakEditor PRIVATE
    ${PEAK_CORE_SOURCES}
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/peak/CoreMidiTransport.mm
    Source/peak/PeakConnection.cpp
    Source/peak/PeakModel.cpp
    Source/ui/MidiMonitorPanel.cpp
    Source/ui/ParameterListPanel.cpp
    Source/ui/ParamRows.cpp
    Source/ui/PanelLayout.cpp
    Source/ui/FrontPanel.cpp
    Source/ui/KeyboardStrip.cpp)

target_include_directories(PeakEditor PRIVATE Source)

target_compile_definitions(PeakEditor PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(PeakEditor
    PRIVATE
        juce::juce_audio_utils
        "-framework CoreMIDI"
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Core unit tests: build and run with `ctest` or ./build/PeakCoreTests
enable_testing()
add_executable(PeakCoreTests Tests/CoreTests.cpp ${PEAK_CORE_SOURCES})
target_include_directories(PeakCoreTests PRIVATE Source Tests)
target_compile_definitions(PeakCoreTests PRIVATE PEAK_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Tests/fixtures")
add_test(NAME PeakCoreTests COMMAND PeakCoreTests)

# Renders the front panel to PNG (visual check against docs/reference/peak-front-panel.png)
juce_add_console_app(PanelSnapshot PRODUCT_NAME "PanelSnapshot")
target_sources(PanelSnapshot PRIVATE
    Tests/PanelSnapshot.cpp
    ${PEAK_CORE_SOURCES}
    Source/peak/CoreMidiTransport.mm
    Source/peak/PeakConnection.cpp
    Source/peak/PeakModel.cpp
    Source/ui/ParamRows.cpp
    Source/ui/PanelLayout.cpp
    Source/ui/FrontPanel.cpp)
target_include_directories(PanelSnapshot PRIVATE Source)
target_compile_definitions(PanelSnapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(PanelSnapshot PRIVATE juce::juce_gui_basics juce::juce_audio_basics "-framework CoreMIDI")
