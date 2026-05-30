#pragma once

#include <juce_core/juce_core.h>

class SequencerEngine;

namespace MidiExporter {

enum class Mode { Bake, Full };

struct Result {
    bool         success = false;
    juce::String message;
};

/// Rend offline le pattern courant en MIDI Type 1 et l'écrit dans `destFile`.
/// `Mode::Full` ajoute un Sequencer-Specific Meta (0xFF 0x7F) avec la sérialisation
/// binaire du projet (PatternValueTree) — restaurable par un NiDMI futur,
/// ignoré par les DAW.
Result exportToFile(const SequencerEngine& live, const juce::File& destFile, Mode mode);

}  // namespace MidiExporter
