#include "PluginEditor.h"
#include "DeviceProfile.h"
#include "PluginProcessor.h"

#include "EditorHelpers.h"
#include "MidiExporter.h"

#include <nidmi_seq/ScaleBank.h>
#include <nidmi_seq/HarmonyEngine.h>

#include <cmath>

void NidmiSeqAudioProcessorEditor::syncValueEncoderFromParam() {
    // Defauts, que les branches ci-dessous peuvent surcharger.
    valueEncoderLabel_.setText("Valeur", juce::dontSendNotification);

    // La sensibilite globale (180 px pour toute la course) suppose une plage
    // fine. Sur une entree a peu de crans elle rend l'encodeur inutilisable :
    // avec 2 profils il fallait tirer 180 px pour en changer, contre 12 px par
    // cran sur « Rangees ». On tire 30 px par cran, borne a la valeur globale.
    const auto scaleDrag = [this](int steps) {
        valueEncoder_.setMouseDragSensitivity(juce::jlimit(60, 180, steps * 30));
    };
    scaleDrag(6);   // neutre : 180 px, comportement d'origine

    // Entrée virtuelle « Canal » (après les params OLED) : canal MIDI de la row sélectionnée (1..16).
    if (oledParamIndex_ == kNumOledParams) {
        const auto& pat = proc_.engine().pattern();
        const int   sr  = (pat.numRows > 0) ? juce::jlimit(0, static_cast<int>(pat.numRows) - 1, selectedRow_) : 0;
        const int   ch  = (pat.numRows > 0) ? static_cast<int>(pat.rows[static_cast<size_t>(sr)].channel) + 1 : 1;
        valueEncoder_.setRange(1.0, 16.0, 1.0);
        valueEncoder_.setValue(static_cast<double>(juce::jlimit(1, 16, ch)), juce::dontSendNotification);
        return;
    }
    // Entrée virtuelle « Pattern » : pattern actif de la banque (1..kMaxPatterns).
    if (oledParamIndex_ == kNumOledParams + 1) {
        const int ap = static_cast<int>(proc_.engine().activePatternIndex()) + 1;
        valueEncoder_.setRange(1.0, static_cast<double>(kMaxPatterns), 1.0);
        valueEncoder_.setValue(static_cast<double>(juce::jlimit(1, static_cast<int>(kMaxPatterns), ap)),
                               juce::dontSendNotification);
        return;
    }
    // Entrée virtuelle « Mesures » : nombre de mesures du pattern (1..kMaxBars).
    if (oledParamIndex_ == kNumOledParams + 2) {
        const int nb = static_cast<int>(proc_.engine().patternNumBars());
        valueEncoder_.setRange(1.0, static_cast<double>(kMaxBars), 1.0);
        valueEncoder_.setValue(static_cast<double>(juce::jlimit(1, static_cast<int>(kMaxBars), nb)),
                               juce::dontSendNotification);
        return;
    }
    // Entree virtuelle « Profil » : profil d'appareil (nommage des CC).
    if (oledParamIndex_ == kNumOledParams + 3) {
        const int n   = juce::jmax(1, DeviceProfile::count());
        const int idx = juce::jlimit(0, n - 1, proc_.deviceProfileIndex());
        valueEncoder_.setRange(0.0, static_cast<double>(n - 1), 1.0);
        valueEncoder_.setValue(static_cast<double>(idx), juce::dontSendNotification);
        scaleDrag(n - 1);
        // Le nom sur l'encodeur : sans lui on ne voit pas ce qu'on selectionne,
        // la page GLOBAL etant derriere le panneau des encodeurs.
        valueEncoderLabel_.setText(DeviceProfile::byIndex(idx).name(), juce::dontSendNotification);
        return;
    }
    // Lignes d'action : rien a tourner, tout se joue au push. On neutralise
    // l'encodeur plutot que de le laisser sur la valeur de la ligne precedente.
    if (oledParamIndex_ == kNumOledParams + 4 || oledParamIndex_ == kNumOledParams + 5) {
        valueEncoder_.setRange(0.0, 1.0, 1.0);
        valueEncoder_.setValue(0.0, juce::dontSendNotification);
        valueEncoderLabel_.setText("Push", juce::dontSendNotification);
        return;
    }
    auto&       ap    = proc_.apvts();
    const char* id    = oledParamId(oledParamIndex_);
    auto*       param = ap.getParameter(id);
    if (param == nullptr)
        return;

    if (auto* pf = dynamic_cast<juce::AudioParameterFloat*>(param)) {
        const float v = ap.getRawParameterValue(id) != nullptr ? ap.getRawParameterValue(id)->load()
                                                                 : 120.0f;
        valueEncoder_.setRange(static_cast<double>(pf->range.start),
                               static_cast<double>(pf->range.end),
                               static_cast<double>(pf->range.interval));
        valueEncoder_.setValue(static_cast<double>(v), juce::dontSendNotification);
    } else if (auto* pi = dynamic_cast<juce::AudioParameterInt*>(param)) {
        const int v =
            ap.getRawParameterValue(id) != nullptr ? (int) std::lround(ap.getRawParameterValue(id)->load())
                                                    : 0;
        valueEncoder_.setRange(static_cast<double>(pi->getRange().getStart()),
                               static_cast<double>(pi->getRange().getEnd()), 1.0);
        valueEncoder_.setValue(static_cast<double>(v), juce::dontSendNotification);
    } else if (dynamic_cast<juce::AudioParameterBool*>(param) != nullptr) {
        const bool on = ap.getRawParameterValue(id) != nullptr && ap.getRawParameterValue(id)->load() > 0.5f;
        valueEncoder_.setRange(0.0, 1.0, 1.0);
        valueEncoder_.setValue(on ? 1.0 : 0.0, juce::dontSendNotification);
    } else if (auto* ch = dynamic_cast<juce::AudioParameterChoice*>(param)) {
        const int idx = ch->getIndex();
        valueEncoder_.setRange(0.0, static_cast<double>(ch->choices.size() - 1), 1.0);
        valueEncoder_.setValue(static_cast<double>(idx), juce::dontSendNotification);
    }
}

void NidmiSeqAudioProcessorEditor::applyValueEncoderToParam() {
    // Entrée virtuelle « Canal » : pose le canal MIDI (1..16) de la row sélectionnée via SetRowChannel.
    if (oledParamIndex_ == kNumOledParams) {
        const auto& pat = proc_.engine().pattern();
        if (pat.numRows == 0) return;
        const int sr = juce::jlimit(0, static_cast<int>(pat.numRows) - 1, selectedRow_);
        const int ch = juce::jlimit(1, 16, (int) std::lround(valueEncoder_.getValue()));
        SequencerCommand c;
        c.id = SequencerCommandId::SetRowChannel;
        c.a  = static_cast<uint8_t>(sr);
        c.b  = static_cast<uint8_t>(ch - 1);   // engine stocke 0..15
        proc_.controller().postCommand(c);
        buildScreenModel();
        return;
    }
    // Entrée virtuelle « Pattern » : bascule le pattern actif de la banque.
    if (oledParamIndex_ == kNumOledParams + 1) {
        const int idx = juce::jlimit(1, static_cast<int>(kMaxPatterns),
                                     (int) std::lround(valueEncoder_.getValue())) - 1;
        SequencerCommand c;
        c.id = SequencerCommandId::SelectPattern;
        c.a  = static_cast<uint8_t>(idx);
        proc_.controller().postCommand(c);
        // Le contenu de l'éditeur (PATTERN/ROLL/HARMONIE) suit le pattern actif :
        // on remet le curseur dans les bornes et on rafraîchit.
        selectedStep_ = 0;
        buildScreenModel();
        return;
    }
    // Entrée virtuelle « Mesures » : nombre de mesures du pattern (1..kMaxBars).
    if (oledParamIndex_ == kNumOledParams + 2) {
        const int nb = juce::jlimit(1, static_cast<int>(kMaxBars),
                                    (int) std::lround(valueEncoder_.getValue()));
        SequencerCommand c;
        c.id = SequencerCommandId::SetPatternNumBars;
        c.a  = static_cast<uint8_t>(nb);
        proc_.controller().postCommand(c);
        editBar_ = juce::jlimit(0, nb - 1, editBar_);
        buildScreenModel();
        return;
    }
    // Lignes d'action : la rotation est inerte, seul le push agit.
    if (oledParamIndex_ == kNumOledParams + 4 || oledParamIndex_ == kNumOledParams + 5)
        return;
    // Entrée virtuelle « Profil » : purement cosmetique, aucun passage par le
    // CommandFifo puisque le moteur n'est pas touche.
    if (oledParamIndex_ == kNumOledParams + 3) {
        proc_.setDeviceProfileIndex(juce::jlimit(0, DeviceProfile::count() - 1,
                                                 (int) std::lround(valueEncoder_.getValue())));
        buildScreenModel();
        return;
    }
    auto&       ap    = proc_.apvts();
    const char* id    = oledParamId(oledParamIndex_);
    auto*       param = ap.getParameter(id);
    auto*       rp    = dynamic_cast<juce::RangedAudioParameter*>(param);
    if (rp == nullptr)
        return;

    const float norm = rp->convertTo0to1(static_cast<float>(valueEncoder_.getValue()));
    rp->setValueNotifyingHost(norm);
}

