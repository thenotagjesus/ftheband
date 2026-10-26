# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 Session Jam contributors
cmake_minimum_required(VERSION 3.22)
project(SessionJam VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ASIO is ON by default on Windows (ASIO4ALL is the primary driver), always OFF elsewhere.
if(WIN32)
    option(ENABLE_ASIO "Build with Steinberg ASIO support" ON)
else()
    set(ENABLE_ASIO OFF CACHE BOOL "ASIO is Windows-only" FORCE)
endif()
set(ASIO_SDK_DIR "" CACHE PATH "Optional local ASIO SDK root (contains common/iasiodrv.h); fetched automatically if empty")

# VST2 hosting (also loads VST1 plugins) needs the legacy Steinberg VST2 SDK, which is no longer distributed.
option(ENABLE_VST2 "Host VST2/VST1 plugins (requires a VST2 SDK)" OFF)
set(VST2_SDK_DIR "" CACHE PATH "VST2 SDK root (contains pluginterfaces/vst2.x/aeffect.h)")

include(FetchContent)
# Pinned JUCE. To use a local checkout: -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.8
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

if(ENABLE_VST2)
    if(NOT EXISTS "${VST2_SDK_DIR}/pluginterfaces/vst2.x/aeffect.h")
        message(FATAL_ERROR "ENABLE_VST2=ON but VST2_SDK_DIR (${VST2_SDK_DIR}) lacks pluginterfaces/vst2.x/aeffect.h")
    endif()
    juce_set_vst2_sdk_path("${VST2_SDK_DIR}")
endif()

# Band sample library (CC0 / CC BY, see CREDITS.md), downloaded at configure time from pinned commits.
option(SESSIONJAM_FETCH_SAMPLES "Download the band sample library at configure time" ON)
set(SESSIONJAM_SAMPLES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets/samples" CACHE PATH "Where the band samples live")
set(SESSIONJAM_DONATE_URL "" CACHE STRING "Optional donate link shown in the About box (empty = hidden)")
set(SESSIONJAM_SOURCE_URL "" CACHE STRING "Public source URL shown in the About box (AGPL: point users to the corresponding source)")
if(SESSIONJAM_FETCH_SAMPLES)
    execute_process(COMMAND ${CMAKE_COMMAND} -DMANIFEST=${CMAKE_CURRENT_SOURCE_DIR}/assets/samples.manifest
        -DSAMPLES_DIR=${SESSIONJAM_SAMPLES_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/FetchSamples.cmake RESULT_VARIABLE fetch_rc)
    if(NOT fetch_rc EQUAL 0)
        message(WARNING "Sample download failed; the band falls back to synth voices")
    endif()
endif()

juce_add_gui_app(SessionJam
    PRODUCT_NAME "Session Jam"
    COMPANY_NAME "SessionJam"
    VERSION 0.1.0)
juce_generate_juce_header(SessionJam)

target_sources(SessionJam PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/Engine/AudioEngine.cpp
    Source/Engine/PluginChain.cpp
    Source/UI/PluginUI.cpp
    Source/Recording/StemRecorder.cpp
    Source/Cli/Cli.cpp)
target_include_directories(SessionJam PRIVATE Source)

target_compile_definitions(SessionJam PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:SessionJam,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:SessionJam,JUCE_VERSION>"
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=0
    JUCE_PLUGINHOST_LADSPA=0
    SESSIONJAM_SAMPLES_DIR="${SESSIONJAM_SAMPLES_DIR}"
    SESSIONJAM_DONATE_URL="${SESSIONJAM_DONATE_URL}"
    SESSIONJAM_SOURCE_URL="${SESSIONJAM_SOURCE_URL}")

# Platform audio backends: only enable what exists on each OS (JACK/ALSA headers are Linux-only).
if(WIN32)
    target_compile_definitions(SessionJam PRIVATE JUCE_WASAPI=1 JUCE_DIRECTSOUND=1)
elseif(UNIX AND NOT APPLE)
    target_compile_definitions(SessionJam PRIVATE JUCE_ALSA=1 JUCE_JACK=1)
endif()

if(ENABLE_VST2)
    target_compile_definitions(SessionJam PRIVATE JUCE_PLUGINHOST_VST=1)
    target_link_libraries(SessionJam PRIVATE juce_vst2_sdk)
    message(STATUS "VST2/VST1 hosting enabled, SDK: ${VST2_SDK_DIR}")
else()
    target_compile_definitions(SessionJam PRIVATE JUCE_PLUGINHOST_VST=0)
endif()

if(ENABLE_ASIO)
    if(NOT WIN32)
        message(FATAL_ERROR "ENABLE_ASIO is only supported on Windows")
    endif()
    if(NOT ASIO_SDK_DIR)
        # Official Steinberg download (GPLv3 / proprietary dual licence). https://www.steinberg.net/asiosdk redirects here.
        FetchContent_Declare(asiosdk
            URL https://download.steinberg.net/sdk_downloads/ASIO-SDK_2.3.4_2025-10-15.zip
            URL_HASH SHA256=d5ebf0c20dd2c5f43771fd0c1418f4b361bf52434ee670097cfa6b3a335e2eca
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        FetchContent_MakeAvailable(asiosdk)
        set(ASIO_SDK_DIR "${asiosdk_SOURCE_DIR}")
    endif()
    if(NOT EXISTS "${ASIO_SDK_DIR}/common/iasiodrv.h")
        message(FATAL_ERROR "ASIO SDK not found at ${ASIO_SDK_DIR} (missing common/iasiodrv.h)")
    endif()
    message(STATUS "ASIO enabled, SDK: ${ASIO_SDK_DIR}")
    target_compile_definitions(SessionJam PRIVATE JUCE_ASIO=1)
    target_include_directories(SessionJam PRIVATE "${ASIO_SDK_DIR}/common")
else()
    target_compile_definitions(SessionJam PRIVATE JUCE_ASIO=0)
endif()

target_link_libraries(SessionJam PRIVATE
    juce::juce_audio_utils juce::juce_audio_processors juce::juce_audio_devices juce::juce_audio_formats
    juce::juce_audio_basics juce::juce_dsp juce::juce_gui_extra
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# Headless tools: --eval, --analyze, --render-demo, --jam (same code as the GUI's CLI mode)
juce_add_console_app(SessionJamCli PRODUCT_NAME "SessionJamCli")
juce_generate_juce_header(SessionJamCli)
target_sources(SessionJamCli PRIVATE Source/Cli/CliMain.cpp Source/Cli/Cli.cpp)
target_include_directories(SessionJamCli PRIVATE Source)
target_compile_definitions(SessionJamCli PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_FLAC=1 JUCE_USE_MP3AUDIOFORMAT=0
    SESSIONJAM_SAMPLES_DIR="${SESSIONJAM_SAMPLES_DIR}")
target_link_libraries(SessionJamCli PRIVATE juce::juce_audio_formats juce::juce_audio_basics juce::juce_dsp juce::juce_events juce::juce_gui_basics
    PUBLIC juce::juce_recommended_config_flags)
