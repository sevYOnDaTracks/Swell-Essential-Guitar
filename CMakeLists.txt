cmake_minimum_required(VERSION 3.22)
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
endif()
project(SwellEssentialGuitar VERSION 1.0.1 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE 8 local, partagé avec les autres plugins Swell (pas de téléchargement).
# JUCE : copie locale partagée avec les autres plugins Swell, sinon téléchargée (CI GitHub, Windows).
set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Swell-Reverse/JUCE" CACHE PATH "JUCE source directory")
if(EXISTS "${JUCE_DIR}/CMakeLists.txt")
    add_subdirectory("${JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")
else()
    include(FetchContent)
    FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.10 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Monocypher (BSD-2 / CC0) : vérification Ed25519 des fichiers de licence Swell.
# Copie locale prise dans Swell-Bass si elle existe, sinon téléchargée par CMake.
set(MONOCYPHER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Swell-Bass/build/_deps/monocypher-src" CACHE PATH "Monocypher source directory")
if(NOT EXISTS "${MONOCYPHER_DIR}/src/monocypher.c")
    include(FetchContent)
    FetchContent_Declare(monocypher GIT_REPOSITORY https://github.com/LoupVaillant/Monocypher.git GIT_TAG 4.0.3 GIT_SHALLOW TRUE SOURCE_SUBDIR none)
    FetchContent_MakeAvailable(monocypher)
    set(MONOCYPHER_DIR "${monocypher_SOURCE_DIR}")
endif()
add_library(monocypher STATIC "${MONOCYPHER_DIR}/src/monocypher.c" "${MONOCYPHER_DIR}/src/optional/monocypher-ed25519.c")
target_include_directories(monocypher PUBLIC "${MONOCYPHER_DIR}/src" "${MONOCYPHER_DIR}/src/optional")
set_target_properties(monocypher PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(EG_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND EG_FORMATS AU)
endif()

juce_add_plugin(SwellEssentialGuitar
    COMPANY_NAME "Swell Audio"
    BUNDLE_ID "audio.swell.essentialguitar"
    PLUGIN_MANUFACTURER_CODE Swel
    PLUGIN_CODE SesG
    FORMATS ${EG_FORMATS}
    PRODUCT_NAME "Swell Essential Guitar"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    VST3_CATEGORIES Instrument Sampler)

juce_add_binary_data(EGAssets SOURCES
    Assets/swell_mark.png
    Assets/geist_regular.ttf
    Assets/geist_semibold.ttf
    Assets/hero_acoustic.jpg
    Assets/hero_bass.jpg
    Assets/hero_electric.jpg
    Assets/hero_muted.jpg
    Assets/hero_nylon.jpg
    Assets/hero_rock.jpg)

target_sources(SwellEssentialGuitar PRIVATE
    Source/Library.cpp
    Source/Processor.cpp
    Source/Editor.cpp
    Source/Licence/SwellLicence.cpp)

target_compile_definitions(SwellEssentialGuitar PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(SwellEssentialGuitar
    PRIVATE EGAssets monocypher juce::juce_audio_utils juce::juce_dsp juce::juce_cryptography
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

option(EG_TESTS "Test hors ligne (son + captures de l'interface)" OFF)
if(EG_TESTS)
    add_executable(EGCheck Tests/Check.cpp)
    target_link_libraries(EGCheck PRIVATE SwellEssentialGuitar)
    target_include_directories(EGCheck PRIVATE Source $<TARGET_PROPERTY:SwellEssentialGuitar,INCLUDE_DIRECTORIES>)
    target_compile_definitions(EGCheck PRIVATE $<TARGET_PROPERTY:SwellEssentialGuitar,COMPILE_DEFINITIONS>)
endif()