void NidmiSeqAudioProcessorEditor::onNavEncoderChanged() {
    encSyncCooldown_ = 8;   // laisse la FIFO draîner avant que le timer re-synchronise
    if (inSub_) {   // Enc2 = curseur de sous-pas
        const int subIdx = activeSubIdx();
        const int sn = (subIdx >= 0)
            ? juce::jlimit(1, 16, static_cast<int>(proc_.engine().pattern().subPatterns[static_cast<size_t>(subIdx)].numSteps))
            : 1;
        subStep_ = juce::jlimit(0, sn - 1, (int) std::lround(navEncoder_.getValue()));
        buildScreenModel();
        return;
    }
    if (screenPage_ == PatternScreenModel::Page::Global) {
        // kNumOledParams params APVTS + Canal (kNumOledParams) + Pattern (+1) + Mesures (+2).
        const int ni = juce::jlimit(0, kNumOledParams + 5, (int) std::lround(navEncoder_.getValue()));
        if (ni != oledParamIndex_) {
            oledParamIndex_ = ni;
            syncValueEncoderFromParam();
        }
    } else if (screenPage_ == PatternScreenModel::Page::Pattern) {
        // PATTERN : Enc2 (Curseur) = pas (cohérent avec ROLL/AUTO). La row est sur Enc4.
        const auto& pat = proc_.engine().pattern();
        const int   nr  = juce::jmax(1, static_cast<int>(pat.numRows));
        const int   sr  = juce::jlimit(0, nr - 1, selectedRow_);
        const int   n   = juce::jlimit(1, 64, static_cast<int>(pat.rows[static_cast<size_t>(sr)].numSteps));
        selectedStep_   = juce::jlimit(0, n - 1, (int) std::lround(navEncoder_.getValue()));
    } else if (screenPage_ == PatternScreenModel::Page::PianoRoll) {
        // PIANO ROLL : l'encodeur curseur déplace le pas.
        const auto& pat = proc_.engine().pattern();
        const int   nr  = juce::jmax(1, static_cast<int>(pat.numRows));
        const int   sr  = juce::jlimit(0, nr - 1, selectedRow_);
        const int   n   = juce::jlimit(1, 64, static_cast<int>(pat.rows[static_cast<size_t>(sr)].numSteps));
        const int   ss  = juce::jlimit(0, n - 1, (int) std::lround(navEncoder_.getValue()));
        if (ss != selectedStep_) {
            selectedStep_ = ss;
            applyEncoderConfigForState();  // recharge Enc1 selon le champ actif (Note/Vélo/Gate)
        }
    } else if (screenPage_ == PatternScreenModel::Page::Harmony) {
        if (harmonyFocus_ == HarmonyFocus::Tonality) {
            // Focus TONALITÉ : le curseur navigue les marqueurs (jusqu'à len = marqueur d'ajout).
            const int klen = juce::jlimit(0, 16, static_cast<int>(proc_.engine().pattern().keyProgression.len));
            const int kc   = juce::jlimit(0, juce::jmin(15, klen), (int) std::lround(navEncoder_.getValue()));
            if (kc != keyCursor_) { keyCursor_ = kc; applyEncoderConfigForState(); }
        } else {
            // Focus ACCORDS : Enc curseur = Slot (jusqu'à len = slot d'ajout).
            const int len = juce::jlimit(0, 32, static_cast<int>(proc_.engine().pattern().chordProgression.len));
            const int cur = juce::jlimit(0, juce::jmin(31, len), (int) std::lround(navEncoder_.getValue()));
            if (cur != harmonyCursor_) {
                harmonyCursor_ = cur;
                applyEncoderConfigForState();  // recharge Enc1 avec le champ du nouveau slot
            }
        }
    } else if (screenPage_ == PatternScreenModel::Page::Auto) {
        const auto& pat = proc_.engine().pattern();
        const int   nr  = juce::jmax(1, static_cast<int>(pat.numRows));
        const int   ar  = juce::jlimit(0, nr - 1, selectedRow_);
        const int   an  = juce::jlimit(1, 64, static_cast<int>(pat.rows[static_cast<size_t>(ar)].numSteps));
        if (autoField_ == 0) {
            const int ss = juce::jlimit(0, an - 1, (int) std::lround(navEncoder_.getValue()));
            if (ss != selectedStep_) { selectedStep_ = ss; applyEncoderConfigForState(); }
        } else {
            const int sl = juce::jlimit(0, 7, (int) std::lround(navEncoder_.getValue()));
            if (sl != autoSlot_) { autoSlot_ = sl; applyEncoderConfigForState(); }
        }
    } else if (screenPage_ == PatternScreenModel::Page::Song) {
        // Enc2 = curseur de slot de chaîne (0..len ; len = ligne « + » d'ajout).
        const int len = static_cast<int>(proc_.engine().chainProgram().len);
        const int cur = juce::jlimit(0, len, (int) std::lround(navEncoder_.getValue()));
        if (cur != songCursor_) { songCursor_ = cur; applyEncoderConfigForState(); }
    } else {
        return;  // onglets en ossature : encodeurs inertes pour l'instant.
    }
    buildScreenModel();
}

