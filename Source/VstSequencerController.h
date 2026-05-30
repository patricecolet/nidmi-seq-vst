#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <nidmi_seq/SequencerCommandApi.h>

class NidmiSeqAudioProcessor;

class VstSequencerController {
public:
    explicit VstSequencerController(NidmiSeqAudioProcessor& proc);

    bool postCommand(const SequencerCommand& cmd);
    void applyPatternFromTree(const juce::ValueTree& tree);

private:
    NidmiSeqAudioProcessor& proc_;
};
