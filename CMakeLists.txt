cmake_minimum_required(VERSION 3.22)
project(nidmi_seq_vst VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(
    "${CMAKE_CURRENT_SOURCE_DIR}/../nidmi-sequencer-core"
    "${CMAKE_BINARY_DIR}/nidmi_seq_core"
    EXCLUDE_FROM_ALL
)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.6
)
FetchContent_MakeAvailable(JUCE)

# Hôte de dev optionnel : charge VST3/AU (graph audio + MIDI) sans ouvrir une grosse DAW.
option(NIDMI_SEQ_BUILD_AUDIO_PLUGIN_HOST "Compiler JUCE AudioPluginHost (tests intermediaires)" ON)

juce_add_plugin(NidmiSeq
    COMPANY_NAME "NiDMI"
    BUNDLE_ID com.nidmi.seq
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    PLUGIN_MANUFACTURER_CODE Nidm
    PLUGIN_CODE Nseq
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "NiDMI Seq"
    COPY_PLUGIN_AFTER_BUILD TRUE
)

target_sources(NidmiSeq
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/PluginEditorScreenModel.cpp
        Source/PluginEditorEncoders.cpp
        Source/PluginEditorGrammar.cpp
        Source/PluginEditorInput.cpp
        Source/PluginEditorHarmony.cpp
        Source/PluginEditorSub.cpp
        Source/PluginEditorAuto.cpp
        Source/HardwareStyleComponents.cpp
        Source/ScreenPatternView.cpp
        Source/ScreenRollView.cpp
        Source/ScreenHarmonyView.cpp
        Source/ScreenAutoGlobalView.cpp
        Source/PianoKeysPanel.cpp
        Source/HostTimeMapper.cpp
        Source/VstSequencerController.cpp
        Source/PatternValueTree.cpp
        Source/CommandFifo.cpp
        Source/MidiClockTransport.cpp
        Source/MidiExporter.cpp
        Source/DeviceProfile.cpp
)

target_include_directories(NidmiSeq PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Source"
)

target_compile_definitions(NidmiSeq
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(NidmiSeq
    PRIVATE
        nidmi_seq
        juce::juce_audio_utils
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

juce_generate_juce_header(NidmiSeq)

if(NIDMI_SEQ_BUILD_AUDIO_PLUGIN_HOST)
    add_subdirectory(
        "${juce_SOURCE_DIR}/extras/AudioPluginHost"
        "${CMAKE_BINARY_DIR}/nidmi_AudioPluginHost"
        EXCLUDE_FROM_ALL)
endif()

# Cible unique : plugin (AU/VST3/Standalone) + hôte de test quand l’option est active.
if(NIDMI_SEQ_BUILD_AUDIO_PLUGIN_HOST)
    add_custom_target(nidmi_seq_dev_session
        DEPENDS NidmiSeq_Standalone NidmiSeq_VST3 NidmiSeq_AU AudioPluginHost
        COMMENT "NiDMI Seq: standalone + formats plugin + AudioPluginHost")
else()
    add_custom_target(nidmi_seq_dev_session
        DEPENDS NidmiSeq_Standalone NidmiSeq_VST3 NidmiSeq_AU
        COMMENT "NiDMI Seq: standalone + formats plugin (AudioPluginHost desactive)")
endif()

message(STATUS "NiDMI Seq dev: cmake --build . --target nidmi_seq_dev_session "
               "puis ouvrir l'app Standalone sous NidmiSeq_artefacts/<Config>/Standalone/ "
               "ou lancer AudioPluginHost pour tester le VST3/AU.")