void NidmiSeqAudioProcessorEditor::onValueEncoderChanged() {
    encSyncCooldown_ = 8;
    if (inSub_) {
        const int subIdx = activeSubIdx();
        if (subIdx < 0) return;
        if (screenPage_ == PatternScreenModel::Page::PianoRoll && subRollDivN_) {
            // Bascule →N : la rotation règle le nombre de divisions du sous-pattern.
            const int n = juce::jlimit(1, 16, (int) std::lround(valueEncoder_.getValue()));
            SequencerCommand c;
            c.id = SequencerCommandId::SetSubPatternSteps;
            c.a  = static_cast<uint8_t>(subIdx);
            c.b  = static_cast<uint8_t>(n);
            proc_.controller().postCommand(c);
            buildScreenModel();
        } else if (screenPage_ == PatternScreenModel::Page::PianoRoll) {
            const auto& sp = proc_.engine().pattern().subPatterns[static_cast<size_t>(subIdx)];
            const int   sn = juce::jlimit(1, 16, static_cast<int>(sp.numSteps));
            const int   ss = juce::jlimit(0, sn - 1, subStep_);
            const int   v  = (int) std::lround(valueEncoder_.getValue());
            // Relatif : Enc1 = intervalle → on convertit en hauteur absolue pour postSubStepPitch.
            postSubStepPitch(ss, sp.relativeToHost ? (subHostNote() + v) : v);
        } else {
            // Enc1 = N du sub (tuplet imbriqué).
            const int n = juce::jlimit(1, 16, (int) std::lround(valueEncoder_.getValue()));
            SequencerCommand c;
            c.id = SequencerCommandId::SetSubPatternSteps;
            c.a  = static_cast<uint8_t>(subIdx);
            c.b  = static_cast<uint8_t>(n);
            proc_.controller().postCommand(c);
            buildScreenModel();
        }
        return;
    }
    if (screenPage_ == PatternScreenModel::Page::Global) {
        applyValueEncoderToParam();
        return;
    }
    if (screenPage_ == PatternScreenModel::Page::Pattern) {
        const auto& pat = proc_.engine().pattern();
        if (selectedRow_ < 0 || selectedRow_ >= static_cast<int>(pat.numRows))
            return;
        const int rn = juce::jlimit(1, 64, static_cast<int>(pat.rows[static_cast<size_t>(selectedRow_)].numSteps));
        if (patternValSpan_) {
            // Push →Span = span : nb de pas hôtes couverts par le pas sélectionné (note longue / sub étalé).
            // Clampé à 1..(N - pas) : le span ne déborde pas la mesure de la row.
            const int ss   = juce::jlimit(0, rn - 1, selectedStep_);
            const int maxS = juce::jmax(1, rn - ss);
            const int sp   = juce::jlimit(1, maxS, (int) std::lround(valueEncoder_.getValue()));
            SequencerCommand c;
            c.id = SequencerCommandId::SetStepSpan;
            c.a  = static_cast<uint8_t>(selectedRow_);
            c.b  = static_cast<uint8_t>(ss);
            c.c  = static_cast<uint8_t>(sp);
            c.f  = static_cast<uint8_t>(editBar_);
            proc_.controller().postCommand(c);
            buildScreenModel();
            return;
        }
        // Enc1 (sans Shift) = N (tuplet) de la row sélectionnée → re-subdivision live.
        const int n = juce::jlimit(1, 64, (int) std::lround(valueEncoder_.getValue()));
        if (n == rn)
            return;
        SequencerCommand c;
        c.id = SequencerCommandId::SetRowSteps;
        c.a  = static_cast<uint8_t>(selectedRow_);
        c.b  = static_cast<uint8_t>(n);
        proc_.controller().postCommand(c);
        buildScreenModel();
        return;
    }
    if (screenPage_ == PatternScreenModel::Page::PianoRoll) {
        // PIANO ROLL : Enc2 = Note du pas sous le curseur (vélo = Enc3, gate = push →Gate).
        const auto& pat = proc_.engine().pattern();
        const int   nr  = juce::jmax(1, static_cast<int>(pat.numRows));
        const int   sr  = juce::jlimit(0, nr - 1, selectedRow_);
        const int   n   = juce::jlimit(1, 64, static_cast<int>(pat.rows[static_cast<size_t>(sr)].numSteps));
        const int   ss  = juce::jlimit(0, n - 1, selectedStep_);
        if (patternValSpan_) {
            // Push →Span = span du pas sélectionné (note longue / sub étalé), comme PATTERN.
            const int maxS = juce::jmax(1, n - ss);
            const int sp   = juce::jlimit(1, maxS, (int) std::lround(valueEncoder_.getValue()));
            SequencerCommand c;
            c.id = SequencerCommandId::SetStepSpan;
            c.a  = static_cast<uint8_t>(sr);
            c.b  = static_cast<uint8_t>(ss);
            c.c  = static_cast<uint8_t>(sp);
            c.f  = static_cast<uint8_t>(editBar_);
            proc_.controller().postCommand(c);
            buildScreenModel();
            return;
        }
        setStepField(ss, 0, (int) std::lround(valueEncoder_.getValue()));
        return;
    }
    if (screenPage_ == PatternScreenModel::Page::Harmony) {
        if (harmonyFocus_ == HarmonyFocus::Tonality)
            setKeyField(0, (int) std::lround(valueEncoder_.getValue()));   // Tonique du marqueur
        else
            setChordField(harmValBass_ ? 3 : 0, (int) std::lround(valueEncoder_.getValue()));
        return;
    }
    if (screenPage_ == PatternScreenModel::Page::Auto) {
        const int v = (int) std::lround(valueEncoder_.getValue());
        if (autoField_ == 0)
            postAutoValueAt(selectedStep_, v);
        else
            postAutoCcNumber(v);
        return;
    }
    if (screenPage_ == PatternScreenModel::Page::Song) {
        // Enc1 = type d'instruction (op) du slot sous le curseur.
        songSetSlotOp((int) std::lround(valueEncoder_.getValue()));
        applyEncoderConfigForState();   // P1/P2 dépendent de l'op → recharge Enc3/Enc4
        return;
    }
    // onglets en ossature : pas d'édition via Enc1 pour l'instant.
}

