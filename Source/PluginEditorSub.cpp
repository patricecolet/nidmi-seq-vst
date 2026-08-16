#include "PluginEditor.h"
#include "PluginProcessor.h"

#include "EditorHelpers.h"
#include "MidiExporter.h"

#include <nidmi_seq/ScaleBank.h>
#include <nidmi_seq/HarmonyEngine.h>

#include <cmath>

void NidmiSeqAudioProcessorEditor::setScreenPage(int pageIndex) {
    pageIndex   = juce::jlimit(0, PatternScreenModel::kNumPages - 1, pageIndex);
    const auto newPage = static_cast<PatternScreenModel::Page>(pageIndex);
    // Re-appui sur HARMONIE (déjà active, hors sub) = bascule le focus accords ↔ tonalité.
    if (newPage == PatternScreenModel::Page::Harmony
        && screenPage_ == PatternScreenModel::Page::Harmony && !inSub_) {
        harmonyFocus_ = (harmonyFocus_ == HarmonyFocus::Chords) ? HarmonyFocus::Tonality
                                                                : HarmonyFocus::Chords;
        updateKeysForPage();
        buildScreenModel();
        return;
    }
    // On reste dans le sub en changeant de Vue (PATTERN = on/off, ROLL = hauteurs…).
    screenPage_ = newPage;
    // Mémorise la dernière vue par famille (retour direct quand on re-sélectionne la famille).
    if (pageIndex == 0 || pageIndex == 1 || pageIndex == 3) lastEditView_ = pageIndex;
    else if (pageIndex == 4 || pageIndex == 5)              lastProjView_ = pageIndex;
    updateKeysForPage();
    buildScreenModel();
}

// Sélection par FAMILLE de vue (bouton dédié) : entre dans la famille (dernière vue utilisée),
// re-appui = sous-vue suivante de la famille. Famille : 0=Édition{Pat,Roll,Auto} 1=Harmonie 2=Projet{Glob,Song}.
void NidmiSeqAudioProcessorEditor::selectViewFamily(int family) {
    const int cur = static_cast<int>(screenPage_);
    if (family == 1) {                       // Harmonie : vue unique (re-appui = focus, géré par setScreenPage)
        setScreenPage(static_cast<int>(PatternScreenModel::Page::Harmony));
        return;
    }
    static const int kEdit[3] = {0, 1, 3};   // Pattern, PianoRoll, Auto
    static const int kProj[2] = {4, 5};      // Global, Song
    const int*  fam = (family == 0) ? kEdit : kProj;
    const int   n   = (family == 0) ? 3 : 2;
    int idx = -1;
    for (int i = 0; i < n; ++i) if (fam[i] == cur) idx = i;
    if (idx >= 0)                            // déjà dans la famille → sous-vue suivante (cycle)
        setScreenPage(fam[(idx + 1) % n]);
    else                                     // hors famille → dernière vue utilisée de la famille
        setScreenPage((family == 0) ? lastEditView_ : lastProjView_);
}

int NidmiSeqAudioProcessorEditor::activeSubIdx() const {
    const auto& pat = proc_.engine().pattern();
    if (pat.numRows == 0)
        return -1;
    const int r = juce::jlimit(0, static_cast<int>(pat.numRows) - 1, subHostRow_);
    const int n = juce::jlimit(1, 64, static_cast<int>(pat.rows[static_cast<size_t>(r)].numSteps));
    if (subHostStep_ < 0 || subHostStep_ >= n)
        return -1;
    const uint8_t idx = pat.rows[static_cast<size_t>(r)].step(static_cast<uint8_t>(editBar_), static_cast<uint8_t>(subHostStep_)).subPatIdx;
    return (idx == kNoSubPattern) ? -1 : static_cast<int>(idx);
}

