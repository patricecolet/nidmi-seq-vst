#include "PluginEditor.h"
#include "DeviceProfile.h"
#include "PluginProcessor.h"

#include "EditorHelpers.h"
#include "MidiExporter.h"

#include <nidmi_seq/ScaleBank.h>
#include <nidmi_seq/HarmonyEngine.h>

#include <cmath>

namespace {
juce::String formatParamValue(const juce::AudioProcessorValueTreeState& ap, const char* id) {
    auto* param = ap.getParameter(id);
    if (param == nullptr)
        return {};

    if (auto* ch = dynamic_cast<juce::AudioParameterChoice*>(param))
        return ch->getCurrentChoiceName();

    if (dynamic_cast<juce::AudioParameterBool*>(param) != nullptr)
        return (ap.getRawParameterValue(id) != nullptr && ap.getRawParameterValue(id)->load() > 0.5f)
                   ? juce::String("On")
                   : juce::String("Off");

    if (dynamic_cast<juce::AudioParameterInt*>(param) != nullptr) {
        const int v = ap.getRawParameterValue(id) != nullptr
                          ? (int) std::lround(ap.getRawParameterValue(id)->load())
                          : 0;
        return juce::String(v);
    }

    if (dynamic_cast<juce::AudioParameterFloat*>(param) != nullptr) {
        const float v = ap.getRawParameterValue(id) != nullptr ? ap.getRawParameterValue(id)->load() : 0.0f;
        return juce::String(v, 1);
    }

    return {};
}
}  // namespace