void NidmiSeqAudioProcessorEditor::applyEncoderConfigForState() {
    if (inSub_) {   // Enc2 = sous-pas ; Enc1 = N (PATTERN) ou hauteur (ROLL)
        const int   subIdx = activeSubIdx();
        const auto& sp = proc_.engine().pattern().subPatterns[static_cast<size_t>(juce::jmax(0, subIdx))];
        const int   sn = (subIdx >= 0) ? juce::jlimit(1, 16, static_cast<int>(sp.numSteps)) : 1;
        const int   ss = juce::jlimit(0, sn - 1, subStep_);
        navEncoderLabel_.setText("Sous-pas " + juce::String(ss + 1), juce::dontSendNotification);
        navEncoder_.setRange(0.0, static_cast<double>(juce::jmax(1, sn - 1)), 1.0);
        navEncoder_.setValue(static_cast<double>(ss), juce::dontSendNotification);
        if (screenPage_ == PatternScreenModel::Page::PianoRoll && subIdx >= 0 && subRollDivN_) {
            // HAUT-GAUCHE bascule sur N : nombre de divisions du sous-pattern (1..16).
            valueEncoderLabel_.setText("Sub N " + juce::String(sn), juce::dontSendNotification);
            valueEncoder_.setRange(1.0, 16.0, 1.0);
            valueEncoder_.setValue(static_cast<double>(sn), juce::dontSendNotification);
        } else if (screenPage_ == PatternScreenModel::Page::PianoRoll && subIdx >= 0) {
            const auto& sd = sp.steps[static_cast<size_t>(ss)];
            if (sp.relativeToHost) {
                // Intervalle (offset centré sur 64) → stable quand l'ancre bouge.
                const int off = static_cast<int>(sd.note) - 64;
                valueEncoderLabel_.setText(juce::String("Inter ") + (off >= 0 ? "+" : "") + juce::String(off),
                                           juce::dontSendNotification);
                valueEncoder_.setRange(-24.0, 24.0, 1.0);
                valueEncoder_.setValue(static_cast<double>(off), juce::dontSendNotification);
            } else {
                valueEncoderLabel_.setText("Note " + noteNameFromMidi(sd.note), juce::dontSendNotification);
                valueEncoder_.setRange(0.0, 127.0, 1.0);
                valueEncoder_.setValue(static_cast<double>(sd.note), juce::dontSendNotification);
            }
        } else {
            valueEncoderLabel_.setText("Sub N " + juce::String(sn), juce::dontSendNotification);
            valueEncoder_.setRange(1.0, 16.0, 1.0);
            valueEncoder_.setValue(static_cast<double>(sn), juce::dontSendNotification);
        }
        return;
    }
    if (screenPage_ == PatternScreenModel::Page::Global) {
        navEncoderLabel_.setText("Param", juce::dontSendNotification);
        valueEncoderLabel_.setText("Valeur", juce::dontSendNotification);
        navEncoder_.setRange(0.0, static_cast<double>(kNumOledParams + 5), 1.0);   // +Canal +Pattern +Mesures +Profil +2 resets
        navEncoder_.setValue(static_cast<double>(oledParamIndex_), juce::dontSendNotification);
        syncValueEncoderFromParam();
    } else if (screenPage_ == PatternScreenModel::Page::Pattern) {
        const auto& pat = proc_.engine().pattern();
        const int   nr  = juce::jmax(1, static_cast<int>(pat.numRows));
        const int   sr  = juce::jlimit(0, nr - 1, selectedRow_);
        const int   n   = juce::jlimit(1, 64, static_cast<int>(pat.rows[static_cast<size_t>(sr)].numSteps));
        const juce::String div = divisionLabel(n, pat.numerator, pat.denominator);
        const int ss = juce::jlimit(0, n - 1, selectedStep_);
        navEncoderLabel_.setText("Pas " + juce::String(ss + 1), juce::dontSendNotification);  // Enc2 = Pas
        navEncoder_.setRange(0.0, static_cast<double>(juce::jmax(1, n - 1)), 1.0);
        navEncoder_.setValue(static_cast<double>(ss), juce::dontSendNotification);
        if (patternValSpan_) {
            // Push →Span = span du pas sélectionné (note longue / sub étalé). Plage 1..(N - pas).
            const int maxS = juce::jmax(1, n - ss);
            const int sp   = juce::jlimit(1, maxS,
                static_cast<int>(pat.rows[static_cast<size_t>(sr)].step(static_cast<uint8_t>(editBar_),
                                                                       static_cast<uint8_t>(ss)).span));
            valueEncoderLabel_.setText("Span " + juce::String(sp) + " pas", juce::dontSendNotification);
            valueEncoder_.setRange(1.0, static_cast<double>(maxS), 1.0);
            valueEncoder_.setValue(static_cast<double>(sp), juce::dontSendNotification);
        } else {
            valueEncoderLabel_.setText("N " + juce::String(n) + (div.isEmpty() ? juce::String() : " " + div),
                                       juce::dontSendNotification);
            valueEncoder_.setRange(1.0, 64.0, 1.0);
            valueEncoder_.setValue(static_cast<double>(n), juce::dontSendNotification);
        }
    } else if (screenPage_ == PatternScreenModel::Page::PianoRoll) {
        const auto& pat = proc_.engine().pattern();
        const int   nr  = juce::jmax(1, static_cast<int>(pat.numRows));
        const int   sr  = juce::jlimit(0, nr - 1, selectedRow_);
        const int   n   = juce::jlimit(1, 64, static_cast<int>(pat.rows[static_cast<size_t>(sr)].numSteps));
        const int   ss  = juce::jlimit(0, n - 1, selectedStep_);
        const auto& sd  = pat.rows[static_cast<size_t>(sr)].step(static_cast<uint8_t>(editBar_), static_cast<uint8_t>(ss));
        navEncoderLabel_.setText("Pas " + juce::String(ss + 1), juce::dontSendNotification);
        navEncoder_.setRange(0.0, static_cast<double>(juce::jmax(1, n - 1)), 1.0);
        navEncoder_.setValue(static_cast<double>(ss), juce::dontSendNotification);
        if (patternValSpan_) {
            // Push →Span actif : Enc2 = span du pas (note longue / sub étalé).
            const int maxS = juce::jmax(1, n - ss);
            const int sp   = juce::jlimit(1, maxS, static_cast<int>(sd.span));
            valueEncoderLabel_.setText("Span " + juce::String(sp), juce::dontSendNotification);
            valueEncoder_.setRange(1.0, static_cast<double>(maxS), 1.0);
            valueEncoder_.setValue(static_cast<double>(sp), juce::dontSendNotification);
        } else {
            // Enc2 = Note (vélo sur Enc3, gate sur push →Gate).
            valueEncoderLabel_.setText("Note " + noteNameFromMidi(sd.note), juce::dontSendNotification);
            valueEncoder_.setRange(0.0, 127.0, 1.0);
            valueEncoder_.setValue(static_cast<double>(sd.note), juce::dontSendNotification);
        }
    } else if (screenPage_ == PatternScreenModel::Page::Harmony
               && harmonyFocus_ == HarmonyFocus::Tonality) {
        // Focus TONALITÉ : Enc1=Marqueur, Enc2=Tonique, Enc3=Durée(temps), Enc4=Gamme (relatif).
        static const char* kPc[12] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
        const auto& kp  = proc_.engine().pattern().keyProgression;
        const auto& ps  = proc_.engine().projectSettings();
        const int   klen = juce::jlimit(0, 16, static_cast<int>(kp.len));
        const int   kc   = juce::jlimit(0, juce::jmin(15, klen), keyCursor_);
        int root = static_cast<int>(ps.masterRootPc), scale = static_cast<int>(ps.masterScaleId);
        int dur  = juce::jmax(1, static_cast<int>(proc_.engine().pattern().numerator));
        if (kc < klen) {
            const auto& k = kp.slots[static_cast<size_t>(kc)];
            root = k.rootPc; scale = k.scaleId; dur = k.durationBeats;
        } else if (klen > 0) {
            const auto& k = kp.slots[static_cast<size_t>(klen - 1)];
            root = k.rootPc; scale = k.scaleId; dur = k.durationBeats;
        }
        navEncoderLabel_.setText("Marqueur " + juce::String(kc + 1), juce::dontSendNotification);
        navEncoder_.setRange(0.0, static_cast<double>(juce::jmax(1, klen)), 1.0);
        navEncoder_.setValue(static_cast<double>(kc), juce::dontSendNotification);
        valueEncoderLabel_.setText(juce::String("Tonique ") + kPc[juce::jlimit(0, 11, root)],
                                   juce::dontSendNotification);
        valueEncoder_.setRange(0.0, 11.0, 1.0);
        valueEncoder_.setValue(static_cast<double>(juce::jlimit(0, 11, root)), juce::dontSendNotification);
        veloEncoderLabel_.setText("Duree " + juce::String(dur) + " tps", juce::dontSendNotification);
        veloEncoder_.setRange(1.0, 64.0, 1.0);
        veloEncoder_.setValue(static_cast<double>(dur), juce::dontSendNotification);
        zoomEncoderLabel_.setText(juce::String("Gamme ") + scalebank::getScale(static_cast<uint8_t>(scale)).name,
                                  juce::dontSendNotification);
    } else if (screenPage_ == PatternScreenModel::Page::Harmony) {
        const auto& prog = proc_.engine().pattern().chordProgression;
        const int   len  = juce::jlimit(0, 32, static_cast<int>(prog.len));
        const int   cur  = juce::jlimit(0, juce::jmin(31, len), harmonyCursor_);  // == len = slot d'ajout
        int deg = 1, bass = 0;
        if (cur < len) {
            const auto& cs = prog.slots[static_cast<size_t>(cur)];
            deg = cs.degree; bass = cs.bassOffset;
        } else if (len > 0) {
            deg = prog.slots[static_cast<size_t>(len - 1)].degree;  // seed = degré du slot précédent
        }
        // Enc1 = Slot (curseur). Enc2 = Degré, ou Bass si push →Bass actif.
        // Le mode par row se cycle par ⇧+blanche N.
        navEncoderLabel_.setText("Slot " + juce::String(cur + 1), juce::dontSendNotification);
        navEncoder_.setRange(0.0, static_cast<double>(juce::jmax(1, len)), 1.0);
        navEncoder_.setValue(static_cast<double>(cur), juce::dontSendNotification);
        if (harmValBass_) {
            valueEncoderLabel_.setText("Bass " + juce::String(bass), juce::dontSendNotification);
            valueEncoder_.setRange(-12.0, 12.0, 1.0);
            valueEncoder_.setValue(static_cast<double>(bass), juce::dontSendNotification);
        } else {
            valueEncoderLabel_.setText(juce::String(juce::CharPointer_UTF8("Degr\xc3\xa9 ")) + juce::String(kRoman[juce::jlimit(0, 6, deg - 1)]),
                                       juce::dontSendNotification);
            valueEncoder_.setRange(1.0, 7.0, 1.0);
            valueEncoder_.setValue(static_cast<double>(deg), juce::dontSendNotification);
        }
    } else if (screenPage_ == PatternScreenModel::Page::Auto) {
        const auto& pat = proc_.engine().pattern();
        const int   nr  = juce::jmax(1, static_cast<int>(pat.numRows));
        const int   ar  = juce::jlimit(0, nr - 1, selectedRow_);
        const int   an  = juce::jlimit(1, 64, static_cast<int>(pat.rows[static_cast<size_t>(ar)].numSteps));
        if (autoField_ == 0) {
            const int   ss = juce::jlimit(0, an - 1, selectedStep_);
            const auto& lk = pat.rows[static_cast<size_t>(ar)].step(static_cast<uint8_t>(editBar_), static_cast<uint8_t>(ss))
                                 .ccLocks[static_cast<size_t>(juce::jlimit(0, 7, autoSlot_))];
            const int   v  = (lk.ccNumber != 0xFF) ? static_cast<int>(lk.value) : 0;
            navEncoderLabel_.setText("Pas " + juce::String(ss + 1), juce::dontSendNotification);
            valueEncoderLabel_.setText("Val " + juce::String(v), juce::dontSendNotification);
            navEncoder_.setRange(0.0, static_cast<double>(juce::jmax(1, an - 1)), 1.0);
            navEncoder_.setValue(static_cast<double>(ss), juce::dontSendNotification);
            valueEncoder_.setRange(0.0, 127.0, 1.0);
            valueEncoder_.setValue(static_cast<double>(v), juce::dontSendNotification);
        } else {
            navEncoderLabel_.setText("Slot " + juce::String(juce::jlimit(0, 7, autoSlot_) + 1), juce::dontSendNotification);
            valueEncoderLabel_.setText(
                DeviceProfile::byIndex(proc_.deviceProfileIndex()).label(effectiveAutoCc()),
                juce::dontSendNotification);
            navEncoder_.setRange(0.0, 7.0, 1.0);
            navEncoder_.setValue(static_cast<double>(juce::jlimit(0, 7, autoSlot_)), juce::dontSendNotification);
            valueEncoder_.setRange(0.0, 127.0, 1.0);
            valueEncoder_.setValue(static_cast<double>(effectiveAutoCc()), juce::dontSendNotification);
        }
    } else if (screenPage_ == PatternScreenModel::Page::Song) {
        static const char* kOpShort[12] = {
            "Play", "Repeat", "EndRep", "Segno", "D.S.", "D.S.alCoda",
            "Coda", "ToCoda", "D.C.", "D.C.alCoda", "Fine", "End"};
        const auto& chain = proc_.engine().chainProgram();
        const int   len   = static_cast<int>(chain.len);
        const int   cur   = juce::jlimit(0, len, songCursor_);
        // Enc2 = curseur de slot (0..len ; len = ligne d'ajout).
        navEncoderLabel_.setText((cur >= len) ? juce::String("Slot +")
                                              : ("Slot " + juce::String(cur + 1)),
                                 juce::dontSendNotification);
        navEncoder_.setRange(0.0, static_cast<double>(juce::jmax(1, len)), 1.0);
        navEncoder_.setValue(static_cast<double>(cur), juce::dontSendNotification);
        if (cur < len) {
            const auto& s   = chain.slots[static_cast<size_t>(cur)];
            const int   op  = juce::jlimit(0, 11, static_cast<int>(s.op));
            // Enc1 = type (op).
            valueEncoderLabel_.setText(juce::String("Type ") + kOpShort[op], juce::dontSendNotification);
            valueEncoder_.setRange(0.0, static_cast<double>(static_cast<int>(ChainOp::Count) - 1), 1.0);
            valueEncoder_.setValue(static_cast<double>(op), juce::dontSendNotification);
            // Enc3 = param1 (sens dépendant de l'op).
            if (op == static_cast<int>(ChainOp::PlayPattern)) {
                veloEncoderLabel_.setText("Pattern P" + juce::String(s.param1 + 1), juce::dontSendNotification);
                veloEncoder_.setRange(0.0, static_cast<double>(kMaxPatterns - 1), 1.0);
                veloEncoder_.setValue(static_cast<double>(juce::jlimit(0, kMaxPatterns - 1, static_cast<int>(s.param1))),
                                      juce::dontSendNotification);
                zoomEncoderLabel_.setText("Repet x" + juce::String(juce::jmax(1, static_cast<int>(s.param2))),
                                          juce::dontSendNotification);
            } else if (op == static_cast<int>(ChainOp::RepeatBegin)) {
                veloEncoderLabel_.setText("Count x" + juce::String(juce::jmax(1, static_cast<int>(s.param1))),
                                          juce::dontSendNotification);
                veloEncoder_.setRange(1.0, 16.0, 1.0);
                veloEncoder_.setValue(static_cast<double>(juce::jlimit(1, 16, static_cast<int>(s.param1))),
                                      juce::dontSendNotification);
                zoomEncoderLabel_.setText(juce::CharPointer_UTF8("\xe2\x80\x94"), juce::dontSendNotification);
            } else {
                veloEncoderLabel_.setText(juce::CharPointer_UTF8("\xe2\x80\x94"), juce::dontSendNotification);
                zoomEncoderLabel_.setText(juce::CharPointer_UTF8("\xe2\x80\x94"), juce::dontSendNotification);
            }
        } else {
            valueEncoderLabel_.setText("+ ajouter", juce::dontSendNotification);
            veloEncoderLabel_.setText(juce::CharPointer_UTF8("\xe2\x80\x94"), juce::dontSendNotification);
            zoomEncoderLabel_.setText(juce::CharPointer_UTF8("\xe2\x80\x94"), juce::dontSendNotification);
        }
    } else {
        // Onglets en ossature : encodeurs inertes, libellés neutres.
        navEncoderLabel_.setText(juce::CharPointer_UTF8("\xe2\x80\x94"), juce::dontSendNotification);   // —
        valueEncoderLabel_.setText(juce::CharPointer_UTF8("\xe2\x80\x94"), juce::dontSendNotification); // —
    }
}

