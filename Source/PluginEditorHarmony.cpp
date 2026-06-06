#include "PluginEditor.h"
#include "PluginProcessor.h"

#include "EditorHelpers.h"
#include "MidiExporter.h"

#include <nidmi_seq/ScaleBank.h>
#include <nidmi_seq/HarmonyEngine.h>

#include <cmath>

void NidmiSeqAudioProcessorEditor::setChordField(int field, int value) {
    const auto& prog = proc_.engine().pattern().chordProgression;
    const int   len  = juce::jlimit(0, 32, static_cast<int>(prog.len));
    const int   cur  = juce::jlimit(0, 31, harmonyCursor_);

    // Lit le slot existant, ou des valeurs par défaut s'il s'agit d'un ajout.
    // Durée par défaut d'un NOUVEAU slot = nombre de temps de la mesure (numerator) → 1 mesure pleine.
    int deg = 1, qual = 0, ext = 0, bass = 0;
    int dur = juce::jmax(1, static_cast<int>(proc_.engine().pattern().numerator));
    if (cur < len) {
        const auto& cs = prog.slots[static_cast<size_t>(cur)];
        deg = cs.degree; qual = static_cast<int>(cs.quality); ext = cs.extensions;
        bass = cs.bassOffset; dur = cs.durationBeats;
    } else if (len > 0) {
        // Ajout : copie complète du dernier slot édité (degré + qualité + ext + bass + durée).
        const auto& cs = prog.slots[static_cast<size_t>(len - 1)];
        deg = cs.degree; qual = static_cast<int>(cs.quality); ext = cs.extensions;
        bass = cs.bassOffset; dur = cs.durationBeats;
    }
    // (sinon : I major par défaut, deg=1 qual=0 ext=0 bass=0 dur=1 ci-dessus)
    switch (field) {
        case 0: deg  = juce::jlimit(1, 7, value); break;             // Degré
        case 1: qual = juce::jlimit(0, 5, value); break;             // Qualité (triade 0..5)
        // case 2 (Extensions par index) supprimé : les extensions sont des bits togglés par les
        //   noires (onBlackKey), pas un index. setChordField préserve les bits existants tels quels.
        case 3: bass = juce::jlimit(-12, 12, value); break;          // Bass
        default: dur = juce::jlimit(1, 64, value); break;            // Durée (en TEMPS)
    }

    // Ajout d'un slot : étend d'abord la longueur de la progression.
    if (cur >= len) {
        SequencerCommand cl;
        cl.id = SequencerCommandId::SetChordProgressionLen;
        cl.a  = static_cast<uint8_t>(cur + 1);
        proc_.controller().postCommand(cl);
    }
    SequencerCommand c;
    c.id = SequencerCommandId::SetChordSlot;
    c.a  = static_cast<uint8_t>(cur);
    c.b  = static_cast<uint8_t>(deg);
    c.c  = static_cast<uint8_t>(qual);
    // extensions est un uint16 : octet bas dans c.d, octet haut dans c.len (cf. SetChordSlot core).
    c.d  = static_cast<uint8_t>(ext & 0xFF);
    c.len = static_cast<uint8_t>((ext >> 8) & 0xFF);
    c.e  = static_cast<uint8_t>(static_cast<int8_t>(bass));
    c.f  = static_cast<uint8_t>(dur);
    proc_.controller().postCommand(c);
    buildScreenModel();
}

// Édite un marqueur de la lane de TONALITÉ. field : 0=root, 1=scale, 2=durée (temps).
// Curseur sur le marqueur d'ajout (cur >= len) → crée d'abord le marqueur (seed = clé maître).
void NidmiSeqAudioProcessorEditor::setKeyField(int field, int value) {
    const auto& kp  = proc_.engine().pattern().keyProgression;
    const int   len = juce::jlimit(0, 16, static_cast<int>(kp.len));
    const int   cur = juce::jlimit(0, 15, keyCursor_);
    const int   nScale = juce::jmax(1, static_cast<int>(scalebank::Count));

    // Valeurs par défaut d'un nouveau marqueur = tonalité maître + durée = 1 mesure.
    const auto& ps  = proc_.engine().projectSettings();
    int root  = static_cast<int>(ps.masterRootPc);
    int scale = static_cast<int>(ps.masterScaleId);
    int dur   = juce::jmax(1, static_cast<int>(proc_.engine().pattern().numerator));
    if (cur < len) {
        const auto& k = kp.slots[static_cast<size_t>(cur)];
        root = k.rootPc; scale = k.scaleId; dur = k.durationBeats;
    } else if (len > 0) {                       // ajout : copie le dernier marqueur
        const auto& k = kp.slots[static_cast<size_t>(len - 1)];
        root = k.rootPc; scale = k.scaleId; dur = k.durationBeats;
    }
    switch (field) {
        case 0:  root  = ((value % 12) + 12) % 12;          break;   // Tonique (wrap 12)
        case 1:  scale = ((value % nScale) + nScale) % nScale; break;// Gamme (wrap)
        default: dur   = juce::jlimit(1, 64, value);         break;  // Durée (temps)
    }
    if (cur >= len) {                            // étend la lane d'abord
        SequencerCommand cl;
        cl.id = SequencerCommandId::SetKeyProgressionLen;
        cl.a  = static_cast<uint8_t>(cur + 1);
        proc_.controller().postCommand(cl);
    }
    SequencerCommand c;
    c.id = SequencerCommandId::SetKeySlot;
    c.a  = static_cast<uint8_t>(cur);
    c.b  = static_cast<uint8_t>(root);
    c.c  = static_cast<uint8_t>(scale);
    c.f  = static_cast<uint8_t>(dur);
    proc_.controller().postCommand(c);
    buildScreenModel();
}

int NidmiSeqAudioProcessorEditor::sharedHarmonyMode() const {
    // Ne considère QUE les rows liées (mode != Chromatic). Si toutes les rows liées
    // partagent le même mode → ce mode ; si elles divergent → -1 (mixte) ; s'il n'y
    // a aucune row liée → défaut B1 (1).
    const auto& pat = proc_.engine().pattern();
    const int   nr  = juce::jlimit(0, 16, static_cast<int>(pat.numRows));
    int shared = -2;   // -2 = aucune row liée vue encore
    for (int r = 0; r < nr; ++r) {
        const int rm = static_cast<int>(pat.rows[static_cast<size_t>(r)].harmonyModeAt(static_cast<uint8_t>(editBar_)));
        if (rm == static_cast<int>(RowHarmonyMode::Chromatic))
            continue;                            // row déliée → ignorée (mesure éditée)
        if (shared == -2)        shared = rm;
        else if (shared != rm) { return -1; }    // rows liées divergentes → mixte
    }
    return (shared == -2) ? 1 : shared;          // défaut B1 si aucune row liée
}
