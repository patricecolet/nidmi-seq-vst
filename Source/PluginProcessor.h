#pragma once

#include "CommandFifo.h"
#include "HostTimeMapper.h"
#include "MidiClockTransport.h"
#include "MidiExporter.h"
#include "VstSequencerController.h"

#include <nidmi_seq/SequencerClockDriver.h>
#include <nidmi_seq/SequencerEngine.h>

#include <juce_audio_processors/juce_audio_processors.h>

class NidmiSeqAudioProcessor : public juce::AudioProcessor {
public:
    NidmiSeqAudioProcessor();
    ~NidmiSeqAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }

    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }

    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& apvts() { return apvts_; }
    const juce::AudioProcessorValueTreeState& apvts() const { return apvts_; }

    SequencerEngine&       engine() { return engine_; }
    const SequencerEngine& engine() const { return engine_; }

    VstSequencerController& controller() { return controller_; }

    bool pushCommand(const SequencerCommand& cmd);
    void applyPatternTree(const juce::ValueTree& tree);

    int64_t approximateNowUs() const;

    void drainSeqEventsToMidi(juce::MidiBuffer& midi, int sampleOffset);

    /// Rend offline le pattern courant en MIDI Type 1 et l'écrit dans `destFile`.
    /// Bake = NoteOn/NoteOff/CC résolus ; Full = Bake + blob projet en Sequencer-Specific Meta.
    /// Sûr à appeler depuis le thread message — n'altère pas l'état live.
    MidiExporter::Result exportMidi(const juce::File& destFile, MidiExporter::Mode mode) const;

    /// Application Standalone JUCE (barre transport locale, pas de playhead DAW).
    /// `wrapperType` peut rester « Undefined » si le constructeur n’a pas vu le thread-local JUCE ;
    /// `PluginHostType::getPluginLoadedAs()` est alors le filet de sécurité.
    bool isStandaloneApp() const noexcept {
        if (wrapperType == wrapperType_Standalone)
            return true;
        return juce::PluginHostType::getPluginLoadedAs() == wrapperType_Standalone;
    }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void syncParametersToEngine(int64_t nowUs);

    juce::AudioProcessorValueTreeState apvts_;

    SequencerEngine        engine_;
    SequencerClockDriver   clock_;
    HostTimeMapper         hostTime_;
    MidiClockTransport     midiClock_;
    CommandFifo            commandFifo_;
    VstSequencerController controller_;

    double sampleRate_ = 44100.0;

    int64_t globalSamples_   = 0;
    int64_t lastMidiTickUs_  = 0;
    bool    lastUseMidiClock_ = false;

    bool   lastFollowHost_      = true;
    bool   lastUseHostBpm_      = true;
    float  lastBpmParam_        = -1.0f;
    int    lastLoop_            = -1;
    int    lastNumSteps_        = -1;
    int    lastNumRows_         = -1;
    int    lastTsNum_           = -1;
    int    lastTsDenIdx_        = -1;
    int    lastMasterRootIdx_   = -1;
    int    lastMasterScaleIdx_  = -1;
    float  lastHostBpmSynced_   = -1.0f;

    bool   wasHostPlaying_ = false;
    int64_t internalTimeUs_ = 0;
    SequencerEngine::State prevEngineState_ = SequencerEngine::State::STOPPED;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NidmiSeqAudioProcessor)
};