void NidmiSeqAudioProcessorEditor::buildScreenModel() {
    const auto& pat = proc_.engine().pattern();
    const auto  st  = proc_.engine().state();
    const bool  playing = (st == SequencerEngine::State::PLAYING);

    PatternScreenModel m;
    m.page        = screenPage_;
    m.numBars     = juce::jlimit(1, 4, static_cast<int>(pat.numBars));
    m.editBar     = juce::jlimit(0, m.numBars - 1, editBar_);
    m.playBar     = juce::jlimit(0, m.numBars - 1, static_cast<int>(proc_.engine().currentBar()));
    m.numRows     = juce::jlimit(0, 16, static_cast<int>(pat.numRows));
    m.selectedRow = juce::jlimit(0, juce::jmax(0, m.numRows - 1), selectedRow_);
    m.selectedStep = selectedStep_;
    m.recArmed    = recArmed_;
    // Presse-papier : pas source en attente (visible seulement sur la mesure éditée).
    if (stepClip_.valid && stepClip_.bar == editBar_) {
        m.clipRow   = stepClip_.row;
        m.clipStep  = stepClip_.step;
        m.clipBar   = stepClip_.bar;
        m.clipCut   = stepClip_.cut;
        m.clipScope = static_cast<int>(stepClip_.scope);
    } else {
        m.clipRow = m.clipStep = m.clipBar = -1;
        m.clipCut = false;
        m.clipScope = 0;
    }
    // Indicateur de fenêtre de page (touches) : seulement sur PATTERN/AUTO et si N>16.
    stepPage_ = juce::jlimit(0, stepPageCount() - 1, stepPage_);
    const bool pageRelevant = (screenPage_ == PatternScreenModel::Page::Pattern
                               || screenPage_ == PatternScreenModel::Page::Auto)
                              && stepPageCount() > 1;
    m.keyPageStart = pageRelevant ? stepPage_ * 16 : -1;
    m.keyPageCount = stepPageCount();
    // PIANO ROLL : la fenêtre de hauteurs suit le clavier (défilement Oct±).
    m.prBottomNote = (screenPage_ == PatternScreenModel::Page::PianoRoll)
                         ? juce::jlimit(0, 127, rollWhiteKeyMidi(0))
                         : -1;
    m.prVisibleSemis = (screenPage_ == PatternScreenModel::Page::PianoRoll)
                           ? juce::jlimit(1, 11, rollOctaves_) * 12
                           : 0;
    m.rowZoom     = rowZoom_;
    m.stepZoom    = juce::jlimit(1, 8, stepZoom_);
    m.padMode     = static_cast<int>(padMode_);
    m.masterHud   = (masterHudFrames_ > 0) ? masterHudText_ : juce::String();   // HUD transitoire
    m.playing     = playing;
    m.tsNum       = pat.numerator;
    m.tsDen       = pat.denominator;

    const DeviceProfile& prof = DeviceProfile::byIndex(proc_.deviceProfileIndex());

    if (auto* bpm = proc_.apvts().getRawParameterValue("bpm"))
        m.bpm = bpm->load();

    // Page GLOBAL : params projet (OLED) + « Canal » de la row + « Pattern » actif + « Mesures ».
    static_assert(static_cast<int>(kGlobalRowCount) <= PatternScreenModel::kMaxGlobalRows,
                  "kGlobalRowCount depasse global[] : agrandir kMaxGlobalRows");
    m.numGlobalParams = static_cast<int>(kGlobalRowCount);
    m.globalCursor    = juce::jlimit(0, juce::jmax(0, m.numGlobalParams - 1), oledParamIndex_);
    for (int i = 0; i < kGlobalRowChannel && i < m.numGlobalParams; ++i) {
        m.global[static_cast<size_t>(i)].name  = oledParamTitle(i);
        m.global[static_cast<size_t>(i)].value = formatParamValue(proc_.apvts(), oledParamId(i));
    }
    if (m.numGlobalParams > kGlobalRowChannel) {   // ligne = canal MIDI de la row sélectionnée
        const int sr = (pat.numRows > 0) ? juce::jlimit(0, static_cast<int>(pat.numRows) - 1, selectedRow_) : 0;
        const int ch = (pat.numRows > 0) ? static_cast<int>(pat.rows[static_cast<size_t>(sr)].channel) + 1 : 1;
        m.global[static_cast<size_t>(kGlobalRowChannel)].name  = "Canal R" + juce::String(sr + 1);
        m.global[static_cast<size_t>(kGlobalRowChannel)].value = juce::String(juce::jlimit(1, 16, ch));
    }
    if (m.numGlobalParams > kGlobalRowKind) {   // ligne = type de la row selectionnee
        const int sr = (pat.numRows > 0) ? juce::jlimit(0, static_cast<int>(pat.numRows) - 1, selectedRow_) : 0;
        const bool isCC = (pat.numRows > 0) && (pat.rows[static_cast<size_t>(sr)].kind == RowKind::CC);
        m.global[static_cast<size_t>(kGlobalRowKind)].name  = "Type R" + juce::String(sr + 1);
        m.global[static_cast<size_t>(kGlobalRowKind)].value = isCC ? "CC" : "Note";
    }
    if (m.numGlobalParams > kGlobalRowCCNum) {   // ligne = destination CC de la row
        const int sr = (pat.numRows > 0) ? juce::jlimit(0, static_cast<int>(pat.numRows) - 1, selectedRow_) : 0;
        const int cc = (pat.numRows > 0) ? static_cast<int>(pat.rows[static_cast<size_t>(sr)].ccNumber) : 74;
        const bool isCC = (pat.numRows > 0) && (pat.rows[static_cast<size_t>(sr)].kind == RowKind::CC);
        m.global[static_cast<size_t>(kGlobalRowCCNum)].name  = "CC# R" + juce::String(sr + 1);
        // Nomme par le profil quand il connait ce CC, et grise quand la row est
        // en Note : le reglage existe mais ne sert a rien.
        m.global[static_cast<size_t>(kGlobalRowCCNum)].value =
            isCC ? prof.label(cc) : ("(" + prof.label(cc) + ")");
    }
    if (m.numGlobalParams > kGlobalRowCCInterp) {   // ligne = lissage de la lane
        const int sr = (pat.numRows > 0) ? juce::jlimit(0, static_cast<int>(pat.numRows) - 1, selectedRow_) : 0;
        const bool isCC = (pat.numRows > 0) && (pat.rows[static_cast<size_t>(sr)].kind == RowKind::CC);
        const int  mode = (pat.numRows > 0)
                            ? static_cast<int>(pat.rows[static_cast<size_t>(sr)].ccInterp) : 0;
        m.global[static_cast<size_t>(kGlobalRowCCInterp)].name  = "Interp R" + juce::String(sr + 1);
        // Entre parentheses sur une row Note : le reglage existe, il ne sert pas.
        m.global[static_cast<size_t>(kGlobalRowCCInterp)].value =
            isCC ? juce::String(ccInterpName(mode))
                 : ("(" + juce::String(ccInterpName(mode)) + ")");
    }
    if (m.numGlobalParams > kGlobalRowPattern) {   // ligne = pattern actif de la banque
        const int ap = static_cast<int>(proc_.engine().activePatternIndex()) + 1;
        m.global[static_cast<size_t>(kGlobalRowPattern)].name  = "Pattern";
        m.global[static_cast<size_t>(kGlobalRowPattern)].value =
            juce::String(ap) + "/" + juce::String(static_cast<int>(kMaxPatterns));
    }
    if (m.numGlobalParams > kGlobalRowBars) {   // ligne = nombre de mesures du pattern
        const int nb = static_cast<int>(proc_.engine().patternNumBars());
        m.global[static_cast<size_t>(kGlobalRowBars)].name  = "Mesures";
        m.global[static_cast<size_t>(kGlobalRowBars)].value =
            juce::String(nb) + "/" + juce::String(static_cast<int>(kMaxBars));
    }

    if (m.numGlobalParams > kGlobalRowMode) {   // ligne = Pattern <-> Song
        m.global[static_cast<size_t>(kGlobalRowMode)].name  = "Mode";
        m.global[static_cast<size_t>(kGlobalRowMode)].value =
            proc_.engine().songMode() ? "Song" : "Pattern";
    }
    if (m.numGlobalParams > kGlobalRowCCBudget) {   // ligne = debit des lanes
        m.global[static_cast<size_t>(kGlobalRowCCBudget)].name  = "Debit CC";
        m.global[static_cast<size_t>(kGlobalRowCCBudget)].value =
            ccBudgetLabel(static_cast<int>(proc_.engine().ccRateBudget()));
    }
    if (m.numGlobalParams > kGlobalRowProfile) {   // ligne = profil d'appareil
        m.global[static_cast<size_t>(kGlobalRowProfile)].name  = "Profil";
        m.global[static_cast<size_t>(kGlobalRowProfile)].value = prof.name();
    }

    // Deux actions, declenchees par le PUSH de l'encodeur Valeur sur la ligne.
    // Elles ne portent pas de valeur : la colonne de droite dit quoi faire.
    if (m.numGlobalParams > kGlobalRowResetMappings) {
        m.global[static_cast<size_t>(kGlobalRowResetMappings)].name  = "Reset mappings";
        m.global[static_cast<size_t>(kGlobalRowResetMappings)].value = "push";
    }
    if (m.numGlobalParams > kGlobalRowResetValues) {
        m.global[static_cast<size_t>(kGlobalRowResetValues)].name  = "Reset valeurs";
        m.global[static_cast<size_t>(kGlobalRowResetValues)].value = "push";
    }

    // Page SONG : chaîne (arrangement) depuis l'engine.
    {
        const auto& chain = proc_.engine().chainProgram();
        const int   len   = juce::jlimit(0, 64, static_cast<int>(chain.len));
        m.songLen    = len;
        m.songCursor = juce::jlimit(0, len, songCursor_);
        m.songMode   = proc_.engine().songMode();
        m.songActive = static_cast<int>(proc_.engine().activePatternIndex()) + 1;
        for (int i = 0; i < len; ++i) {
            m.songSlots[static_cast<size_t>(i)].op = static_cast<int>(chain.slots[i].op);
            m.songSlots[static_cast<size_t>(i)].p1 = chain.slots[i].param1;
            m.songSlots[static_cast<size_t>(i)].p2 = chain.slots[i].param2;
        }
    }

    // Filtre harmonique : tonalité effective (même logique que la section HARMONIE plus bas,
    // calculée ici en amont car la boucle rows en a besoin pour résoudre la note jouée).
    // effectiveHarmony() est privé côté core → on recompose root/scale manuellement.
    const auto& psHarm   = proc_.engine().projectSettings();
    const auto& phHarm   = pat.harmony;
    int         effRootB = phHarm.followMasterTonality ? static_cast<int>(psHarm.masterRootPc)
                                                       : static_cast<int>(phHarm.rootPc);
    int         effScaleB = phHarm.followMasterTonality ? static_cast<int>(psHarm.masterScaleId)
                                                        : static_cast<int>(phHarm.scaleId);
    // Lane de tonalité : si elle a des marqueurs, la clé courante (marqueur en lecture, ou
    // slot 0 à l'arrêt) remplace la tonalité maître pour la résolution affichée.
    if (pat.keyProgression.len > 0) {
        effRootB  = static_cast<int>(pat.keyProgression.current().rootPc);
        effScaleB = static_cast<int>(pat.keyProgression.current().scaleId);
    }
    const bool  harmActive = phHarm.harmonyEnabled;
    const bool  progActive = phHarm.followProgression && pat.chordProgression.len > 0;
    const ChordSlot currentChord = pat.chordProgression.current();

    // Durée d'un mesure en µs : mêmes formules que le core (barDurationUs_).
    // numerator/denominator = taille de mesure (indépendante de numSteps = divisions).
    const double bpmEff       = (m.bpm > 0.0) ? static_cast<double>(m.bpm) : 120.0;
    const double usPerQuarter = 60'000'000.0 / bpmEff;
    const double barUs        = static_cast<double>(pat.numerator) * 4.0 * usPerQuarter
                                / static_cast<double>(juce::jmax<int>(1, pat.denominator));

    for (int r = 0; r < m.numRows; ++r) {
        const auto& row = pat.rows[static_cast<size_t>(r)];
        auto&       dst = m.rows[static_cast<size_t>(r)];
        const int   n   = juce::jlimit(1, 64, static_cast<int>(row.numSteps));
        dst.numSteps    = n;
        dst.channel     = static_cast<int>(row.channel) + 1;
        dst.harmonyMode = static_cast<int>(row.harmonyModeAt(static_cast<uint8_t>(editBar_)));  // mode de la mesure ÉDITÉE
        dst.muted       = row.muted;
        // Libellé musical (1/16, 1/8T, 5:tps…) + durée réelle d'un pas en ms.
        // Calculs côté éditeur → PatternScreen reste agnostique du core.
        dst.divLabel    = divisionLabel(n, pat.numerator, pat.denominator);
        const double stepUs = barUs / static_cast<double>(juce::jmax(1, n));
        dst.stepMs      = static_cast<int>(stepUs / 1000.0 + 0.5);
        dst.isCC        = (row.kind == RowKind::CC);
        dst.ccNumber    = static_cast<int>(row.ccNumber);
        dst.ccLabel     = dst.isCC ? prof.label(dst.ccNumber)      : juce::String();
        dst.ccShort     = dst.isCC ? prof.shortLabel(dst.ccNumber) : juce::String();
        // Row soumise à l'harmonie : harmonie active globale ET mode != Chromatic.
        const bool bound = harmActive && row.harmonyModeAt(static_cast<uint8_t>(editBar_)) != RowHarmonyMode::Chromatic;
        dst.harmonyBound = bound;
        for (int s = 0; s < n; ++s) {
            const auto& sd = row.step(static_cast<uint8_t>(editBar_), static_cast<uint8_t>(s));   // mesure ÉDITÉE
            dst.enabled[static_cast<size_t>(s)]  = sd.enabled;
            dst.note[static_cast<size_t>(s)]     = sd.note;
            dst.velocity[static_cast<size_t>(s)] = sd.velocity;
            dst.gate[static_cast<size_t>(s)]     = sd.gate;
            dst.subIdx[static_cast<size_t>(s)]   = (sd.subPatIdx == kNoSubPattern)
                                                       ? static_cast<signed char>(-1)
                                                       : static_cast<signed char>(sd.subPatIdx);
            dst.span[static_cast<size_t>(s)]     = static_cast<unsigned char>(juce::jlimit(1, 64, static_cast<int>(sd.span)));
            if (bound) {
                const uint8_t pn = harmony::resolveDegreeToMidi(
                    sd.note, row.harmonyModeAt(static_cast<uint8_t>(editBar_)), currentChord, progActive,
                    static_cast<uint8_t>(effScaleB), static_cast<uint8_t>(effRootB));
                dst.playedNote[static_cast<size_t>(s)] = pn;
                dst.snapped[static_cast<size_t>(s)]    = (pn != sd.note);
            } else {
                dst.playedNote[static_cast<size_t>(s)] = sd.note;
                dst.snapped[static_cast<size_t>(s)]    = false;
            }
        }
        // Playhead visible UNIQUEMENT si la mesure éditée == mesure jouée (sinon la grille
        // affiche une autre mesure que celle en cours de lecture → pas de tête de lecture).
        const uint8_t raw = proc_.engine().currentStepForRow(static_cast<uint8_t>(r));
        const bool    sameBar = (editBar_ == static_cast<int>(proc_.engine().currentBar()));
        dst.playhead = (playing && sameBar && raw != 0xFF && raw < n) ? static_cast<int>(raw) : -1;
    }

    // Subpatterns : pool (aperçu niché) + état d'édition « drill-in ».
    for (int i = 0; i < 16; ++i) {
        const auto& sp = pat.subPatterns[static_cast<size_t>(i)];
        const int   sn = juce::jlimit(0, 16, static_cast<int>(sp.numSteps));
        auto&       sv = m.subs[static_cast<size_t>(i)];
        sv.numSteps = sn;
        sv.relative = sp.relativeToHost;
        sv.duration = juce::jlimit(1, 64, static_cast<int>(sp.duration));
        for (int s = 0; s < sn; ++s) {
            sv.enabled[static_cast<size_t>(s)]  = sp.steps[static_cast<size_t>(s)].enabled;
            sv.note[static_cast<size_t>(s)]     = sp.steps[static_cast<size_t>(s)].note;
            sv.velocity[static_cast<size_t>(s)] = sp.steps[static_cast<size_t>(s)].velocity;
            sv.span[static_cast<size_t>(s)]     = static_cast<unsigned char>(juce::jmax(1, static_cast<int>(sp.steps[static_cast<size_t>(s)].span)));
            sv.gate[static_cast<size_t>(s)]     = static_cast<unsigned char>(juce::jlimit(1, 100, static_cast<int>(sp.steps[static_cast<size_t>(s)].gate)));
        }
    }
    m.inSub      = inSub_;
    m.subEditIdx = activeSubIdx();
    m.subHostRow = subHostRow_;
    m.subHostStep = subHostStep_;
    {
        const int hr = juce::jlimit(0, juce::jmax(0, m.numRows - 1), subHostRow_);
        const int hn = (m.numRows > 0)
            ? juce::jlimit(1, 64, static_cast<int>(pat.rows[static_cast<size_t>(hr)].numSteps)) : 1;
        const int hs = juce::jlimit(0, hn - 1, subHostStep_);
        m.subHostNote = (m.numRows > 0)
            ? static_cast<int>(pat.rows[static_cast<size_t>(hr)].step(static_cast<uint8_t>(editBar_), static_cast<uint8_t>(hs)).note) : 60;
    }
    if (inSub_ && m.subEditIdx >= 0) {
        const int sn = juce::jmax(1, m.subs[static_cast<size_t>(m.subEditIdx)].numSteps);
        subStep_ = juce::jlimit(0, sn - 1, subStep_);
    }
    m.subStep = subStep_;

    // Page HARMONIE : progression d'accords du pattern.
    const auto& prog = pat.chordProgression;
    m.progLen       = juce::jlimit(0, 32, static_cast<int>(prog.len));
    m.progCurrent   = (playing && prog.len > 0) ? static_cast<int>(prog.idx) : -1;
    m.harmonyCursor = juce::jlimit(0, 31, harmonyCursor_);

    // Tonalité effective : effectiveHarmony() est privé côté core → recalcul ici.
    // followMasterTonality → root/scale = master ; sinon réglages du pattern.
    const auto& ps     = proc_.engine().projectSettings();
    const auto& ph     = pat.harmony;
    int effRoot  = ph.followMasterTonality ? static_cast<int>(ps.masterRootPc)
                                           : static_cast<int>(ph.rootPc);
    int effScale = ph.followMasterTonality ? static_cast<int>(ps.masterScaleId)
                                           : static_cast<int>(ph.scaleId);
    if (pat.keyProgression.len > 0) {   // lane de tonalité : clé courante prioritaire
        effRoot  = static_cast<int>(pat.keyProgression.current().rootPc);
        effScale = static_cast<int>(pat.keyProgression.current().scaleId);
    }
    m.harmonyRootPc        = juce::jlimit(0, 11, effRoot);
    m.harmonyScaleId       = juce::jlimit(0, 11, effScale);
    m.harmonyEnabled       = ph.harmonyEnabled;
    m.followProgression    = ph.followProgression;
    m.followMasterTonality = ph.followMasterTonality;

    // Mode harmonique partagé des rows LIÉES (helper unique : exclut Chromatic, -1 = mixte,
    // B1 par défaut s'il n'y a aucune row liée). Le bandeau "Rows" dérive de rows[r].harmonyMode.
    m.harmonySharedMode = sharedHarmonyMode();

    for (int i = 0; i < m.progLen; ++i) {
        const auto& cs = prog.slots[static_cast<size_t>(i)];
        auto&       cv = m.chord[static_cast<size_t>(i)];
        cv.degree        = cs.degree;
        cv.quality       = static_cast<int>(cs.quality);
        cv.extensions    = cs.extensions;
        cv.bassOffset    = cs.bassOffset;
        cv.durationBeats = cs.durationBeats;
        cv.rootPc        = static_cast<int>(
            harmony::degreePitchClass(static_cast<uint8_t>(cs.degree),
                                      static_cast<uint8_t>(effScale),
                                      static_cast<uint8_t>(effRoot)));
    }

    // Lane de TONALITÉ : marqueurs + focus.
    const auto& kp  = pat.keyProgression;
    m.keyLen        = juce::jlimit(0, 16, static_cast<int>(kp.len));
    m.keyCurrent    = (playing && kp.len > 0) ? static_cast<int>(kp.idx) : -1;
    m.keyCursor     = juce::jlimit(0, 15, keyCursor_);
    m.harmonyFocus  = static_cast<int>(harmonyFocus_);
    for (int i = 0; i < m.keyLen; ++i) {
        const auto& ks = kp.slots[static_cast<size_t>(i)];
        auto&       kv = m.keyLane[static_cast<size_t>(i)];
        kv.rootPc        = ks.rootPc;
        kv.scaleId       = ks.scaleId;
        kv.durationBeats = ks.durationBeats;
    }

    // Page AUTO : P-locks CC de la row sélectionnée, slot actif.
    m.autoSlot  = juce::jlimit(0, 7, autoSlot_);
    m.autoField = juce::jlimit(0, 1, autoField_);
    if (m.numRows > 0) {
        const int   ar    = juce::jlimit(0, m.numRows - 1, selectedRow_);
        const auto& arow  = pat.rows[static_cast<size_t>(ar)];
        const int   an    = juce::jlimit(1, 64, static_cast<int>(arow.numSteps));
        for (int i = 0; i < 8; ++i) {
            int cc = -1;
            for (int s = 0; s < an; ++s) {
                const auto& lk = arow.step(static_cast<uint8_t>(editBar_), static_cast<uint8_t>(s)).ccLocks[static_cast<size_t>(i)];
                if (lk.ccNumber != 0xFF) { cc = lk.ccNumber; break; }
            }
            m.autoSlotCc[i]    = cc;
            m.ccSlotLabel[i]   = (cc >= 0) ? prof.shortLabel(cc) : juce::String();
        }
        int activeCc = -1;
        for (int s = 0; s < an; ++s) {
            const auto& lk = arow.step(static_cast<uint8_t>(editBar_), static_cast<uint8_t>(s)).ccLocks[static_cast<size_t>(m.autoSlot)];
            m.autoValue[s] = (lk.ccNumber != 0xFF) ? static_cast<int>(lk.value) : -1;
            if (activeCc < 0 && lk.ccNumber != 0xFF) activeCc = lk.ccNumber;
        }
        m.autoCc = (activeCc >= 0) ? activeCc : autoCcDefault_;
    }

    screen_.setModel(m);
}

