cmake_minimum_required(VERSION 3.22)
project(BackScratch VERSION 0.11.0 LANGUAGES C CXX)

if (APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(JUCE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE" CACHE PATH "Pinned JUCE source")
add_subdirectory("${JUCE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")

juce_add_binary_data(BackScratchAssets SOURCES
    Assets/Shell.png
    Assets/Interior-v8.png
    ../shared/VfdLogo.svg
    Assets/ChakraPetch-SemiBold.ttf
    Assets/ChakraPetch-BoldItalic.ttf)

juce_add_plugin(BackScratch
    COMPANY_NAME "VIVID FEVER DREAMS"
    BUNDLE_ID com.yoiboi.backscratch
    PLUGIN_MANUFACTURER_CODE Yoib
    PLUGIN_CODE Bsc1
    FORMATS VST3 AU
    PRODUCT_NAME "BACK-SCRATCH"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Assets/AppIcon.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/Assets/AppIcon.png"
    DESCRIPTION "Live tempo-synced scratch pattern effect"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(BackScratch PRIVATE
    Source/BackScratchProcessor.cpp
    Source/PatternEditorState.cpp
    Source/PatternEditing.cpp
    Source/BackScratchEditor.cpp
    Source/ScratchPattern.cpp
    Source/LiveScratchEngine.cpp
    Source/PhraseBuilder.cpp
    Source/PresetBank.cpp)
target_include_directories(BackScratch PRIVATE Source ../shared)
target_link_libraries(BackScratch PRIVATE
    BackScratchAssets
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
target_compile_definitions(BackScratch PUBLIC
    BACK_SCRATCH_QUEUE_BRIDGE=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

if (APPLE)
    foreach(format_target BackScratch_VST3 BackScratch_AU)
        add_custom_command(TARGET ${format_target} POST_BUILD
            COMMAND /usr/bin/codesign --force --deep --sign - "$<TARGET_BUNDLE_DIR:${format_target}>"
            COMMAND /usr/bin/codesign --verify --deep --strict "$<TARGET_BUNDLE_DIR:${format_target}>"
            VERBATIM)
    endforeach()
endif()

juce_add_console_app(BackScratchTests PRODUCT_NAME "BACK-SCRATCH Tests")
target_sources(BackScratchTests PRIVATE Tests/ScratchTests.cpp Source/ScratchPattern.cpp Source/LiveScratchEngine.cpp Source/PatternEditing.cpp
    Source/PhraseBuilder.cpp Source/PresetBank.cpp)
target_include_directories(BackScratchTests PRIVATE Source)
target_link_libraries(BackScratchTests PRIVATE
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
target_compile_definitions(BackScratchTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)

juce_add_console_app(BackScratchHostSmoke PRODUCT_NAME "BACK-SCRATCH Host Smoke")
target_sources(BackScratchHostSmoke PRIVATE Tests/HostSmoke.cpp)
target_include_directories(BackScratchHostSmoke PRIVATE Source)
target_link_libraries(BackScratchHostSmoke PRIVATE
    BackScratch
    BackScratchAssets
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
target_compile_definitions(BackScratchHostSmoke PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=1)

# A plain VST3/AU host without BACK-SCRATCH code, for the 0.10.2 project
# compatibility check (it loads the old release's plug-in, read-only) and the
# in-process AU check (nothing is installed).
juce_add_console_app(BackScratchCompatHost PRODUCT_NAME "BACK-SCRATCH Compat Host")
target_sources(BackScratchCompatHost PRIVATE Tests/CompatHost.cpp)
target_include_directories(BackScratchCompatHost PRIVATE Tests)
target_link_libraries(BackScratchCompatHost PRIVATE
    juce::juce_audio_utils
    juce::juce_recommended_config_flags)
target_compile_definitions(BackScratchCompatHost PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=1)
