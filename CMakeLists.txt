cmake_minimum_required(VERSION 3.22)
project(Arrhythmetic VERSION 1.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows: build the Microsoft C++ runtime into the .exe, so it runs on PCs
# without Visual Studio or the "Visual C++ Redistributable" installed.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# JUCE is downloaded automatically the first time you configure.
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.9
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_gui_app(Arrhythmetic
    PRODUCT_NAME "Arrhythmetic"
    COMPANY_NAME "Pano"
    ICON_BIG     Resources/icon256.png     # pixel art, drawn by Resources/make_icon.py
    ICON_SMALL   Resources/icon16.png)

# The 32 px icon, embedded in the app so the header can draw it.
juce_add_binary_data(ArrhythmeticData SOURCES Resources/icon32.png)

target_sources(Arrhythmetic PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/DrumEngine.cpp)

target_compile_definitions(Arrhythmetic PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Arrhythmetic,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Arrhythmetic,JUCE_VERSION>")

target_link_libraries(Arrhythmetic
    PRIVATE
        ArrhythmeticData
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