void NidmiSeqAudioProcessorEditor::refreshPianoKeysFromEngine() {
    const auto& pat = proc_.engine().pattern();
    if (pat.numRows == 0) {
        piano_.setBlackKeyLed(-1, false);
        return;
    }

    // LED d'état d'une noire (le helper n'en gère qu'une à la fois) :
    //  - HARMONIE (hors sub) : 1ʳᵉ extension active du slot courant (noires 0..6).
    //  - sinon : noire 9 rel/abs du sub pertinent (drill-in : sub édité ; sinon sub du pas sél.).
    // Rafraîchi à chaque tick du timer → reflète l'état même si modifié ailleurs.
    if (!inSub_ && screenPage_ == PatternScreenModel::Page::Harmony) {
        // Même ordre/mapping que kBlackExtBit dans onBlackKey (11 noires).
        static const uint16_t kBit[11] = {
            kExtFlat7, kExtMaj7, kExtFlat5, kExtSharp5,
            kExt9, kExt11, kExt13, kExtFlat9, kExtSharp9, kExtSharp11, kExtFlat13};
        const auto& prog = pat.chordProgression;
        const int   cur  = juce::jlimit(0, 31, harmonyCursor_);
        const uint16_t ext = (cur < static_cast<int>(prog.len))
                               ? prog.slots[static_cast<size_t>(cur)].extensions : 0;
        int litExt = -1;
        for (int i = 0; i < 11; ++i)
            if ((ext & kBit[i]) != 0) { litExt = i; break; }
        piano_.setBlackKeyLed(litExt, litExt >= 0);
    } else {
        const int subIdx = relevantSubIdx();
        // La LED « Mode rel/abs » (noire 9) ne s'affiche QUE quand la noire 9 EST la fonction
        // Mode : drill-in (toujours), PATTERN (noires = fonctions), ROLL seulement sous Shift
        // (hors Shift les noires sont des notes → pas de LED verte parasite). Auto/Global : jamais.
        const bool key9IsMode = inSub_
            || screenPage_ == PatternScreenModel::Page::Pattern
            || (screenPage_ == PatternScreenModel::Page::PianoRoll && shiftActive());
        const bool ledOn = key9IsMode && subIdx >= 0
            && pat.subPatterns[static_cast<size_t>(subIdx)].relativeToHost;
        piano_.setBlackKeyLed(9, ledOn);
    }

    // Édition d'un sub.
    if (inSub_) {
        const int subIdx = activeSubIdx();
        const int sn = (subIdx >= 0)
            ? juce::jlimit(1, 16, static_cast<int>(pat.subPatterns[static_cast<size_t>(subIdx)].numSteps)) : 0;
        piano_.setBlackKeyHighlight(-1);
        if (screenPage_ == PatternScreenModel::Page::PianoRoll && subIdx >= 0 && sn > 0) {
            // Clavier de hauteurs : surligne la blanche = hauteur du sous-pas courant.
            for (int i = 0; i < 16; ++i) piano_.whiteKey(i).setToggleState(false, juce::dontSendNotification);
            const auto& sp = pat.subPatterns[static_cast<size_t>(subIdx)];
            const auto& sd = sp.steps[static_cast<size_t>(juce::jlimit(0, sn - 1, subStep_))];
            const int   disp = sp.relativeToHost
                ? juce::jlimit(0, 127, subHostNote() + (static_cast<int>(sd.note) - 64)) : static_cast<int>(sd.note);
            int hl = -1;
            for (int i = 0; i < 16; ++i) if (rollWhiteKeyMidi(i) == disp) { hl = i; break; }
            piano_.setPlayheadStep(hl);
        } else {
            // PATTERN-sub : blanches = sous-pas (on/off) + curseur.
            for (int i = 0; i < 16; ++i)
                piano_.whiteKey(i).setToggleState(
                    subIdx >= 0 && i < sn && pat.subPatterns[static_cast<size_t>(subIdx)].steps[static_cast<size_t>(i)].enabled,
                    juce::dontSendNotification);
            piano_.setPlayheadStep((sn > 0) ? juce::jlimit(0, sn - 1, subStep_) : -1);
        }
        return;
    }

    const int    sr      = juce::jlimit(0, static_cast<int>(pat.numRows) - 1, selectedRow_);
    const auto&  row     = pat.rows[static_cast<size_t>(sr)];
    const int    rowN    = juce::jmax<int>(1, row.numSteps);
    stepPage_            = juce::jlimit(0, stepPageCount() - 1, stepPage_);
    const int    base    = stepPage_ * 16;   // 1er pas de la fenêtre de page (touches)
    const bool   playing = (proc_.engine().state() == SequencerEngine::State::PLAYING);
    const uint8_t rawStep = proc_.engine().currentStepForRow(static_cast<uint8_t>(sr));
    const bool   sameBar = (editBar_ == static_cast<int>(proc_.engine().currentBar()));
    // Playhead relatif à la fenêtre de page de 16 touches ; visible seulement si on édite
    // la mesure jouée (cohérent avec la grille).
    const int    livePh  = (playing && sameBar && rawStep != 0xFF && rawStep >= base && rawStep < base + 16
                            && rawStep < rowN) ? static_cast<int>(rawStep - base) : -1;

    auto clearWhites = [this] {
        for (int i = 0; i < 16; ++i)
            piano_.whiteKey(i).setToggleState(false, juce::dontSendNotification);
    };
    piano_.setBlackKeyHighlight(-1);

    switch (screenPage_) {
        case PatternScreenModel::Page::Pattern:
            // Mode Pas = on/off ; mode Accent/Swing = flag du pas (LED des blanches reflète le mode).
            for (int i = 0; i < 16; ++i) {
                const int s = base + i;
                bool lit = false;
                if (s < rowN) {
                    const auto& sd = row.step(static_cast<uint8_t>(editBar_), static_cast<uint8_t>(s));
                    lit = (padMode_ == PadMode::Accent) ? sd.accent
                        : (padMode_ == PadMode::Swing)  ? sd.swingEnable
                                                        : sd.enabled;
                }
                piano_.whiteKey(i).setToggleState(lit, juce::dontSendNotification);
            }
            piano_.setPlayheadStep(livePh);
            break;

        case PatternScreenModel::Page::Auto: {
            const int slot = juce::jlimit(0, 7, autoSlot_);
            for (int i = 0; i < 16; ++i) {
                const int s = base + i;
                piano_.whiteKey(i).setToggleState(
                    s < rowN && row.step(static_cast<uint8_t>(editBar_), static_cast<uint8_t>(s)).ccLocks[static_cast<size_t>(slot)].ccNumber != 0xFF,
                    juce::dontSendNotification);
            }
            piano_.setPlayheadStep(livePh);
            break;
        }

        case PatternScreenModel::Page::PianoRoll: {
            clearWhites();
            const int ss   = juce::jlimit(0, rowN - 1, selectedStep_);
            const int note = row.step(static_cast<uint8_t>(editBar_), static_cast<uint8_t>(ss)).note;
            // Indique la note sélectionnée sur le clavier : blanche OU noire selon sa hauteur.
            // (En Shift, les noires sont des fonctions → pas d'indication de note dessus.)
            int hlW = -1;
            for (int i = 0; i < 16; ++i)
                if (rollWhiteKeyMidi(i) == note) { hlW = i; break; }
            piano_.setPlayheadStep(hlW);
            int hlB = -1;
            if (!shiftActive())
                for (int i = 0; i < 11; ++i)
                    if (rollBlackKeyMidi(i) == note) { hlB = i; break; }
            piano_.setBlackKeyHighlight(hlB);
            break;
        }

        case PatternScreenModel::Page::Harmony: {
            clearWhites();
            if (shiftActive()) {
                // ⇧ : surligne les blanches des rows LIÉES (mode != Chromatic) ; pas de degré.
                const int nr = juce::jlimit(0, 16, static_cast<int>(pat.numRows));
                for (int i = 0; i < nr && i < 16; ++i)
                    piano_.whiteKey(i).setToggleState(
                        pat.rows[static_cast<size_t>(i)].harmonyModeAt(static_cast<uint8_t>(editBar_)) != RowHarmonyMode::Chromatic,
                        juce::dontSendNotification);
                piano_.setPlayheadStep(-1);
            } else {
                int deg = -1;
                if (harmonyCursor_ < static_cast<int>(pat.chordProgression.len))
                    deg = pat.chordProgression.slots[static_cast<size_t>(juce::jlimit(0, 31, harmonyCursor_))].degree;
                piano_.setPlayheadStep((deg >= 1 && deg <= 7) ? deg - 1 : -1);
            }
            break;
        }

        case PatternScreenModel::Page::Global:
        case PatternScreenModel::Page::Song:
        default:
            clearWhites();
            piano_.setPlayheadStep(-1);
            break;
    }
}