void NidmiSeqAudioProcessorEditor::configurePushButtons() {
    // Pose label + état toggle/led des 4 boutons PUSH selon la Vue. Par défaut : cachés.
    // Pour ce lot, SEULE la Vue HARMONIE câble des fonctions de push (cf. onPushButton).
    auto hide = [this](int i) { pushBtn_[i].setVisible(false); };
    auto setToggle = [this](int i, const juce::String& txt, bool on) {
        pushBtn_[i].setVisible(true);
        pushBtn_[i].setButtonText(txt);
        pushBtn_[i].setToggleState(on, juce::dontSendNotification);
        if (on) pushBtn_[i].getProperties().set("led", true);
        else    pushBtn_[i].getProperties().remove("led");
        pushBtn_[i].repaint();
    };
    auto setAction = [this](int i, const juce::String& txt, bool enabled) {
        pushBtn_[i].setVisible(true);
        pushBtn_[i].setButtonText(txt);
        pushBtn_[i].setToggleState(false, juce::dontSendNotification);
        pushBtn_[i].getProperties().remove("led");   // action : pas d'état led persistant
        pushBtn_[i].setEnabled(enabled);
        pushBtn_[i].repaint();
    };

    // Flèche « → » réutilisée pour les toggles d'attribut secondaire (même encodage que HARMONIE).
    const juce::String arrow = juce::CharPointer_UTF8("\xe2\x86\x92");

    if (inSub_) {
        // Drill-in : push HAUT-DROITE = Sortir. En ROLL, push HAUT-GAUCHE = →N (bascule
        // Note <-> nombre de divisions du sous-pattern). En strip (PATTERN), N est en rotation directe.
        setAction(0, juce::String(juce::CharPointer_UTF8("\xe2\x86\x91")) + " Sortir", true);
        if (screenPage_ == PatternScreenModel::Page::PianoRoll)
            setToggle(1, arrow + "N", subRollDivN_);  // HAUT-GAUCHE : Note <-> divisions
        else
            hide(1);
        setToggle(2, arrow + "Long", subVeloGate_);   // Enc3 : Vélo <-> Longueur (span+gate unifiés)
        hide(3);
        return;
    }
    if (screenPage_ == PatternScreenModel::Page::Harmony
        && harmonyFocus_ == HarmonyFocus::Tonality) {
        // Focus TONALITÉ : Enc1 push = Suppr le marqueur ; Tonique/Durée/Gamme en rotation directe.
        const int klen = juce::jlimit(0, 16, static_cast<int>(proc_.engine().pattern().keyProgression.len));
        setAction(0, "Suppr", keyCursor_ < klen);
        hide(1);
        hide(2);
        hide(3);
        return;
    }
    if (screenPage_ == PatternScreenModel::Page::Harmony) {
        const int len = juce::jlimit(0, 32, static_cast<int>(proc_.engine().pattern().chordProgression.len));
        const bool realSlot = harmonyCursor_ < len;   // Suppr seulement sur un slot existant
        setAction(0, "Suppr", realSlot);               // Enc1 : action immédiate
        setToggle(1, "\xe2\x86\x92" "Bass",  harmValBass_);   // Enc2 : →Bass
        hide(2);                                               // Enc3 = Durée en rotation directe (plus de toggle)
        setToggle(3, "\xe2\x86\x92" "Gamme", harmZoomScale_); // Enc4 : →Gamme
        return;
    }
    if (screenPage_ == PatternScreenModel::Page::Pattern) {
        setAction(0, arrow + "Sub", true);            // Enc1 : entre/crée le sous-pattern
        setToggle(1, arrow + "Span", patternValSpan_);// Enc2 : N <-> Span
        setToggle(2, arrow + "Gate", veloGate_);      // Enc3 : Vélo <-> Gate
        setToggle(3, arrow + "Bars", zoomNumBars_);   // Enc4 : Row <-> Mesures
        return;
    }
    if (screenPage_ == PatternScreenModel::Page::PianoRoll) {
        setAction(0, arrow + "Sub", true);            // Enc1 : entre/crée le sous-pattern
        setToggle(1, arrow + "Span", patternValSpan_);// Enc2 : Note <-> Span (comme PATTERN)
        setToggle(2, arrow + "Gate", rollVeloGate_);  // Enc3 : Vélo <-> Gate
        hide(3);                                       // Enc4 = zoom octaves
        return;
    }
    // Auto / Global / Song : pushBtn cachés.
    for (int i = 0; i < 4; ++i) hide(i);
}

