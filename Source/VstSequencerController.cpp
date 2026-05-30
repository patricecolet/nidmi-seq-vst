#include "VstSequencerController.h"
#include "PluginProcessor.h"

VstSequencerController::VstSequencerController(NidmiSeqAudioProcessor& proc)
    : proc_(proc) {}

bool VstSequencerController::postCommand(const SequencerCommand& cmd) {
    return proc_.pushCommand(cmd);
}

void VstSequencerController::applyPatternFromTree(const juce::ValueTree& tree) {
    proc_.applyPatternTree(tree);
}