void NidmiSeqAudioProcessorEditor::updateKeysForPage() {
    if (inSub_) {   // édition d'un sub : noire 0 = Back, noire 9 = Mode (abs/rel) ; noire 1 libérée
        if (screenPage_ == PatternScreenModel::Page::PianoRoll)
            for (int i = 0; i < 16; ++i) piano_.setWhiteKeyLabel(i, noteNameFromMidi(rollWhiteKeyMidi(i)));
        else
            for (int i = 0; i < 16; ++i) piano_.setWhiteKeyLabel(i, {});
        piano_.setBlackKeyLabel(0, "Back");
        for (int i = 1; i < 11; ++i) piano_.setBlackKeyLabel(i, {});
        piano_.setBlackKeyLabel(9, "Mode");
        return;
    }
    switch (screenPage_) {
        case PatternScreenModel::Page::PianoRoll:
            for (int i = 0; i < 16; ++i) piano_.setWhiteKeyLabel(i, noteNameFromMidi(rollWhiteKeyMidi(i)));
            if (shiftActive()) {
                // ⇧ : noires = fonctions (R±/Page±/Sub/Mes±/Oct±).
                piano_.setBlackKeyLabel(0, "R-");
                piano_.setBlackKeyLabel(1, "R+");
                piano_.setBlackKeyLabel(2, "Pg-");
                piano_.setBlackKeyLabel(3, "Pg+");
                piano_.setBlackKeyLabel(4, "Sub");
                piano_.setBlackKeyLabel(5, "Mes-");
                piano_.setBlackKeyLabel(6, "Mes+");
                piano_.setBlackKeyLabel(7, "Oct-");
                piano_.setBlackKeyLabel(8, "Oct+");
                piano_.setBlackKeyLabel(9, (selectedStepSubIdx() >= 0) ? "Mode" : juce::String());  // rel/abs sub
                piano_.setBlackKeyLabel(10, {});
            } else {
                // Shift OFF : noires = notes chromatiques (mêmes que le clavier).
                for (int i = 0; i < 11; ++i) piano_.setBlackKeyLabel(i, noteNameFromMidi(rollBlackKeyMidi(i)));
            }
            break;
        case PatternScreenModel::Page::Harmony: {
            static const char* kR[7] = {"I", "II", "III", "IV", "V", "VI", "VII"};
            static const char* kTri[6] = {"maj", "m", "dim", "aug", "sus2", "sus4"};   // blanches 7..12
            // Noires 0..10 = 11 extensions (toggle bitfield), même ordre que kBlackExtBit dans onBlackKey.
            // Noire 0 = 7e mineure (+10, donne les accords « 7 ») ; noire 1 = 7e majeure (+11, « M7 »).
            static const char* kExtLbl[11] = {"7", "M7", "b5", "#5", "9", "11", "13",
                                              "b9", "#9", "#11", "b13"};
            static const char* kMode[4] = {"A", "B1", "B2", "CHR"};
            if (shiftActive()) {
                // ⇧ : les blanches 1..numRows = rows à cycler. Libellé "Rn:mode" (CHR = délié).
                const auto& pat = proc_.engine().pattern();
                const int   nr  = juce::jlimit(0, 16, static_cast<int>(pat.numRows));
                for (int i = 0; i < 16; ++i) {
                    if (i < nr) {
                        const int m = juce::jlimit(0, 3, static_cast<int>(pat.rows[static_cast<size_t>(i)].harmonyModeAt(static_cast<uint8_t>(editBar_))));
                        piano_.setWhiteKeyLabel(i, "R" + juce::String(i + 1) + ":" + kMode[m]);
                    } else {
                        piano_.setWhiteKeyLabel(i, {});   // au-delà de numRows : neutre
                    }
                }
            } else {
                for (int i = 0; i < 7; ++i)  piano_.setWhiteKeyLabel(i, kR[i]);        // 0..6 = degrés
                for (int i = 7; i < 13; ++i) piano_.setWhiteKeyLabel(i, kTri[i - 7]);  // 7..12 = triades
                // 13/14 = nav de mesure (si multi-mesures), 15 = vide.
                const bool multi = static_cast<int>(proc_.engine().pattern().numBars) > 1;
                piano_.setWhiteKeyLabel(13, multi ? juce::String("Mes-") : juce::String());
                piano_.setWhiteKeyLabel(14, multi ? juce::String("Mes+") : juce::String());
                piano_.setWhiteKeyLabel(15, {});
            }
            for (int i = 0; i < 11; ++i) piano_.setBlackKeyLabel(i, kExtLbl[i]);
            // (la LED d'extension active est gérée par frame dans refreshPianoKeysFromEngine.)
            break;
        }
        case PatternScreenModel::Page::Auto:
            for (int i = 0; i < 16; ++i) piano_.setWhiteKeyLabel(i, {});
            piano_.setBlackKeyLabel(0, "Slt-");
            piano_.setBlackKeyLabel(1, "Slt+");
            piano_.setBlackKeyLabel(2, "Pg-");
            piano_.setBlackKeyLabel(3, "Pg+");
            for (int i = 4; i < 11; ++i) piano_.setBlackKeyLabel(i, {});
            break;
        case PatternScreenModel::Page::Pattern: {
            for (int i = 0; i < 16; ++i) piano_.setWhiteKeyLabel(i, {});
            // Noires = fonctions directes (Mes a maintenant ses propres touches 5/6 → plus de ⇧Page=Mes).
            piano_.setBlackKeyLabel(0, "R-");
            piano_.setBlackKeyLabel(1, "R+");
            // ⇧+2 = mode Accent, ⇧+3 = mode Swing (façon Elektron) ; sinon Pg-/Pg+.
            piano_.setBlackKeyLabel(2, shiftActive() ? (padMode_ == PadMode::Accent ? "Acc*" : "Acc") : "Pg-");
            piano_.setBlackKeyLabel(3, shiftActive() ? (padMode_ == PadMode::Swing  ? "Swg*" : "Swg") : "Pg+");
            piano_.setBlackKeyLabel(4, "Sub");
            piano_.setBlackKeyLabel(5, "Mes-");
            piano_.setBlackKeyLabel(6, "Mes+");
            // Noires 7/8/10 = presse-papier. 7 = Copy (⇧ = Cut), 8 = Paste, 10 = Clear.
            // Grain par re-tap (Pas→Row→Mesure) : le label de Paste reflète le grain mémorisé.
            piano_.setBlackKeyLabel(7, shiftActive() ? "Cut" : "Copy");
            piano_.setBlackKeyLabel(8, !stepClip_.valid ? juce::String("Paste")
                                        : stepClip_.scope == ClipScope::Mesure ? juce::String("PstMes")
                                        : stepClip_.scope == ClipScope::Row    ? juce::String("PstRow")
                                                                               : juce::String("Paste"));
            piano_.setBlackKeyLabel(9, (selectedStepSubIdx() >= 0) ? "Mode" : juce::String());  // rel/abs sub
            piano_.setBlackKeyLabel(10, "Clear");
            break;
        }
        case PatternScreenModel::Page::Song:
            for (int i = 0; i < 16; ++i) piano_.setWhiteKeyLabel(i, {});
            piano_.setBlackKeyLabel(0, "Slt-");
            piano_.setBlackKeyLabel(1, "Slt+");
            piano_.setBlackKeyLabel(2, "+");
            piano_.setBlackKeyLabel(3, "Suppr");
            piano_.setBlackKeyLabel(4, proc_.engine().songMode() ? "Song*" : "Song");
            for (int i = 5; i < 11; ++i) piano_.setBlackKeyLabel(i, {});
            break;
        case PatternScreenModel::Page::Global:
        default:
            for (int i = 0; i < 16; ++i) piano_.setWhiteKeyLabel(i, {});
            piano_.setBlackKeyLabel(0, "R-");
            piano_.setBlackKeyLabel(1, "R+");
            for (int i = 2; i < 11; ++i) piano_.setBlackKeyLabel(i, {});
            break;
    }
}