int NidmiSeqAudioProcessorEditor::selectedStepSubIdx() const {
    const auto& pat = proc_.engine().pattern();
    if (pat.numRows == 0)
        return -1;
    const int r = juce::jlimit(0, static_cast<int>(pat.numRows) - 1, selectedRow_);
    const int n = juce::jlimit(1, 64, static_cast<int>(pat.rows[static_cast<size_t>(r)].numSteps));
    if (selectedStep_ < 0 || selectedStep_ >= n)
        return -1;
    const uint8_t idx = pat.rows[static_cast<size_t>(r)].step(static_cast<uint8_t>(editBar_), static_cast<uint8_t>(selectedStep_)).subPatIdx;
    return (idx == kNoSubPattern) ? -1 : static_cast<int>(idx);
}

int NidmiSeqAudioProcessorEditor::relevantSubIdx() const {
    return inSub_ ? activeSubIdx() : selectedStepSubIdx();
}

void NidmiSeqAudioProcessorEditor::enterOrCreateSub() {
    const auto& pat = proc_.engine().pattern();
    if (pat.numRows == 0)
        return;
    const int   r  = juce::jlimit(0, static_cast<int>(pat.numRows) - 1, selectedRow_);
    const int   n  = juce::jlimit(1, 64, static_cast<int>(pat.rows[static_cast<size_t>(r)].numSteps));
    const int   s  = juce::jlimit(0, n - 1, selectedStep_);
    const auto& sd = pat.rows[static_cast<size_t>(r)].step(static_cast<uint8_t>(editBar_), static_cast<uint8_t>(s));

    if (sd.subPatIdx == kNoSubPattern) {
        // Prédit le 1er sub libre (alloc choisit le premier numSteps==0), puis alloc + assign.
        int fi = -1;
        for (int i = 0; i < 16; ++i)
            if (pat.subPatterns[static_cast<size_t>(i)].numSteps == 0) { fi = i; break; }
        if (fi < 0)
            return;  // pool de subs plein
        SequencerCommand a; a.id = SequencerCommandId::AllocSubPattern; a.a = 4; a.b = 1;  // N=4, durée=1
        proc_.controller().postCommand(a);
        SequencerCommand c; c.id = SequencerCommandId::SetStepSubPattern;
        c.a = static_cast<uint8_t>(r); c.b = static_cast<uint8_t>(s); c.c = static_cast<uint8_t>(fi);
        c.f = static_cast<uint8_t>(editBar_);   // sub attaché au pas de la mesure éditée
        proc_.controller().postCommand(c);
        if (!sd.enabled) {   // le pas hôte doit être actif pour déclencher le sub
            SequencerCommand t; t.id = SequencerCommandId::ToggleStep;
            t.a = static_cast<uint8_t>(r); t.b = static_cast<uint8_t>(s); t.f = static_cast<uint8_t>(editBar_);
            proc_.controller().postCommand(t);
        }
    }
    inSub_ = true; subHostRow_ = r; subHostStep_ = s; subStep_ = 0;
    updateKeysForPage();
    buildScreenModel();
}

void NidmiSeqAudioProcessorEditor::exitSub() {
    inSub_ = false;
    updateKeysForPage();
    buildScreenModel();
}

bool NidmiSeqAudioProcessorEditor::harmonyActive() const {
    return proc_.engine().pattern().harmony.harmonyEnabled;
}

// Resolution harmonique d'une note — LE point unique.
//
// effectiveHarmony() est prive cote core, root/scale sont donc recomposes ici : la
// lane de tonalite prime des qu'elle a un marqueur, exactement comme currentKey().
// Une seule copie de cette recomposition dans tout l'editeur.
int NidmiSeqAudioProcessorEditor::resolvePlayedNote(int rawNote, int mode) const {
    const auto& pat = proc_.engine().pattern();
    const auto& ph  = pat.harmony;
    const auto  m   = static_cast<RowHarmonyMode>(mode);
    if (!ph.harmonyEnabled || m == RowHarmonyMode::Chromatic)
        return rawNote;

    const auto& ps = proc_.engine().projectSettings();
    int root  = ph.followMasterTonality ? static_cast<int>(ps.masterRootPc)
                                        : static_cast<int>(ph.rootPc);
    int scale = ph.followMasterTonality ? static_cast<int>(ps.masterScaleId)
                                        : static_cast<int>(ph.scaleId);
    if (pat.keyProgression.len > 0) {
        root  = static_cast<int>(pat.keyProgression.current().rootPc);
        scale = static_cast<int>(pat.keyProgression.current().scaleId);
    }
    return static_cast<int>(harmony::resolveDegreeToMidi(
        static_cast<uint8_t>(juce::jlimit(0, 127, rawNote)), m,
        pat.chordProgression.current(),
        ph.followProgression && pat.chordProgression.len > 0,
        static_cast<uint8_t>(scale), static_cast<uint8_t>(root)));
}

