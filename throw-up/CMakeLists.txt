cmake_minimum_required(VERSION 3.22)
project(ThrowUp VERSION 0.1.5 LANGUAGES C CXX)
if(APPLE)
  enable_language(OBJCXX)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")
  set(CMAKE_OBJCXX_VISIBILITY_PRESET hidden)
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE/CMakeLists.txt")
  set(_throwup_default_juce "${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE")
else()
  set(_throwup_default_juce "${CMAKE_CURRENT_SOURCE_DIR}/../back-scratch/external/JUCE")
endif()
set(JUCE_SOURCE_DIR "${_throwup_default_juce}" CACHE PATH "JUCE 9.0.2 source")
if(NOT EXISTS "${JUCE_SOURCE_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "Set JUCE_SOURCE_DIR to a JUCE 9.0.2 checkout")
endif()
add_subdirectory("${JUCE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")
juce_add_binary_data(ThrowUpAssets SOURCES Assets/AppIcon.png Assets/DeviceShell-v2.png ../shared/VfdLogo.svg Assets/ChakraPetch-SemiBold.ttf Assets/ChakraPetch-BoldItalic.ttf)
juce_add_plugin(ThrowUp
  COMPANY_NAME "VIVID FEVER DREAMS" BUNDLE_ID com.yoiboi.throwup
  PLUGIN_MANUFACTURER_CODE Yoib PLUGIN_CODE Thr1 FORMATS VST3 PRODUCT_NAME "THROW-UP"
  ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Assets/AppIcon.png"
  ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/Assets/AppIcon.png"
  DESCRIPTION "Mid-bass reverb throws"
  IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE NEEDS_MIDI_OUTPUT FALSE IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE COPY_PLUGIN_AFTER_BUILD FALSE)
target_sources(ThrowUp PRIVATE Source/ThrowUpProcessor.cpp Source/ThrowUpEditor.cpp Source/ReverbEngine.cpp Source/Presets.cpp)
target_include_directories(ThrowUp PUBLIC Source PRIVATE ../shared)
target_link_libraries(ThrowUp PRIVATE ThrowUpAssets juce::juce_audio_utils juce::juce_dsp
  juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
target_compile_definitions(ThrowUp PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
if(APPLE)
  add_custom_command(TARGET ThrowUp_VST3 POST_BUILD
    COMMAND /usr/bin/codesign --force --deep --sign - "$<TARGET_BUNDLE_DIR:ThrowUp_VST3>"
    COMMAND /usr/bin/codesign --verify --deep --strict "$<TARGET_BUNDLE_DIR:ThrowUp_VST3>" VERBATIM)
endif()
option(THROW_UP_BUILD_TESTS "Build audio and plugin checks" ON)
if(THROW_UP_BUILD_TESTS)
  add_executable(ThrowUpDspTests Tests/DspTests.cpp Source/ReverbEngine.cpp Source/Presets.cpp)
  target_include_directories(ThrowUpDspTests PRIVATE Source)
  add_executable(ThrowUpWetLevelAudit Tests/WetLevelAudit.cpp Source/ReverbEngine.cpp Source/Presets.cpp)
  target_include_directories(ThrowUpWetLevelAudit PRIVATE Source)
  add_executable(ThrowUpWetLevelDemo Tests/WetLevelDemo.cpp Source/ReverbEngine.cpp Source/Presets.cpp)
  target_include_directories(ThrowUpWetLevelDemo PRIVATE Source)
  enable_testing()
  add_test(NAME ThrowUpDsp COMMAND ThrowUpDspTests)
  add_test(NAME ThrowUpWetLevels COMMAND ThrowUpWetLevelAudit
    "${CMAKE_CURRENT_BINARY_DIR}/wet-level-audit.csv" factory --verify)
  juce_add_console_app(ThrowUpHostTests PRODUCT_NAME "THROW-UP Host Tests")
  target_sources(ThrowUpHostTests PRIVATE Tests/HostTests.cpp)
  target_include_directories(ThrowUpHostTests PRIVATE Source)
  target_link_libraries(ThrowUpHostTests PRIVATE ThrowUp ThrowUpAssets juce::juce_audio_utils
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
  target_compile_definitions(ThrowUpHostTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=1)
  if(APPLE)
    # This regression host inspects callbacks from the shipping VST3 bundle.
    # Its concrete controller test hook requires the same JUCE wrapper version.
    juce_add_console_app(ThrowUpVstPresetAutomation PRODUCT_NAME "THROW-UP VST3 Automation Tests")
    target_sources(ThrowUpVstPresetAutomation PRIVATE Tests/VstPresetAutomation.cpp)
    set_source_files_properties(Tests/VstPresetAutomation.cpp PROPERTIES LANGUAGE OBJCXX)
    target_include_directories(ThrowUpVstPresetAutomation PRIVATE Source)
    target_link_libraries(ThrowUpVstPresetAutomation PRIVATE ThrowUp ThrowUpAssets juce::juce_audio_utils
      juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    target_compile_definitions(ThrowUpVstPresetAutomation PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
      JUCE_PLUGINHOST_VST3=1 JucePlugin_Build_VST3=1)
  endif()
endif()
