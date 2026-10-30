# Tests for the shared INFO page and suite updates (VfdInfo.h, VfdUpdate.h).
#   cmake -S shared/Tests -B shared/Tests/build -DJUCE_SOURCE_DIR=/path/to/JUCE-9.0.2 && cmake --build shared/Tests/build
#   shared/Tests/build/VfdUpdateTests_artefacts/Release/VfdUpdateTests [--shots <folder>] [--live]
# --live talks to GitHub: it checks the latest release and downloads and verifies its installer (nothing is opened).
cmake_minimum_required(VERSION 3.22)
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Architecture")
project(VfdSharedTests VERSION 1.0.0 LANGUAGES C CXX)
if(APPLE)
    enable_language(OBJCXX)
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(JUCE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../energy-harvester/external/JUCE" CACHE PATH "JUCE 9.0.2 source directory")
add_subdirectory("${JUCE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")
juce_add_console_app(VfdUpdateTests PRODUCT_NAME "VFD Update Tests" NEEDS_CURL TRUE)
target_sources(VfdUpdateTests PRIVATE UpdateTests.cpp)
target_include_directories(VfdUpdateTests PRIVATE ..)
target_compile_definitions(VfdUpdateTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_LOAD_CURL_SYMBOLS_LAZILY=1 JUCE_MODAL_LOOPS_PERMITTED=1
    VFD_FONT_FILE="${CMAKE_CURRENT_SOURCE_DIR}/../../energy-harvester/Assets/ChakraPetch-SemiBold.ttf"
    VFD_LOGO_FILE="${CMAKE_CURRENT_SOURCE_DIR}/../VfdLogo.svg")
target_link_libraries(VfdUpdateTests PRIVATE juce::juce_gui_extra juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