void NidmiSeqAudioProcessorEditor::onPushButton(int idx) {
    // Routage du push d'encodeur par Vue. Les actions (drill-in sub) rafraîchissent
    // elles-mêmes l'UI et sortent tôt ; les bascules tombent dans le refresh commun.
    // Page GLOBAL : le push de l'encodeur Valeur declenche l'action de la ligne
    // sous le curseur. Les deux resets sont volontairement separes — effacer ses
    // assignations de controleur n'a rien a voir avec reinitialiser le son.
    if (!inSub_ && screenPage_ == PatternScreenModel::Page::Global && idx == 1) {
        if (oledParamIndex_ == kNumOledParams + 4) {
            proc_.resetLearnMappings();
            buildScreenModel();
            return;
        }
        if (oledParamIndex_ == kNumOledParams + 5) {
            proc_.sendDefaultValues();
            buildScreenModel();
            return;
        }
    }

    if (inSub_) {
        if (idx == 0) { exitSub(); return; }          // Enc1 = Sortir du sub
        else if (idx == 1 && screenPage_ == PatternScreenModel::Page::PianoRoll) {
            subRollDivN_ = !subRollDivN_;             // HAUT-GAUCHE = →N (Note <-> divisions)
        }
        else if (idx == 2) { subVeloGate_ = !subVeloGate_; }   // Enc3 = →Gate (Vélo/Gate du sous-pas)
        else return;                                  // autres push inertes
    } else if (screenPage_ == PatternScreenModel::Page::Pattern) {
        switch (idx) {
            case 0: enterOrCreateSub(); return;        // Enc1 = entre/crée le sub
            case 1: patternValSpan_ = !patternValSpan_; break;
            case 2: veloGate_       = !veloGate_;       break;
            case 3: zoomNumBars_    = !zoomNumBars_;    break;
            default: return;
        }
    } else if (screenPage_ == PatternScreenModel::Page::PianoRoll) {
        if (idx == 0) { enterOrCreateSub(); return; }  // Enc1 = entre/crée le sub
        else if (idx == 1) { patternValSpan_ = !patternValSpan_; }  // Enc2 = →Span (comme PATTERN)
        else if (idx == 2) { rollVeloGate_   = !rollVeloGate_; }    // Enc3 = →Gate
        else return;
    } else if (screenPage_ == PatternScreenModel::Page::Harmony
               && harmonyFocus_ == HarmonyFocus::Tonality) {
        // Focus TONALITÉ : Enc1 push = Suppr le marqueur courant. Autres push inertes.
        if (idx != 0) return;
        const int klen = juce::jlimit(0, 16, static_cast<int>(proc_.engine().pattern().keyProgression.len));
        if (keyCursor_ >= klen) return;   // marqueur d'ajout : rien à supprimer
        SequencerCommand c;
        c.id = SequencerCommandId::DeleteKeySlot;
        c.a  = static_cast<uint8_t>(keyCursor_);
        proc_.controller().postCommand(c);
        keyCursor_ = juce::jlimit(0, juce::jmax(0, klen - 1), keyCursor_);
    } else if (screenPage_ == PatternScreenModel::Page::Harmony) {
        switch (idx) {
            case 0: {   // Enc1 PUSH = Suppr le slot courant (action immédiate, non toggle).
                const int len = juce::jlimit(0, 32, static_cast<int>(proc_.engine().pattern().chordProgression.len));
                if (harmonyCursor_ >= len)
                    return;   // curseur sur slot d'ajout : rien à supprimer
                SequencerCommand c;
                c.id = SequencerCommandId::DeleteChordSlot;
                c.a  = static_cast<uint8_t>(harmonyCursor_);
                proc_.controller().postCommand(c);
                // Clamp du curseur sur la nouvelle longueur (len-1), bornée à >=0.
                harmonyCursor_ = juce::jlimit(0, juce::jmax(0, len - 1), harmonyCursor_);
                break;
            }
            case 1: harmValBass_   = !harmValBass_;   break;   // Enc2 : bascule →Bass
            case 2: harmVeloDur_   = !harmVeloDur_;    break;   // Enc3 : bascule →Durée
            case 3: harmZoomScale_ = !harmZoomScale_;  break;   // Enc4 : bascule →Gamme
            default: return;
        }
    } else {
        return;   // Auto / Global / Song : pas de push câblé.
    }
    applyEncoderConfigForState();
    configureVeloEncoder();
    configurePushButtons();
    buildScreenModel();
}

void NidmiSeqAudioProcessorEditor::configureVeloEncoder() {
    // SONG : Enc3 (param1) est entièrement géré par applyEncoderConfigForState.
    if (screenPage_ == PatternScreenModel::Page::Song)
        return;
    // Dans un sub : Enc3 = Vélo (ou Gate via push) du sous-pas courant.
    // (L'ancre se règle dans PATTERN/ROLL, accès rapide par push ↑Sortir.)
    if (inSub_) {
        const int subIdx = activeSubIdx();
        if (subIdx < 0) return;
        const auto& sp = proc_.engine().pattern().subPatterns[static_cast<size_t>(subIdx)];
        const int   sn = juce::jlimit(1, 16, static_cast<int>(sp.numSteps));
        const int   ss = juce::jlimit(0, sn - 1, subStep_);
        const auto& sd = sp.steps[static_cast<size_t>(ss)];
        if (subVeloGate_) {
            // « Longueur » unifiée (remplace gate) : en quarts de sous-pas, bornée au sub.
            const int maxQ = juce::jmax(1, (sn - ss) * 4);
            const int q    = juce::jlimit(1, maxQ, lengthToQuarters(juce::jmax(1, (int) sd.span), sd.gate));
            veloEncoder_.setRange(1.0, static_cast<double>(maxQ), 1.0);
            veloEncoder_.setValue(static_cast<double>(q), juce::dontSendNotification);
            veloEncoderLabel_.setText("Long " + lengthQuartersLabel(q), juce::dontSendNotification);
        } else {
            veloEncoder_.setRange(0.0, 127.0, 1.0);
            veloEncoder_.setValue(static_cast<double>(sd.velocity), juce::dontSendNotification);
            veloEncoderLabel_.setText(juce::String(juce::CharPointer_UTF8("V\xc3\xa9lo ")) + juce::String(sd.velocity), juce::dontSendNotification);
        }
        return;
    }

    // HARMONIE : la qualité (triade) passe sur les blanches 8..13 → Enc3 = Durée du slot.
    if (screenPage_ == PatternScreenModel::Page::Harmony) {
        const auto& prog = proc_.engine().pattern().chordProgression;
        const int   cur  = juce::jlimit(0, 31, harmonyCursor_);
        int dur = juce::jmax(1, static_cast<int>(proc_.engine().pattern().numerator));
        if (cur < static_cast<int>(prog.len))
            dur = prog.slots[static_cast<size_t>(cur)].durationBeats;
        veloEncoder_.setRange(1.0, 64.0, 1.0);
        veloEncoder_.setValue(static_cast<double>(dur), juce::dontSendNotification);
        // durationBeats = nombre de TEMPS pendant lesquels l'accord reste actif.
        veloEncoderLabel_.setText("Duree " + juce::String(dur) + " tps", juce::dontSendNotification);
        return;
    }

    // Cale Enc3 sur Vélo, ou Gate selon la bascule push de l'encodeur (par vue).
    const bool gate = (screenPage_ == PatternScreenModel::Page::PianoRoll) ? rollVeloGate_ : veloGate_;
    const auto& pat = proc_.engine().pattern();
    if (pat.numRows == 0)
        return;
    const int sr = juce::jlimit(0, static_cast<int>(pat.numRows) - 1, selectedRow_);
    const int n  = juce::jlimit(1, 64, static_cast<int>(pat.rows[static_cast<size_t>(sr)].numSteps));
    const int ss = juce::jlimit(0, n - 1, selectedStep_);
    const auto& sd = pat.rows[static_cast<size_t>(sr)].step(static_cast<uint8_t>(editBar_), static_cast<uint8_t>(ss));
    if (gate) {
        veloEncoder_.setRange(1.0, 100.0, 1.0);
        veloEncoder_.setValue(static_cast<double>(sd.gate), juce::dontSendNotification);
        veloEncoderLabel_.setText("Gate " + juce::String(sd.gate), juce::dontSendNotification);
    } else {
        veloEncoder_.setRange(0.0, 127.0, 1.0);
        veloEncoder_.setValue(static_cast<double>(sd.velocity), juce::dontSendNotification);
        veloEncoderLabel_.setText(juce::String(juce::CharPointer_UTF8("V\xc3\xa9lo ")) + juce::String(sd.velocity), juce::dontSendNotification);
    }
}

