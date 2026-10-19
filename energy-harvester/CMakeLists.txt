cmake_minimum_required(VERSION 3.22)
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Architecture")
project(EnergyHarvester VERSION 0.4.2 LANGUAGES C CXX)
if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_VISIBILITY_PRESET hidden)
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_subdirectory(external/JUCE)
juce_add_binary_data(EnergyAssets SOURCES Assets/DeviceShell-fullbleed.png Assets/AppIcon.png ../shared/VfdLogo.svg Assets/ChakraPetch-SemiBold.ttf Assets/ChakraPetch-BoldItalic.ttf)
juce_add_plugin(EnergyHarvester COMPANY_NAME "VIVID FEVER DREAMS" BUNDLE_ID com.yoiboi.energyharvester
    PLUGIN_MANUFACTURER_CODE Yoib PLUGIN_CODE Enh1 FORMATS VST3 AU PRODUCT_NAME "ENERGY HARVESTER"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Assets/AppIcon.png" ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/Assets/AppIcon.png"
    DESCRIPTION "Magnetic tape-start effect" IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE EDITOR_WANTS_KEYBOARD_FOCUS TRUE NEEDS_CURL TRUE COPY_PLUGIN_AFTER_BUILD FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect AU_SANDBOX_SAFE TRUE)
target_sources(EnergyHarvester PRIVATE Source/Dsp/HarvestEngine.cpp Source/Dsp/FxRack.cpp Source/EnergyHarvesterProcessor.cpp Source/EnergyHarvesterEditor.cpp Source/Ui/HarvestLights.cpp Source/Presets.cpp)
target_include_directories(EnergyHarvester PUBLIC Source PRIVATE ../shared)
# Linux: https for the INFO page's update check goes through libcurl, loaded only when used.
target_compile_definitions(EnergyHarvester PUBLIC JUCE_WEB_BROWSER=0 JUCE_LOAD_CURL_SYMBOLS_LAZILY=1 JUCE_VST3_CAN_REPLACE_VST2=0 ENERGY_HARVESTER_QUEUE_BRIDGE=1)
target_link_libraries(EnergyHarvester PRIVATE EnergyAssets juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
if(APPLE)
    target_link_options(EnergyHarvester_VST3 PRIVATE "LINKER:-exported_symbols_list,${CMAKE_CURRENT_SOURCE_DIR}/cmake/Vst3Exports.txt")
    add_custom_command(TARGET EnergyHarvester_VST3 POST_BUILD
        COMMAND /usr/bin/codesign --force --deep --sign - "$<TARGET_BUNDLE_DIR:EnergyHarvester_VST3>"
        COMMAND /usr/bin/codesign --verify --deep --strict "$<TARGET_BUNDLE_DIR:EnergyHarvester_VST3>" VERBATIM)
    add_custom_command(TARGET EnergyHarvester_AU POST_BUILD
        COMMAND /usr/bin/codesign --force --deep --sign - "$<TARGET_BUNDLE_DIR:EnergyHarvester_AU>"
        COMMAND /usr/bin/codesign --verify --deep --strict "$<TARGET_BUNDLE_DIR:EnergyHarvester_AU>" VERBATIM)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # libcurl is opened when an update check runs, so a computer without it still loads the plug-in.
    target_link_options(EnergyHarvester_VST3 PRIVATE "LINKER:--as-needed")
endif()
option(ENERGY_HARVESTER_BUILD_TESTS "Build the DSP, host and plug-in checks" ON)
if(NOT ENERGY_HARVESTER_BUILD_TESTS)
    return()
endif()
enable_testing()
add_executable(EnergyDspTests Tests/DspTests.cpp Source/Dsp/HarvestEngine.cpp Source/Dsp/FxRack.cpp)
target_include_directories(EnergyDspTests PRIVATE Source)
add_test(NAME EnergyDSP COMMAND EnergyDspTests)
juce_add_console_app(EnergyHostTests PRODUCT_NAME "ENERGY HARVESTER Host Tests")
target_sources(EnergyHostTests PRIVATE Tests/HostTests.cpp)
target_link_libraries(EnergyHostTests PRIVATE EnergyHarvester EnergyAssets juce::juce_audio_utils juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
target_compile_definitions(EnergyHostTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
juce_add_console_app(EnergyVstAutomation PRODUCT_NAME "ENERGY HARVESTER VST3 Automation Tests")
target_sources(EnergyVstAutomation PRIVATE Tests/VstAutomation.cpp)
if(APPLE)
    set_source_files_properties(Tests/VstAutomation.cpp PROPERTIES LANGUAGE OBJCXX)
endif()
target_link_libraries(EnergyVstAutomation PRIVATE EnergyHarvester EnergyAssets juce::juce_audio_utils juce::juce_recommended_config_flags)
target_compile_definitions(EnergyVstAutomation PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=1 JucePlugin_Build_VST3=1)
add_test(NAME EnergyHostState COMMAND EnergyHostTests)
add_test(NAME EnergyVst3Automation COMMAND EnergyVstAutomation "$<TARGET_BUNDLE_DIR:EnergyHarvester_VST3>")
# Audio Unit: registered in-process from the build folder (nothing is installed) and checked like auval.
if(APPLE)
    add_executable(EnergyAuValidation Tests/AuValidation.mm)
    target_link_libraries(EnergyAuValidation PRIVATE "-framework AudioToolbox" "-framework CoreFoundation" "-framework Foundation" "-framework CoreAudio")
    add_test(NAME EnergyAuValidation COMMAND EnergyAuValidation "$<TARGET_BUNDLE_DIR:EnergyHarvester_AU>")
endif()
# Compatibility: a state saved by the shipped 0.3.2 bundle must render identically in this build.
set(ENERGY_COMPAT_BUNDLE "${CMAKE_CURRENT_SOURCE_DIR}/../../outputs/Vivid-Fever-Dreams-Suite-v1.2.2/Install Vivid Fever Dreams Suite.app/Contents/Resources/Payload/ENERGY HARVESTER.vst3"
    CACHE PATH "ENERGY HARVESTER 0.3.2 VST3 used by the compatibility test (read only)")
if(EXISTS "${ENERGY_COMPAT_BUNDLE}")
    add_test(NAME EnergyCompat032 COMMAND EnergyVstAutomation --compat "${ENERGY_COMPAT_BUNDLE}" "$<TARGET_BUNDLE_DIR:EnergyHarvester_VST3>")
endif()
