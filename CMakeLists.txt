cmake_minimum_required(VERSION 3.22)
project(VoiceMultiplier VERSION 1.1.0)

option(VM_BUILD_TESTS "Build the JUCE-independent DSP unit tests" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        7.0.12
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# On CI we don't copy to the system VST3 folder
if(DEFINED ENV{CI})
    set(COPY_VST3 FALSE)
else()
    set(COPY_VST3 TRUE)
endif()

juce_add_plugin(VoiceMultiplier
    COMPANY_NAME              "YourCompany"
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          TRUE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD   ${COPY_VST3}
    PLUGIN_MANUFACTURER_CODE  "YrCo"
    PLUGIN_CODE               "VcMl"
    FORMATS                   VST3
    PRODUCT_NAME              "VoiceMultiplier"
    VST3_CATEGORIES           Fx Pitch Shift
)

# REQUIRED: generates JuceHeader.h in the build directory
juce_generate_juce_header(VoiceMultiplier)

target_sources(VoiceMultiplier PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

target_compile_definitions(VoiceMultiplier PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

# Silence noisy warnings from JUCE internals (GCC / Clang only, MSVC rejects these flags)
if(NOT MSVC)
    target_compile_options(VoiceMultiplier PRIVATE
        -Wno-sign-conversion
        -Wno-array-bounds
    )
endif()

target_link_libraries(VoiceMultiplier PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# ---------------------------------------------------------------
# Optional: plain C++ unit tests for Source/Dsp.h (no JUCE needed)
#   cmake -B build -DVM_BUILD_TESTS=ON && cmake --build build
#   ctest --test-dir build -C Release --output-on-failure
# ---------------------------------------------------------------
if(VM_BUILD_TESTS)
    enable_testing()
    add_executable(DspTests Tests/DspTests.cpp)
    target_include_directories(DspTests PRIVATE Source)
    target_compile_features(DspTests PRIVATE cxx_std_17)
    add_test(NAME DspTests COMMAND DspTests)
endif()
