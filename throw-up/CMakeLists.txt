cmake_minimum_required(VERSION 3.22)
project(ThrowUp VERSION 0.2.2 LANGUAGES C CXX)
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
juce_add_binary_data(ThrowUpAssets SOURCES Assets/AppIcon.png Assets/DeviceShell-fullbleed.png ../shared/VfdLogo.svg Assets/ChakraPetch-SemiBold.ttf Assets/ChakraPetch-BoldItalic.ttf)
juce_add_plugin(ThrowUp
  COMPANY_NAME "VIVID FEVER DREAMS" BUNDLE_ID com.yoiboi.throwup
  PLUGIN_MANUFACTURER_CODE Yoib PLUGIN_CODE Thr1 FORMATS VST3 AU PRODUCT_NAME "THROW-UP"
  ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Assets/AppIcon.png"
  ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/Assets/AppIcon.png"
  DESCRIPTION "Mid-bass reverb throws"
  # MIDI input drives the THROW trigger (note-on throws, note picks room). The
  # VST3 class IDs and parameter IDs are unchanged; VST3 gains an event bus.
  IS_SYNTH FALSE NEEDS_MIDI_INPUT TRUE NEEDS_MIDI_OUTPUT FALSE IS_MIDI_EFFECT FALSE
  AU_MAIN_TYPE kAudioUnitType_MusicEffect
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE NEEDS_CURL TRUE COPY_PLUGIN_AFTER_BUILD FALSE)
target_sources(ThrowUp PRIVATE Source/ThrowUpProcessor.cpp Source/ThrowUpEditor.cpp Source/ReverbEngine.cpp Source/Presets.cpp)
target_include_directories(ThrowUp PUBLIC Source PRIVATE ../shared)
target_link_libraries(ThrowUp PRIVATE ThrowUpAssets juce::juce_audio_utils juce::juce_dsp
  juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
# Linux: https for the INFO page's update check goes through libcurl, loaded only when used.
target_compile_definitions(ThrowUp PUBLIC JUCE_WEB_BROWSER=0 JUCE_LOAD_CURL_SYMBOLS_LAZILY=1 JUCE_VST3_CAN_REPLACE_VST2=0)
# juce_add_plugin publishes JUCE_STANDALONE_APPLICATION=JucePlugin_Build_Standalone
# on the shared-code target's interface. Keep it for the plugin targets, but stop
# it leaking into the console test hosts that link ThrowUp; those define
# JUCE_STANDALONE_APPLICATION=1 themselves (juce_add_console_app).
get_target_property(_throwup_interface_definitions ThrowUp INTERFACE_COMPILE_DEFINITIONS)
list(FILTER _throwup_interface_definitions EXCLUDE REGEX "^JUCE_STANDALONE_APPLICATION=")
set_target_properties(ThrowUp PROPERTIES INTERFACE_COMPILE_DEFINITIONS "${_throwup_interface_definitions}")
# THROW-UP takes MIDI notes only. JUCE would otherwise publish 2,080 "MIDI CC"
# proxy parameters on the VST3 controller (16 channels x 130 controllers).
if(TARGET ThrowUp_VST3)
  target_compile_definitions(ThrowUp_VST3 PRIVATE JUCE_VST3_EMULATE_MIDI_CC_WITH_PARAMETERS=0)
endif()
foreach(_throwup_format ThrowUp_VST3 ThrowUp_AU)
  if(TARGET ${_throwup_format})
    target_compile_definitions(${_throwup_format} PRIVATE JUCE_STANDALONE_APPLICATION=JucePlugin_Build_Standalone)
    if(APPLE)
      add_custom_command(TARGET ${_throwup_format} POST_BUILD
        COMMAND /usr/bin/codesign --force --deep --sign - "$<TARGET_BUNDLE_DIR:${_throwup_format}>"
        COMMAND /usr/bin/codesign --verify --deep --strict "$<TARGET_BUNDLE_DIR:${_throwup_format}>" VERBATIM)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
      # libcurl is opened when an update check runs, so a computer without it still loads the plug-in.
      target_link_options(${_throwup_format} PRIVATE "LINKER:--as-needed")
    endif()
  endif()
endforeach()
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
  # Hosts the built VST3 and AU in-process (the AU is registered for this process
  # only, from the build folder) and the read-only 0.1.5 payload for compatibility.
  target_compile_definitions(ThrowUpHostTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=1)
  if(APPLE)
    target_link_libraries(ThrowUpHostTests PRIVATE "-framework AudioUnit" "-framework CoreAudioKit")
  endif()
  if(APPLE)
    # This regression host inspects callbacks from the shipping VST3 bundle.
    # Its concrete controller test hook requires the same JUCE wrapper version.
    juce_add_console_app(ThrowUpVstPresetAutomation PRODUCT_NAME "THROW-UP VST3 Automation Tests")
    target_sources(ThrowUpVstPresetAutomation PRIVATE Tests/VstPresetAutomation.cpp)
    # Same wrapper configuration as the shipping VST3 (no MIDI-CC proxy parameters).
    set_source_files_properties(Tests/VstPresetAutomation.cpp PROPERTIES LANGUAGE OBJCXX
      COMPILE_DEFINITIONS JUCE_VST3_EMULATE_MIDI_CC_WITH_PARAMETERS=0)
    target_include_directories(ThrowUpVstPresetAutomation PRIVATE Source)
    target_link_libraries(ThrowUpVstPresetAutomation PRIVATE ThrowUp ThrowUpAssets juce::juce_audio_utils
      juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    target_compile_definitions(ThrowUpVstPresetAutomation PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
      JUCE_PLUGINHOST_VST3=1 JucePlugin_Build_VST3=1)
  endif()
endif()
