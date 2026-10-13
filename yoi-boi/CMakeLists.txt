cmake_minimum_required(VERSION 3.22)
project(YoiBoi VERSION 0.2.2 LANGUAGES C CXX)

if (APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 20)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
# JUCE's Apple modules are Objective-C++. Keep their C++ symbols private just
# like the C++ modules, so loading another JUCE plug-in cannot interpose them.
set(CMAKE_OBJCXX_VISIBILITY_PRESET hidden)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

set(JUCE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE" CACHE PATH "JUCE 9.0.2 source directory")
if (NOT EXISTS "${JUCE_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "JUCE 9.0.2 is required. Set -DJUCE_SOURCE_DIR=/path/to/JUCE.")
endif()
add_subdirectory("${JUCE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")

juce_add_binary_data(YoiBoiAssets SOURCES
    ../shared/VfdLogo.svg
    Assets/ChakraPetch-BoldItalic.ttf
    Assets/ChakraPetch-SemiBold.ttf
    Assets/HandheldShell-fullbleed.png)

juce_add_plugin(YoiBoi
    COMPANY_NAME "VIVID FEVER DREAMS"
    BUNDLE_ID com.yoiboi.synth
    PLUGIN_MANUFACTURER_CODE YBoi
    PLUGIN_CODE YB01
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "YOI-BOI"
    ICON_BIG Assets/AppIcon.png
    ICON_SMALL Assets/AppIcon.png
    DESCRIPTION "Talking bass synthesizer"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(YoiBoi PRIVATE
    Source/Plugin/YoiBoiProcessor.cpp
    Source/Plugin/ParameterMap.cpp
    Source/Plugin/BounceRenderer.cpp
    Source/Patch/FactoryPresets.cpp
    Source/Dsp/YoiVoice4.cpp
    Source/Dsp/YoiVoice.cpp
    Source/Dsp/Legacy/YoiVoice010.cpp
    Source/Dsp/Legacy/YoiVoice012.cpp
    Source/Patch/Patch.cpp
    Source/UI/YoiBoiEditor.cpp)

target_sources(YoiBoi_Standalone PRIVATE Source/Standalone/DeviceApplication.cpp)
target_compile_definitions(YoiBoi_Standalone PRIVATE JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)

if (APPLE)
    target_sources(YoiBoi PRIVATE Source/UI/WindowActions.mm)
else()
    target_sources(YoiBoi PRIVATE Source/UI/WindowActions.cpp)
endif()

target_include_directories(YoiBoi PRIVATE Source ../shared)
target_compile_features(YoiBoi PRIVATE cxx_std_20)
target_compile_definitions(YoiBoi PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(YoiBoi PRIVATE
    YoiBoiAssets
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# JUCE writes moduleinfo.json after its initial VST3 signature. Seal the final
# bundle, including that manifest, so a local development build verifies cleanly.
if (APPLE)
    target_link_options(YoiBoi_VST3 PRIVATE
        "LINKER:-exported_symbols_list,${CMAKE_CURRENT_SOURCE_DIR}/cmake/Vst3Exports.txt")
    foreach(bundle_target IN ITEMS YoiBoi_VST3 YoiBoi_AU YoiBoi_Standalone)
        add_custom_command(TARGET ${bundle_target} POST_BUILD
            COMMAND /usr/bin/codesign --force --deep --sign - "$<TARGET_BUNDLE_DIR:${bundle_target}>"
            COMMAND /usr/bin/codesign --verify --deep --strict "$<TARGET_BUNDLE_DIR:${bundle_target}>"
            VERBATIM)
    endforeach()
endif()

option(YOIBOI_BUILD_TESTS "Build standalone DSP/patch tests" ON)
if (YOIBOI_BUILD_TESTS)
    juce_add_console_app(YoiBoiTests PRODUCT_NAME "YOI-BOI Tests")
    target_sources(YoiBoiTests PRIVATE
        Tests/TestMain.cpp
        Tests/ProcessorTests.cpp
        Tests/EngineCompatibilityTests.cpp
        Tests/EditorInteractionTests.cpp
        Tests/RegressionTests.cpp
        Tests/LatestEngineTests.cpp
        Source/Plugin/ParameterMap.cpp
        Source/Plugin/BounceRenderer.cpp
        Source/Patch/FactoryPresets.cpp
        Source/Dsp/YoiVoice4.cpp
        Source/Dsp/YoiVoice.cpp
        Source/Dsp/Legacy/YoiVoice010.cpp
        Source/Dsp/Legacy/YoiVoice012.cpp
        Source/Patch/Patch.cpp
        Source/Patch/PatchTests.cpp
        Source/Plugin/YoiBoiProcessor.cpp
        Source/UI/YoiBoiEditor.cpp)
    target_include_directories(YoiBoiTests PRIVATE Source ../shared)
    if (APPLE)
        target_sources(YoiBoiTests PRIVATE Source/UI/WindowActions.mm)
    else()
        target_sources(YoiBoiTests PRIVATE Source/UI/WindowActions.cpp)
    endif()
    target_compile_features(YoiBoiTests PRIVATE cxx_std_20)
    target_compile_definitions(YoiBoiTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_UNIT_TESTS=1 JUCE_PLUGINHOST_VST3=1
        JUCE_PLUGINHOST_AU=1 YOIBOI_VERSION_STRING="${PROJECT_VERSION}")
    target_link_libraries(YoiBoiTests PRIVATE
        YoiBoiAssets
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
endif()
