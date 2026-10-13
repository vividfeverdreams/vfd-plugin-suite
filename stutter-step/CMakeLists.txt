cmake_minimum_required(VERSION 3.22)
project(StutterStep VERSION 0.4.0 LANGUAGES C CXX)

if (APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(JUCE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE" CACHE PATH "Pinned JUCE source")
add_subdirectory("${JUCE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")

juce_add_binary_data(StutterStepAssets SOURCES
    Assets/DeviceShell.png
    ../shared/VfdLogo.svg
    Assets/ChakraPetch-SemiBold.ttf
    Assets/ChakraPetch-BoldItalic.ttf)

juce_add_plugin(StutterStep
    COMPANY_NAME "VIVID FEVER DREAMS"
    BUNDLE_ID com.yoiboi.stutterstep
    PLUGIN_MANUFACTURER_CODE Yoib
    PLUGIN_CODE Stp1
    FORMATS VST3 AU
    PRODUCT_NAME "STUTTER-STEP"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Assets/AppIcon.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/Assets/AppIcon.png"
    DESCRIPTION "Swing gate with slap delay for mid-bass and sub"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE)

set(STUTTER_CORE_SOURCES
    Source/Groove.cpp
    Source/SlapDelay.cpp
    Source/StutterProcessor.cpp)

target_sources(StutterStep PRIVATE ${STUTTER_CORE_SOURCES} Source/StutterEditor.cpp)
target_include_directories(StutterStep PRIVATE Source ../shared)
target_link_libraries(StutterStep PRIVATE
    StutterStepAssets
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
target_compile_definitions(StutterStep PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

if (APPLE)
    foreach(format_target StutterStep_VST3 StutterStep_AU)
        add_custom_command(TARGET ${format_target} POST_BUILD
            COMMAND /usr/bin/codesign --force --deep --sign - "$<TARGET_BUNDLE_DIR:${format_target}>"
            COMMAND /usr/bin/codesign --verify --deep --strict "$<TARGET_BUNDLE_DIR:${format_target}>"
            VERBATIM)
    endforeach()
endif()

option(STUTTER_BUILD_TESTS "Build the console test app" ON)
if (STUTTER_BUILD_TESTS)
    juce_add_console_app(StutterStepTests PRODUCT_NAME "STUTTER-STEP Tests")
    target_sources(StutterStepTests PRIVATE Tests/Tests.cpp ${STUTTER_CORE_SOURCES})
    target_include_directories(StutterStepTests PRIVATE Source ../shared)
    target_compile_definitions(StutterStepTests PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 STUTTER_HEADLESS=1
        JucePlugin_Name="STUTTER-STEP")
    target_link_libraries(StutterStepTests PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
endif()

# Offscreen editor renders for review (Tests/Shots.cpp writes PNGs).
juce_add_console_app(StutterStepShots PRODUCT_NAME "STUTTER-STEP Shots")
target_sources(StutterStepShots PRIVATE Tests/Shots.cpp ${STUTTER_CORE_SOURCES} Source/StutterEditor.cpp)
target_include_directories(StutterStepShots PRIVATE Source ../shared)
target_compile_definitions(StutterStepShots PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(StutterStepShots PRIVATE
    StutterStepAssets
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# Preset audition renders (Tests/Demos.cpp writes WAVs).
juce_add_console_app(StutterStepDemos PRODUCT_NAME "STUTTER-STEP Demos")
target_sources(StutterStepDemos PRIVATE Tests/Demos.cpp ${STUTTER_CORE_SOURCES})
target_include_directories(StutterStepDemos PRIVATE Source ../shared)
target_compile_definitions(StutterStepDemos PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 STUTTER_HEADLESS=1)
target_link_libraries(StutterStepDemos PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
