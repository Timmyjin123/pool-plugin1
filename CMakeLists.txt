cmake_minimum_required(VERSION 3.22)
project(Pool VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")

include(FetchContent)
FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.15 GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

set(FORMATS VST3 Standalone)
if(APPLE)
  list(APPEND FORMATS AU)
endif()

juce_add_plugin(Pool
  COMPANY_NAME "PoolLab"
  PLUGIN_MANUFACTURER_CODE Plab
  PLUGIN_CODE Pool
  FORMATS ${FORMATS}
  PRODUCT_NAME "Pool"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  VST3_CATEGORIES Fx
  AU_MAIN_TYPE kAudioUnitType_Effect
  COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(Pool PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_compile_definitions(Pool PUBLIC
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=1)
target_link_libraries(Pool PRIVATE
  juce::juce_audio_utils juce::juce_dsp
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags)
if(MSVC)
  target_compile_options(Pool PRIVATE /utf-8)
endif()

# Тест движка без JUCE
add_executable(engine_test tests/engine_test.cpp)
find_package(Threads REQUIRED)
target_link_libraries(engine_test PRIVATE Threads::Threads)
