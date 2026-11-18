cmake_minimum_required(VERSION 3.22)
project(FLUIDIC VERSION 0.1.0 LANGUAGES C CXX)

# FLUIDIC — drip synthesis instrument (VST3 + Standalone).
# JUCE is downloaded automatically at configure time; on Windows so is the WebView2 SDK.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")   # no VC++ redistributable needed
endif()

option(FLUIDIC_BUILD_PLUGIN "Build the FLUIDIC plugin" ON)
option(FLUIDIC_BUILD_TESTS  "Build the offline render tools used by the parity tests" OFF)
option(FLUIDIC_COPY_AFTER_BUILD "Copy the VST3 into the system VST3 folder after building (needs admin on Windows)" OFF)
set(FLUIDIC_JUCE_DIR "" CACHE PATH "Use a local JUCE checkout instead of downloading it")

if(FLUIDIC_JUCE_DIR)
    add_subdirectory(${FLUIDIC_JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.4 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(FLUIDIC_DSP Source/dsp/FluidicEngine.cpp Source/dsp/FxBus.cpp)

if(FLUIDIC_BUILD_PLUGIN)
    if(WIN32)
        # WebView2 SDK (the Edge engine the interface runs in), fetched from NuGet and linked statically
        set(WV2_VERSION 1.0.1901.177)
        set(WV2_ROOT "${CMAKE_BINARY_DIR}/webview2")
        set(WV2_DIR "${WV2_ROOT}/Microsoft.Web.WebView2.${WV2_VERSION}")
        if(NOT EXISTS "${WV2_DIR}/build/native/include/WebView2.h")
            file(DOWNLOAD "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/${WV2_VERSION}" "${WV2_ROOT}/webview2.zip" SHOW_PROGRESS STATUS wv2_status)
            list(GET wv2_status 0 wv2_code)
            if(NOT wv2_code EQUAL 0)
                message(FATAL_ERROR "Could not download the WebView2 SDK from nuget.org: ${wv2_status}")
            endif()
            file(ARCHIVE_EXTRACT INPUT "${WV2_ROOT}/webview2.zip" DESTINATION "${WV2_DIR}")
        endif()
        set(JUCE_WEBVIEW2_PACKAGE_LOCATION "${WV2_ROOT}")
    endif()

    juce_add_plugin(FLUIDIC
        PRODUCT_NAME "FLUIDIC"
        COMPANY_NAME "FLUIDIC"
        BUNDLE_ID "com.fluidic.fluidic"
        PLUGIN_MANUFACTURER_CODE Fldc
        PLUGIN_CODE Fdc1
        FORMATS VST3 Standalone
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        NEEDS_WEB_BROWSER TRUE
        NEEDS_WEBVIEW2 TRUE
        VST3_CATEGORIES Instrument Synth
        COPY_PLUGIN_AFTER_BUILD ${FLUIDIC_COPY_AFTER_BUILD})

    juce_add_binary_data(FluidicUI NAMESPACE BinaryData SOURCES ui/index.html)

    target_sources(FLUIDIC PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp ${FLUIDIC_DSP})
    target_include_directories(FLUIDIC PRIVATE Source)
    target_compile_definitions(FLUIDIC PUBLIC
        JUCE_WEB_BROWSER=1
        JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)
    target_link_libraries(FLUIDIC
        PRIVATE FluidicUI juce::juce_audio_utils juce::juce_dsp juce::juce_gui_extra
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
endif()

if(FLUIDIC_BUILD_TESTS)
    # Offline renderer for the engine + effects bus (no GUI): used to check the C++ against the web prototype
    juce_add_console_app(fluidic_render PRODUCT_NAME "fluidic_render")
    target_sources(fluidic_render PRIVATE tests/render_full.cpp ${FLUIDIC_DSP})
    target_include_directories(fluidic_render PRIVATE Source)
    target_compile_definitions(fluidic_render PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
    target_link_libraries(fluidic_render PRIVATE juce::juce_dsp juce::juce_audio_basics juce::juce_core
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
