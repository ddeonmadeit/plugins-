# dd_add_plugin(<Target>
#     PRODUCT_NAME  "Display Name"
#     PLUGIN_CODE   Abcd              # 4 chars, unique per plugin, >=1 uppercase
#     KIND          SYNTH | EFFECT
#     VERSION       1.0.0
#     SOURCES       Source/a.cpp ...
#     PRESETS_DIR   Presets           # optional: every *.json below is embedded as factory presets
# )
#
# Creates:
#   <Target>_VST3 / _AU / _Standalone   the plugin formats (AU only on macOS)
#   <Target>_Harness                    headless console app (render presets to WAV, UI snapshot, stress test)
#
# Plugin sources may use DD_PLUGIN_NAME / DD_PLUGIN_VERSION / DD_PLUGIN_COMPANY instead of JucePlugin_* macros,
# so the same sources compile unchanged into the harness.

set(DD_COMPANY_NAME "ddeonmadeit")
set(DD_MANUFACTURER_CODE "Ddeo")
set(DD_BUNDLE_PREFIX "com.ddeonmadeit")
set(DD_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")

function(dd_add_plugin target)
    cmake_parse_arguments(DD "" "PRODUCT_NAME;PLUGIN_CODE;KIND;VERSION;PRESETS_DIR" "SOURCES" ${ARGN})

    if(NOT DD_VERSION)
        set(DD_VERSION 1.0.0)
    endif()

    set(formats VST3 Standalone)
    if(APPLE)
        list(APPEND formats AU)
    endif()

    if(DD_KIND STREQUAL "SYNTH")
        set(kind_args IS_SYNTH TRUE NEEDS_MIDI_INPUT TRUE VST3_CATEGORIES Instrument Synth
                      AU_MAIN_TYPE kAudioUnitType_MusicDevice)
    else()
        set(kind_args IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE VST3_CATEGORIES Fx
                      AU_MAIN_TYPE kAudioUnitType_Effect)
    endif()

    string(TOLOWER "${target}" target_lower)

    juce_add_plugin(${target}
        VERSION                  ${DD_VERSION}
        PRODUCT_NAME             "${DD_PRODUCT_NAME}"
        COMPANY_NAME             "${DD_COMPANY_NAME}"
        BUNDLE_ID                "${DD_BUNDLE_PREFIX}.${target_lower}"
        PLUGIN_MANUFACTURER_CODE ${DD_MANUFACTURER_CODE}
        PLUGIN_CODE              ${DD_PLUGIN_CODE}
        FORMATS                  ${formats}
        NEEDS_MIDI_OUTPUT        FALSE
        IS_MIDI_EFFECT           FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD  FALSE
        HARDENED_RUNTIME_ENABLED FALSE
        ${kind_args})

    target_sources(${target} PRIVATE ${DD_SOURCES})

    set(common_defs
        DD_PLUGIN_NAME="${DD_PRODUCT_NAME}"
        DD_PLUGIN_VERSION="${DD_VERSION}"
        DD_PLUGIN_COMPANY="${DD_COMPANY_NAME}"
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_REPORT_APP_USAGE=0)

    target_compile_definitions(${target} PUBLIC ${common_defs})

    set(preset_lib "")
    if(DD_PRESETS_DIR)
        file(GLOB_RECURSE preset_files CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${DD_PRESETS_DIR}/*.json")
        if(preset_files)
            juce_add_binary_data(${target}_Presets
                HEADER_NAME PresetData.h
                NAMESPACE   PresetData
                SOURCES     ${preset_files})
            set_target_properties(${target}_Presets PROPERTIES POSITION_INDEPENDENT_CODE ON)
            set(preset_lib ${target}_Presets)
            target_compile_definitions(${target} PUBLIC DD_HAS_PRESET_DATA=1)
        endif()
    endif()

    target_link_libraries(${target}
        PRIVATE
            ${preset_lib}
            dd_dsp dd_core dd_gui dd_fonts
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

    if(DD_BUILD_HARNESS)
        juce_add_console_app(${target}_Harness PRODUCT_NAME "${target}Harness")
        target_sources(${target}_Harness PRIVATE ${DD_SOURCES} "${DD_REPO_ROOT}/tools/harness/HarnessMain.cpp")
        target_include_directories(${target}_Harness PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")
        target_compile_definitions(${target}_Harness PRIVATE ${common_defs} DD_HARNESS=1)
        if(preset_lib)
            target_compile_definitions(${target}_Harness PRIVATE DD_HAS_PRESET_DATA=1)
        endif()
        target_link_libraries(${target}_Harness
            PRIVATE
                ${preset_lib}
                dd_dsp dd_core dd_gui dd_fonts
                juce::juce_audio_processors
                juce::juce_audio_formats
                juce::juce_dsp
            PUBLIC
                juce::juce_recommended_config_flags
                juce::juce_recommended_warning_flags)
    endif()
endfunction()