void NidmiSeqAudioProcessorEditor::onVeloEncoderChanged() {
    encSyncCooldown_ = 8;
    // Dans un sub : Enc3 = Vélo (ou Gate via push) du sous-pas courant.
    // SetSubStep(subIdx, subStep, note, vélo, gate) : on relit la note + le champ
    // non édité pour ne changer QUE vélo (ou gate).
    if (inSub_) {
        const int subIdx = activeSubIdx();
        if (subIdx < 0) return;
        const auto& sp = proc_.engine().pattern().subPatterns[static_cast<size_t>(subIdx)];
        const int   sn = juce::jlimit(1, 16, static_cast<int>(sp.numSteps));
        const int   ss = juce::jlimit(0, sn - 1, subStep_);
        const auto& sd = sp.steps[static_cast<size_t>(ss)];
        const int   v  = (int) std::lround(veloEncoder_.getValue());
        if (subVeloGate_) {
            // « Longueur » : v = quarts → span (couvre/masque) + gate (fraction du span).
            const int span = quartersToSpan(v, sn - ss);
            const int gate = quartersToGate(v, span);
            SequencerCommand cs;
            cs.id = SequencerCommandId::SetSubStepSpan;
            cs.a  = static_cast<uint8_t>(subIdx);
            cs.b  = static_cast<uint8_t>(ss);
            cs.c  = static_cast<uint8_t>(span);
            proc_.controller().postCommand(cs);
            SequencerCommand c;
            c.id = SequencerCommandId::SetSubStep;
            c.a  = static_cast<uint8_t>(subIdx);
            c.b  = static_cast<uint8_t>(ss);
            c.c  = sd.note;                                              // note préservée
            c.d  = sd.velocity;                                         // vélo préservée
            c.e  = static_cast<uint8_t>(gate);                          // gate dérivé de la longueur
            proc_.controller().postCommand(c);
        } else {
            SequencerCommand c;
            c.id = SequencerCommandId::SetSubStep;
            c.a  = static_cast<uint8_t>(subIdx);
            c.b  = static_cast<uint8_t>(ss);
            c.c  = sd.note;                                              // note préservée
            c.d  = static_cast<uint8_t>(juce::jlimit(0, 127, v));       // Vélo
            c.e  = sd.gate;                                             // gate préservé
            proc_.controller().postCommand(c);
        }
        buildScreenModel();
        return;
    }

    // HARMONIE : Enc3 = Durée (du slot d'accord, ou du marqueur de tonalité selon le focus).
    if (screenPage_ == PatternScreenModel::Page::Harmony) {
        if (harmonyFocus_ == HarmonyFocus::Tonality)
            setKeyField(2, (int) std::lround(veloEncoder_.getValue()));    // Durée du marqueur (temps)
        else
            setChordField(4, (int) std::lround(veloEncoder_.getValue()));
        return;
    }

    // SONG : Enc3 = param1 du slot (idx pattern si Play, count si Repeat ; ignoré sinon).
    if (screenPage_ == PatternScreenModel::Page::Song) {
        songSetSlotP1((int) std::lround(veloEncoder_.getValue()));
        return;
    }

    // Tourne = Vélo ; bascule push →Gate (par vue) = Gate. Seulement sur un pas actif.
    const bool gate = (screenPage_ == PatternScreenModel::Page::PianoRoll) ? rollVeloGate_ : veloGate_;
    const auto& pat = proc_.engine().pattern();
    if (pat.numRows == 0)
        return;
    const int sr = juce::jlimit(0, static_cast<int>(pat.numRows) - 1, selectedRow_);
    const int n  = juce::jlimit(1, 64, static_cast<int>(pat.rows[static_cast<size_t>(sr)].numSteps));
    const int ss = juce::jlimit(0, n - 1, selectedStep_);
    if (!pat.rows[static_cast<size_t>(sr)].step(static_cast<uint8_t>(editBar_), static_cast<uint8_t>(ss)).enabled)
        return;
    setStepField(ss, gate ? 2 : 1, (int) std::lround(veloEncoder_.getValue()));
}

