#pragma once

#include <juce_data_structures/juce_data_structures.h>

class SequencerEngine;

namespace PatternValueTree {

juce::ValueTree buildFromEngine(const SequencerEngine& engine);
void applyToEngine(SequencerEngine& engine, const juce::ValueTree& v, int64_t nowUs);

juce::Identifier rootId();

}  // namespace PatternValueTree