// Note hote RESOLUE : celle que le moteur utilise reellement comme ancre d'un sub
// relatif (triggerRowStep passe la note resolue a startSubPattern).
//
// Elle renvoyait la note STOCKEE. Sous harmonie, l'offset enregistre en cliquant une
// hauteur dans un sub relatif valait donc « pas cliquee − hote STOCKEE », alors que
// la lecture joue « hote RESOLUE + offset » : la note atterrissait ailleurs que la ou
// on venait de la poser, decalee de l'ecart harmonique.
int NidmiSeqAudioProcessorEditor::subHostNote() const {
    const auto& pat = proc_.engine().pattern();
    if (pat.numRows == 0) return 60;
    const int r = juce::jlimit(0, static_cast<int>(pat.numRows) - 1, subHostRow_);
    const auto& row = pat.rows[static_cast<size_t>(r)];
    const int n = juce::jlimit(1, 64, static_cast<int>(row.numSteps));
    const int s = juce::jlimit(0, n - 1, subHostStep_);
    return resolvePlayedNote(
        static_cast<int>(row.step(static_cast<uint8_t>(editBar_), static_cast<uint8_t>(s)).note),
        static_cast<int>(row.harmonyModeAt(static_cast<uint8_t>(editBar_))));
}

void NidmiSeqAudioProcessorEditor::postSubStepPitch(int subStepIndex, int absolutePitch) {
    const int subIdx = activeSubIdx();
    if (subIdx < 0) return;
    const auto& sp = proc_.engine().pattern().subPatterns[static_cast<size_t>(subIdx)];
    const int   sn = juce::jlimit(1, 16, static_cast<int>(sp.numSteps));
    if (subStepIndex < 0 || subStepIndex >= sn) return;
    const auto& sd = sp.steps[static_cast<size_t>(subStepIndex)];
    // En relatif : stocke l'offset centré sur 64 (note jouée = hôte + offset).
    int store = juce::jlimit(0, 127, absolutePitch);
    if (sp.relativeToHost)
        store = juce::jlimit(0, 127, 64 + (absolutePitch - subHostNote()));
    SequencerCommand c;
    c.id = SequencerCommandId::SetSubStep;
    c.a  = static_cast<uint8_t>(subIdx);
    c.b  = static_cast<uint8_t>(subStepIndex);
    c.c  = static_cast<uint8_t>(store);
    c.d  = sd.velocity > 0 ? sd.velocity : 100;
    c.e  = sd.gate > 0 ? sd.gate : 80;
    proc_.controller().postCommand(c);
    buildScreenModel();
}

void NidmiSeqAudioProcessorEditor::toggleSubMode() {
    // Cible : en drill-in le sub édité, sinon le sub du pas SÉLECTIONNÉ (PATTERN/ROLL).
    const int subIdx = relevantSubIdx();
    if (subIdx < 0) return;   // pas de sub pertinent → inactif
    const bool now = proc_.engine().pattern().subPatterns[static_cast<size_t>(subIdx)].relativeToHost;
    SequencerCommand c;
    c.id = SequencerCommandId::SetSubPatternRelative;
    c.a  = static_cast<uint8_t>(subIdx);
    c.x  = !now;
    proc_.controller().postCommand(c);
    buildScreenModel();
}