void NidmiSeqAudioProcessorEditor::onZoomEncoderChanged() {
    encSyncCooldown_ = 8;
    // Encodeur relatif progressif. En ROLL = zoom octaves (cran ample) ; ailleurs = Row (plus réactif).
    const bool   roll  = (screenPage_ == PatternScreenModel::Page::PianoRoll);
    const double kStep = roll ? 60.0 : 30.0;
    const double v     = zoomEncoder_.getValue();
    const double delta = v - lastZoomEnc_;
    if (std::abs(delta) < kStep) {
        if (v < 40.0 || v > 960.0) {   // recentre près des bords (sans générer de cran)
            zoomEncoder_.setValue(500.0, juce::dontSendNotification);
            lastZoomEnc_ = 500.0;
        }
        return;
    }
    const int dir = (delta > 0) ? 1 : -1;
    lastZoomEnc_  = v;
    if (roll) {
        // PIANO ROLL : zoom = octaves visibles, jusqu'à toute la plage MIDI (~11 octaves).
        rollOctaves_ = juce::jlimit(1, 11, rollOctaves_ - dir);
    } else if (screenPage_ == PatternScreenModel::Page::Harmony
               && harmonyFocus_ == HarmonyFocus::Tonality) {
        // Focus TONALITÉ : Enc4 = Gamme du marqueur courant (relatif). Tonique = Enc2, durée = Enc3.
        const auto& kp = proc_.engine().pattern().keyProgression;
        const int   kc = juce::jlimit(0, 15, keyCursor_);
        const int   curScale = (kc < static_cast<int>(kp.len))
                                   ? static_cast<int>(kp.slots[static_cast<size_t>(kc)].scaleId)
                                   : static_cast<int>(proc_.engine().projectSettings().masterScaleId);
        setKeyField(1, curScale + dir);
    } else if (screenPage_ == PatternScreenModel::Page::Harmony) {
        {
            // HARMONIE : Enc4 = Tonique (push →Gamme = Gamme). Relatif, wrap modulo 12.
            // (Harmonie ON/OFF est sur le bouton « Harm » dédié — plus de ⇧Enc4.)
            // Édite la SOURCE ACTIVE : master (si followMasterTonality) sinon le pattern.
            const auto& ph    = proc_.engine().pattern().harmony;
            const auto& ps    = proc_.engine().projectSettings();
            const bool  toMas = ph.followMasterTonality;
            const int   nScale = juce::jmax(1, static_cast<int>(scalebank::Count));
            if (harmZoomScale_) {
                const int curScale = toMas ? static_cast<int>(ps.masterScaleId)
                                           : static_cast<int>(ph.scaleId);
                const int newScale = ((curScale + dir) % nScale + nScale) % nScale;  // wrap
                if (toMas) {
                    SequencerCommand c;
                    c.id = SequencerCommandId::SetProjectMasterScaleId;
                    c.a  = static_cast<uint8_t>(newScale);
                    proc_.controller().postCommand(c);
                } else {
                    SequencerCommand c;
                    c.id = SequencerCommandId::SetPatternHarmony;
                    c.x  = ph.harmonyEnabled;                       // préserve ON/OFF
                    c.a  = static_cast<uint8_t>(newScale);          // gamme éditée
                    c.b  = static_cast<uint8_t>(ph.rootPc);         // préserve tonique
                    c.y  = ph.followProgression;                    // préserve suivi
                    proc_.controller().postCommand(c);
                }
            } else {
                const int curRoot = toMas ? static_cast<int>(ps.masterRootPc)
                                          : static_cast<int>(ph.rootPc);
                const int newRoot = ((curRoot + dir) % 12 + 12) % 12;  // wrap 12 demi-tons
                if (toMas) {
                    SequencerCommand c;
                    c.id = SequencerCommandId::SetProjectMasterRootPc;
                    c.a  = static_cast<uint8_t>(newRoot);
                    proc_.controller().postCommand(c);
                } else {
                    SequencerCommand c;
                    c.id = SequencerCommandId::SetPatternHarmony;
                    c.x  = ph.harmonyEnabled;
                    c.a  = static_cast<uint8_t>(ph.scaleId);        // préserve gamme
                    c.b  = static_cast<uint8_t>(newRoot);           // tonique éditée
                    c.y  = ph.followProgression;
                    proc_.controller().postCommand(c);
                }
            }
        }
    } else if (screenPage_ == PatternScreenModel::Page::Pattern && zoomNumBars_) {
        // Push →Bars (PATTERN) = nombre de MESURES du pattern (1..kMaxBars). Le core duplique
        // la dernière mesure quand on augmente ; ici on poste juste la commande + on rafraîchit.
        const int cur = juce::jlimit(1, 4, static_cast<int>(proc_.engine().patternNumBars()));
        const int nb  = juce::jlimit(1, 4, cur + dir);
        if (nb != cur) {
            SequencerCommand c;
            c.id = SequencerCommandId::SetPatternNumBars;
            c.a  = static_cast<uint8_t>(nb);
            proc_.controller().postCommand(c);
            editBar_ = juce::jlimit(0, nb - 1, editBar_);
        }
    } else if (screenPage_ == PatternScreenModel::Page::Song) {
        // SONG : Enc4 = param2 du slot (répétitions du PlayPattern). Relatif.
        const auto& chain = proc_.engine().chainProgram();
        const int   cur   = songCursor_;
        if (cur >= 0 && cur < static_cast<int>(chain.len)) {
            const int p2 = static_cast<int>(chain.slots[static_cast<size_t>(cur)].param2);
            songSetSlotP2(p2 + dir);
        }
    } else {
        // PATTERN/AUTO : Enc4 = sélection de Row (remplace le zoom).
        const int nr = juce::jmax(1, static_cast<int>(proc_.engine().pattern().numRows));
        selectedRow_ = juce::jlimit(0, nr - 1, selectedRow_ + dir);
        applyEncoderConfigForState();
    }
    buildScreenModel();
}

// --- Encodeur MASTER (contextuel) ------------------------------------------
// Mode Accent (PATTERN) → montant d'accent ; mode Swing → swing % ; sinon → tempo (BPM).

void NidmiSeqAudioProcessorEditor::configureMasterEncoder() {
    const auto& pat = proc_.engine().pattern();
    const bool inPattern = (screenPage_ == PatternScreenModel::Page::Pattern);
    // Push master actif : sélection du pattern de la banque (prioritaire sur BPM/Accent/Swing).
    if (masterPatternMode_) {
        const int ap = static_cast<int>(proc_.engine().activePatternIndex()) + 1;
        masterEncoder_.setRange(1.0, static_cast<double>(kMaxPatterns), 1.0);
        masterEncoder_.setValue(static_cast<double>(juce::jlimit(1, static_cast<int>(kMaxPatterns), ap)),
                                juce::dontSendNotification);
        masterEncoderLabel_.setText("Pat " + juce::String(ap) + "/" + juce::String(static_cast<int>(kMaxPatterns)),
                                    juce::dontSendNotification);
        return;
    }
    if (inPattern && padMode_ == PadMode::Accent) {
        const int v = juce::jlimit(0, 127, static_cast<int>(pat.timing.accentAmount));
        masterEncoder_.setRange(0.0, 127.0, 1.0);
        masterEncoder_.setValue(static_cast<double>(v), juce::dontSendNotification);
        masterEncoderLabel_.setText("Accent " + juce::String(v), juce::dontSendNotification);
    } else if (inPattern && padMode_ == PadMode::Swing) {
        const int v = juce::jlimit(50, 75, static_cast<int>(pat.timing.swingPositionPercent));
        masterEncoder_.setRange(50.0, 75.0, 1.0);
        masterEncoder_.setValue(static_cast<double>(v), juce::dontSendNotification);
        masterEncoderLabel_.setText("Swing " + juce::String(v) + "%", juce::dontSendNotification);
    } else {
        double lo = 20.0, hi = 300.0;
        if (auto* pf = dynamic_cast<juce::AudioParameterFloat*>(proc_.apvts().getParameter("bpm"))) {
            lo = pf->range.start; hi = pf->range.end;
        }
        const float bpm = proc_.apvts().getRawParameterValue("bpm") != nullptr
                              ? proc_.apvts().getRawParameterValue("bpm")->load() : 120.0f;
        masterEncoder_.setRange(lo, hi, 1.0);
        masterEncoder_.setValue(static_cast<double>(bpm), juce::dontSendNotification);
        masterEncoderLabel_.setText("BPM " + juce::String(juce::roundToInt(bpm)), juce::dontSendNotification);
    }
}

void NidmiSeqAudioProcessorEditor::onMasterEncoderChanged() {
    encSyncCooldown_ = 8;
    const int v = static_cast<int>(std::lround(masterEncoder_.getValue()));
    const bool inPattern = (screenPage_ == PatternScreenModel::Page::Pattern);
    // Push master actif : sélection du pattern de la banque.
    if (masterPatternMode_) {
        const int idx = juce::jlimit(1, static_cast<int>(kMaxPatterns), v) - 1;
        SequencerCommand c;
        c.id = SequencerCommandId::SelectPattern;
        c.a  = static_cast<uint8_t>(idx);
        proc_.controller().postCommand(c);
        selectedStep_   = 0;   // le contenu édité suit le pattern actif
        masterHudText_  = "Pattern " + juce::String(idx + 1);
        masterEncoderLabel_.setText("Pat " + juce::String(idx + 1) + "/" + juce::String(static_cast<int>(kMaxPatterns)),
                                    juce::dontSendNotification);
        masterHudFrames_ = 36;
        buildScreenModel();
        return;
    }
    if (inPattern && padMode_ == PadMode::Accent) {
        SequencerCommand c; c.id = SequencerCommandId::SetPatternAccentAmount;
        c.a = static_cast<uint8_t>(juce::jlimit(0, 127, v));
        proc_.controller().postCommand(c);
        masterHudText_ = "Accent " + juce::String(juce::jlimit(0, 127, v));
        masterEncoderLabel_.setText(masterHudText_, juce::dontSendNotification);
    } else if (inPattern && padMode_ == PadMode::Swing) {
        const int pct = juce::jlimit(50, 75, v);
        SequencerCommand c; c.id = SequencerCommandId::SetPatternSwing; c.x = true;
        c.a = static_cast<uint8_t>(pct);
        proc_.controller().postCommand(c);
        masterHudText_ = "Swing " + juce::String(pct) + "%";
        masterEncoderLabel_.setText(masterHudText_, juce::dontSendNotification);
    } else {
        // Tempo : édite le paramètre APVTS « bpm » (le processeur l'applique au moteur).
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(proc_.apvts().getParameter("bpm"))) {
            rp->setValueNotifyingHost(rp->convertTo0to1(static_cast<float>(v)));
            masterHudText_ = "BPM " + juce::String(v);
            masterEncoderLabel_.setText(masterHudText_, juce::dontSendNotification);
        }
    }
    masterHudFrames_ = 36;   // ~1,2 s d'affichage du HUD à l'écran
    buildScreenModel();
}
