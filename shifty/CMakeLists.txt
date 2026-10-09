cmake_minimum_required(VERSION 3.22)
project(Shifty VERSION 0.5.1 LANGUAGES C CXX)

if (APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")
    set(CMAKE_OBJCXX_VISIBILITY_PRESET hidden)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(JUCE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE" CACHE PATH "JUCE 9.0.2 source directory")
if (NOT EXISTS "${JUCE_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "JUCE 9.0.2 source not found. Set -DJUCE_SOURCE_DIR=/path/to/JUCE")
endif()
add_subdirectory("${JUCE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")

set(SHIFTY_DSP_SOURCES
    Source/Dsp/FormantEngine.cpp
    Source/Dsp/TalkEngine.cpp
    Source/Dsp/MouthEngine.cpp)

option(SHIFTY_BUILD_TESTS "Build DSP validation and demo renderer" ON)
if (SHIFTY_BUILD_TESTS)
    juce_add_console_app(ShiftyDspTests PRODUCT_NAME "SHIFTY DSP Tests")
    target_sources(ShiftyDspTests PRIVATE
        Tests/DspSmoke.cpp
        ${SHIFTY_DSP_SOURCES})
    target_include_directories(ShiftyDspTests PRIVATE Source)
    target_link_libraries(ShiftyDspTests PRIVATE
        juce::juce_audio_formats
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    target_compile_definitions(ShiftyDspTests PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)

    juce_add_console_app(ShiftyImpactTests PRODUCT_NAME "SHIFTY Formant Impact Tests")
    target_sources(ShiftyImpactTests PRIVATE Tests/FormantImpact.cpp ${SHIFTY_DSP_SOURCES})
    target_include_directories(ShiftyImpactTests PRIVATE Source/Dsp)
    target_link_libraries(ShiftyImpactTests PRIVATE juce::juce_audio_formats juce::juce_dsp
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    target_compile_definitions(ShiftyImpactTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
endif()

juce_add_binary_data(ShiftyAssets SOURCES
    ../shared/VfdLogo.svg
    Assets/DeviceShell-editor.png
    Assets/ShellGlow.png
    Assets/ChakraPetch-SemiBold.ttf
    Assets/ChakraPetch-BoldItalic.ttf)

juce_add_plugin(Shifty
    COMPANY_NAME "VIVID FEVER DREAMS"
    BUNDLE_ID com.yoiboi.shifty
    PLUGIN_MANUFACTURER_CODE Yoib
    PLUGIN_CODE Shf1
    FORMATS VST3 AU
    PRODUCT_NAME "SHIFTY"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Assets/AppIcon.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/Assets/AppIcon.png"
    DESCRIPTION "Formant shifter for bass audio"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE)

set(SHIFTY_PLUGIN_SOURCES
    Source/Plugin/ShiftyProcessor.cpp
    Source/Plugin/FactoryPresets.cpp
    Source/UI/ShiftyEditor.cpp
    ${SHIFTY_DSP_SOURCES})

target_sources(Shifty PRIVATE ${SHIFTY_PLUGIN_SOURCES})
target_include_directories(Shifty PRIVATE Source ../shared)
target_link_libraries(Shifty PRIVATE
    ShiftyAssets
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
target_compile_definitions(Shifty PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

if (APPLE)
    foreach(format_target Shifty_VST3 Shifty_AU)
        add_custom_command(TARGET ${format_target} POST_BUILD
            COMMAND /usr/bin/codesign --force --deep --sign - "$<TARGET_BUNDLE_DIR:${format_target}>"
            COMMAND /usr/bin/codesign --verify --deep --strict "$<TARGET_BUNDLE_DIR:${format_target}>"
            VERBATIM)
    endforeach()
endif()

if (SHIFTY_BUILD_TESTS)
    juce_add_console_app(ShiftyEditorSnapshot PRODUCT_NAME "SHIFTY Editor Snapshot")
    target_sources(ShiftyEditorSnapshot PRIVATE
        Tests/EditorSnapshot.cpp
        ${SHIFTY_PLUGIN_SOURCES})
    target_include_directories(ShiftyEditorSnapshot PRIVATE Source ../shared)
    target_link_libraries(ShiftyEditorSnapshot PRIVATE
        ShiftyAssets
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    target_compile_definitions(ShiftyEditorSnapshot PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_PLUGINHOST_VST3=1
        JUCE_PLUGINHOST_AU=1
        JUCE_VST3_CAN_REPLACE_VST2=0)
endif()
