cmake_minimum_required(VERSION 3.22)
project(Memoria VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")

option(MEMORIA_TOOLS "Build the offline snapshot tool (Linux, for development)" OFF)

include(FetchContent)
FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.15 GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

set(FORMATS VST3 Standalone)
if(APPLE)
  list(APPEND FORMATS AU)
endif()

juce_add_plugin(Memoria
  COMPANY_NAME "PoolLab"
  PLUGIN_MANUFACTURER_CODE Plab
  PLUGIN_CODE Mmra
  FORMATS ${FORMATS}
  PRODUCT_NAME "Memoria"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  VST3_CATEGORIES Fx Delay
  AU_MAIN_TYPE kAudioUnitType_Effect
  COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(Memoria PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_compile_definitions(Memoria PUBLIC
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=1)
target_link_libraries(Memoria PRIVATE
  juce::juce_audio_utils
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags)
if(MSVC)
  target_compile_options(Memoria PRIVATE /utf-8)
endif()

# Тест движка без JUCE
add_executable(memoria_engine_test tests/engine_test.cpp)

if(MEMORIA_TOOLS)
  juce_add_console_app(memoria_snapshot PRODUCT_NAME "memoria_snapshot")
  target_sources(memoria_snapshot PRIVATE tools/snapshot.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
  target_compile_definitions(memoria_snapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1 JucePlugin_Name="Memoria")
  target_link_libraries(memoria_snapshot PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)
  juce_add_console_app(memoria_startup PRODUCT_NAME "memoria_startup")
  target_sources(memoria_startup PRIVATE tools/startup.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
  target_compile_definitions(memoria_startup PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Name="Memoria")
  target_link_libraries(memoria_startup PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)
  juce_add_console_app(memoria_loud PRODUCT_NAME "memoria_loud")
  target_sources(memoria_loud PRIVATE tools/loudtest.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
  target_compile_definitions(memoria_loud PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Name="Memoria")
  target_link_libraries(memoria_loud PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)
  juce_add_console_app(memoria_clicks PRODUCT_NAME "memoria_clicks")
  target_sources(memoria_clicks PRIVATE tools/clicks.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
  target_compile_definitions(memoria_clicks PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Name="Memoria")
  target_link_libraries(memoria_clicks PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)
endif()
