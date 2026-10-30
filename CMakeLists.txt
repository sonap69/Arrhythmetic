cmake_minimum_required(VERSION 3.22)
project(DR16 VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE is downloaded automatically the first time you configure.
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.9
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_gui_app(DR16
    PRODUCT_NAME "DR-16"
    COMPANY_NAME "Pano")

target_sources(DR16 PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/DrumEngine.cpp)

target_compile_definitions(DR16 PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:DR16,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:DR16,JUCE_VERSION>")

target_link_libraries(DR16
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
