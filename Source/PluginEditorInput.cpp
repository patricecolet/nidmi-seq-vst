#include "PluginEditor.h"
#include "PluginProcessor.h"

#include "EditorHelpers.h"
#include "MidiExporter.h"

#include <nidmi_seq/ScaleBank.h>
#include <nidmi_seq/HarmonyEngine.h>

#include <cmath>

// Re-tap pour élargir : un tap sur la MÊME cible (même touche + même curseur) fait
// Pas → Row → Mesure → Pas ; sinon (autre touche ou curseur déplacé) repart à Pas.
NidmiSeqAudioProcessorEditor::ClipScope
NidmiSeqAudioProcessorEditor::advanceClipScope(int key) {
    const bool sameTarget = (key == lastClipKey_
                             && selectedRow_  == lastClipRow_
                             && selectedStep_ == lastClipStep_
                             && editBar_      == lastClipBar_);
    if (sameTarget) {
        clipCycleScope_ = static_cast<ClipScope>((static_cast<int>(clipCycleScope_) + 1) % 3);
    } else {
        clipCycleScope_ = ClipScope::Pas;
    }
    lastClipKey_  = key;
    lastClipRow_  = selectedRow_;
    lastClipStep_ = selectedStep_;
    lastClipBar_  = editBar_;
    return clipCycleScope_;
}

// --- Clavier : miroir de l'onglet actif (cahier §10.3) ----------------------

juce::String NidmiSeqAudioProcessorEditor::noteNameFromMidi(int midi) {
    static const char* kNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    if (midi < 0 || midi > 127)
        return "?";
    const int oct = midi / 12 - 1;  // MIDI 60 = C4
    return juce::String(kNames[midi % 12]) + juce::String(oct);
}

int NidmiSeqAudioProcessorEditor::rollWhiteKeyMidi(int index) const {
    // Clavier diatonique : 16 blanches = 16 degrés consécutifs de la gamme maître.
    int rootPc = 0, scaleId = 0;
    if (auto* p = proc_.apvts().getRawParameterValue("masterRoot"))  rootPc  = static_cast<int>(p->load());
    if (auto* p = proc_.apvts().getRawParameterValue("masterScale")) scaleId = static_cast<int>(p->load());
    const auto& sc    = scalebank::getScale(static_cast<uint8_t>(
        juce::jlimit(0, static_cast<int>(scalebank::Count) - 1, scaleId)));
    const int   count = juce::jmax(1, static_cast<int>(sc.count));
    const int   k     = juce::jlimit(0, 15, index);
    const int   base  = 48 + keyboardOctave_ * 12 + rootPc;   // ~do central, décalable
    return juce::jlimit(0, 127, base + 12 * (k / count) + sc.intervals[k % count]);
}

// Miroir de HardwareStyleComponents : la noire k est entre la blanche kBlackLeftWhiteIndex[k]
// et la suivante. Une noire ROLL joue donc un demi-ton au-dessus de sa blanche de gauche.
static const int kBlackLeftWhiteIndexEd[11] = {0, 1, 3, 4, 5, 7, 8, 10, 11, 12, 14};

int NidmiSeqAudioProcessorEditor::rollBlackKeyMidi(int blackIndex) const {
    const int k = juce::jlimit(0, 10, blackIndex);
    return juce::jlimit(0, 127, rollWhiteKeyMidi(kBlackLeftWhiteIndexEd[k]) + 1);
}

void NidmiSeqAudioProcessorEditor::shiftKeyboardOctave(int delta) {
    keyboardOctave_ = juce::jlimit(-4, 4, keyboardOctave_ + delta);
    updateKeysForPage();
    buildScreenModel();   // la fenêtre de hauteurs (PIANO ROLL) défile aussitôt
}

void NidmiSeqAudioProcessorEditor::onWhiteKey(int index) {
    const auto& pat = proc_.engine().pattern();
    if (pat.numRows == 0)
        return;

    // Dans un sub : ROLL = pose la hauteur (clavier diatonique), sinon = toggle du sous-pas.
    if (inSub_) {
        const int subIdx = activeSubIdx();
        if (subIdx < 0) return;
        const int sn = juce::jlimit(1, 16, static_cast<int>(pat.subPatterns[static_cast<size_t>(subIdx)].numSteps));
        if (screenPage_ == PatternScreenModel::Page::PianoRoll) {
            postSubStepPitch(juce::jlimit(0, sn - 1, subStep_), rollWhiteKeyMidi(index));
        } else {
            if (index >= sn) return;
            subStep_ = index;
            SequencerCommand c;
            c.id = SequencerCommandId::ToggleSubStep;
            c.a  = static_cast<uint8_t>(subIdx);
            c.b  = static_cast<uint8_t>(index);
            proc_.controller().postCommand(c);
        }
        applyEncoderConfigForState();
        buildScreenModel();
        return;
    }

    const int   sr  = juce::jlimit(0, static_cast<int>(pat.numRows) - 1, selectedRow_);
    const auto& row = pat.rows[static_cast<size_t>(sr)];
    const int   n   = juce::jmax(1, static_cast<int>(row.numSteps));

    switch (screenPage_) {
        case PatternScreenModel::Page::Pattern: {
            const int step = stepPage_ * 16 + index;   // fenêtre de page
            if (step >= n) return;
            // Mode ACCENT / SWING (façon Elektron) : la blanche toggle le flag du pas (pas le on/off).
            if (padMode_ == PadMode::Accent || padMode_ == PadMode::Swing) {
                selectedStep_ = step;
                const auto& sd = row.step(static_cast<uint8_t>(editBar_), static_cast<uint8_t>(step));
                SequencerCommand c;
                if (padMode_ == PadMode::Accent) { c.id = SequencerCommandId::SetStepAccent; c.x = !sd.accent; }
                else                             { c.id = SequencerCommandId::SetStepSwing;  c.x = !sd.swingEnable; }
                c.a = static_cast<uint8_t>(sr);
                c.b = static_cast<uint8_t>(step);
                c.f = static_cast<uint8_t>(editBar_);
                proc_.controller().postCommand(c);
                break;
            }
            // PAD : toggle le pas.
            // Pas recouvert par un owner antérieur (drill / note longue span>1) : on sélectionne
            // l'owner au lieu de créer un pas par-dessus (cohérent avec le clic souris).
            int owner = -1;
            for (int o = step - 1; o >= 0; --o) {
                const auto& osd = row.step(static_cast<uint8_t>(editBar_), static_cast<uint8_t>(o));
                if (!osd.enabled) continue;
                const int osp = juce::jlimit(1, n - o, juce::jmax(1, static_cast<int>(osd.span)));
                if (o + osp > step) { owner = o; break; }
            }
            if (owner >= 0) { selectedStep_ = owner; break; }   // zone couverte : sélectionne le drill
            selectedStep_ = step;
            SequencerCommand c;
            c.id = SequencerCommandId::ToggleStep;
            c.a  = static_cast<uint8_t>(sr);
            c.b  = static_cast<uint8_t>(step);
            c.f  = static_cast<uint8_t>(editBar_);
            proc_.controller().postCommand(c);
            break;
        }
        case PatternScreenModel::Page::PianoRoll: {
            // Pose la hauteur (degré diatonique) sur le pas sous le curseur ; REC ON = step-record.
            const int   ss = juce::jlimit(0, n - 1, selectedStep_);
            const auto& sd = row.step(static_cast<uint8_t>(editBar_), static_cast<uint8_t>(ss));
            SequencerCommand c;
            c.id = SequencerCommandId::SetStep;
            c.a  = static_cast<uint8_t>(sr);
            c.b  = static_cast<uint8_t>(ss);
            c.c  = static_cast<uint8_t>(rollWhiteKeyMidi(index));
            c.d  = sd.velocity;
            c.e  = sd.gate;
            c.f  = static_cast<uint8_t>(editBar_);
            proc_.controller().postCommand(c);
            if (recArmed_)
                selectedStep_ = (ss + 1) % n;   // avance le curseur
            break;
        }
        case PatternScreenModel::Page::Auto: {
            const int step = stepPage_ * 16 + index;   // fenêtre de page
            if (step >= n) return;          // toggle le P-lock du slot actif
            selectedStep_ = step;
            const int   slot = juce::jlimit(0, 7, autoSlot_);
            const auto& lk   = row.step(static_cast<uint8_t>(editBar_), static_cast<uint8_t>(step)).ccLocks[static_cast<size_t>(slot)];
            if (lk.ccNumber != 0xFF) {
                SequencerCommand c;
                c.id = SequencerCommandId::ClearStepCCLock;
                c.a  = static_cast<uint8_t>(sr);
                c.b  = static_cast<uint8_t>(step);
                c.c  = static_cast<uint8_t>(slot);
                c.f  = static_cast<uint8_t>(editBar_);
                proc_.controller().postCommand(c);
            } else {
                postAutoValueAt(step, 100);  // pose un lock à valeur par défaut
            }
            break;
        }
        case PatternScreenModel::Page::Harmony: {
            // ⇧ + blanche N : cycle l'état harmonique de la row N POUR LA MESURE ÉDITÉE.
            // Ordre : Chromatic(délié) → A → B1 → B2 → Chromatic.
            if (shiftActive()) {
                const int hr = index;   // blanche index ↔ row index
                if (hr >= static_cast<int>(pat.numRows)) return;   // pas de row N
                const int rm = static_cast<int>(pat.rows[static_cast<size_t>(hr)].harmonyModeAt(static_cast<uint8_t>(editBar_)));
                int newMode;
                switch (rm) {
                    case static_cast<int>(RowHarmonyMode::Chromatic): newMode = 0; break;  // → A
                    case 0: newMode = 1; break;                                            // A → B1
                    case 1: newMode = 2; break;                                            // B1 → B2
                    default: newMode = static_cast<int>(RowHarmonyMode::Chromatic); break; // B2 → Chromatic
                }
                SequencerCommand c;
                c.id = SequencerCommandId::SetRowHarmonyMode;
                c.a  = static_cast<uint8_t>(hr);
                c.b  = static_cast<uint8_t>(newMode);
                c.f  = static_cast<uint8_t>(editBar_);   // mesure ciblée
                proc_.controller().postCommand(c);
                buildScreenModel();
                return;
            }
            if (index >= 0 && index < 7) {
                setChordField(0, index + 1);     // blanches 0..6 = degrés I..VII
                if (recArmed_)                   // REC = step-record d'accords : avance au slot suivant
                    harmonyCursor_ = juce::jlimit(0, 31, harmonyCursor_ + 1);
            } else if (index >= 7 && index <= 12) {
                setChordField(1, index - 7);     // blanches 7..12 = triades 0..5 (qualité) ; pas d'avance REC
            } else if (index == 13 || index == 14) {
                // blanches 13/14 = nav de mesure (Mes-/Mes+) : le mode harmo est par row+mesure.
                const int nb = juce::jmax(1, static_cast<int>(pat.numBars));
                editBar_ = juce::jlimit(0, nb - 1, editBar_ + (index == 13 ? -1 : 1));
                updateKeysForPage();   // rafraîchit les labels R1:A… de la nouvelle mesure
            }
            // blanche 15 : inactive.
            break;
        }
        case PatternScreenModel::Page::Global:
        case PatternScreenModel::Page::Song:
        default:
            break;
    }
    applyEncoderConfigForState();
    buildScreenModel();
}

void NidmiSeqAudioProcessorEditor::onBlackKey(int index) {
    // Dans un sub : noire 0 = sortir, noire 9 = bascule mode relatif/absolu.
    // La noire 9 porte le mode rel/abs PARTOUT (cohérence "même touche") ; la noire 1 est libérée.
    if (inSub_) {
        if (index == 0)      exitSub();
        else if (index == 9) toggleSubMode();
        return;
    }
    const auto& pat = proc_.engine().pattern();

    // Mapping des fonctions des 11 noires, IDENTIQUE sur la même touche physique en PATTERN et ROLL.
    // 0=R- 1=R+ 2=Page- 3=Page+ 4=Sub 5=Mes- 6=Mes+ 7=Oct- 8=Oct+ 9,10=libre.
    // allowOct : Oct± n'a de sens qu'en ROLL (clavier de hauteur).
    auto runFunction = [this, &pat](int idx, bool allowOct) {
        switch (idx) {
            case 0: selectedRow_ = juce::jmax(0, selectedRow_ - 1); break;                       // R-
            case 1: selectedRow_ = juce::jmin(static_cast<int>(pat.numRows) - 1, selectedRow_ + 1); break; // R+
            case 2: stepPage_ = juce::jmax(0, stepPage_ - 1); break;                             // Page-
            case 3: stepPage_ = juce::jmin(stepPageCount() - 1, stepPage_ + 1); break;           // Page+
            case 4: enterOrCreateSub(); return;                                                  // Sub (drill-in)
            case 5: editBar_ = juce::jmax(0, editBar_ - 1); break;                               // Mes-
            case 6: editBar_ = juce::jmin(static_cast<int>(pat.numBars) - 1, editBar_ + 1); break; // Mes+
            case 7: if (allowOct) shiftKeyboardOctave(-1); break;                                // Oct-
            case 8: if (allowOct) shiftKeyboardOctave(+1); break;                                // Oct+
            default: break;                                                                      // 9,10 libres
        }
    };

    switch (screenPage_) {
        case PatternScreenModel::Page::Pattern: {
            // ⇧+noire 2 = mode ACCENT, ⇧+noire 3 = mode SWING (façon Elektron) : les blanches
            // togglent alors le flag du pas. Re-appui sur la même = retour au mode Pas (on/off).
            // À l'entrée, on garantit un effet audible (montant accent / swing global par défaut).
            if (shiftActive() && (index == 2 || index == 3)) {
                const PadMode want = (index == 2) ? PadMode::Accent : PadMode::Swing;
                padMode_ = (padMode_ == want) ? PadMode::Steps : want;
                if (padMode_ == PadMode::Accent
                    && proc_.engine().pattern().timing.accentAmount == 0) {
                    SequencerCommand c; c.id = SequencerCommandId::SetPatternAccentAmount; c.a = 40;
                    proc_.controller().postCommand(c);          // défaut audible
                } else if (padMode_ == PadMode::Swing
                           && !proc_.engine().pattern().timing.swingEnabled) {
                    SequencerCommand c; c.id = SequencerCommandId::SetPatternSwing; c.x = true; c.a = 62;
                    proc_.controller().postCommand(c);          // swing global ON + défaut 62%
                }
                updateKeysForPage();
                break;
            }
            // Blanches = pas (pas de hauteur à jouer) → noire SEULE = la fonction. Oct± inactif.
            // Noire 9 = bascule rel/abs du sub du pas sélectionné (inactif si pas de sub).
            // ⇧+noire 9 = DÉTACHER un sous-pattern partagé (ghost) → copie indépendante.
            // Noires libres 7/8/10 = presse-papier de pas : 7=Copy (⇧=Cut) 8=Paste 10=Clear.
            if (index == 9) {
                if (shiftActive()) {
                    const int sub = selectedStepSubIdx();
                    if (sub >= 0 && proc_.engine().subPatternRefCount(static_cast<uint8_t>(sub)) > 1) {
                        const int sr = juce::jlimit(0, static_cast<int>(pat.numRows) - 1, selectedRow_);
                        const int n  = juce::jmax(1, static_cast<int>(pat.rows[static_cast<size_t>(sr)].numSteps));
                        const int ss = juce::jlimit(0, n - 1, selectedStep_);
                        SequencerCommand c;
                        c.id = SequencerCommandId::DetachStepSubPattern;
                        c.a  = static_cast<uint8_t>(sr);
                        c.b  = static_cast<uint8_t>(ss);
                        c.f  = static_cast<uint8_t>(editBar_);
                        proc_.controller().postCommand(c);
                    }
                } else {
                    toggleSubMode();
                }
                break;
            }
            if (index == 7 || index == 8 || index == 10) {
                if (pat.numRows == 0) break;
                const int   sr  = juce::jlimit(0, static_cast<int>(pat.numRows) - 1, selectedRow_);
                const auto& row = pat.rows[static_cast<size_t>(sr)];
                const int   n   = juce::jmax(1, static_cast<int>(row.numSteps));
                const int   ss  = juce::jlimit(0, n - 1, selectedStep_);
                // Poste le CLEAR de la SOURCE selon le grain (pour Cut). dr/ds/db = coords source.
                auto postClearScoped = [this](ClipScope sc, int dr, int ds, int db) {
                    SequencerCommand c;
                    if (sc == ClipScope::Mesure)   { c.id = SequencerCommandId::ClearBar; c.f = static_cast<uint8_t>(db); }
                    else if (sc == ClipScope::Row) { c.id = SequencerCommandId::ClearRow; c.a = static_cast<uint8_t>(dr); c.f = static_cast<uint8_t>(db); }
                    else                           { c.id = SequencerCommandId::ClearStep; c.a = static_cast<uint8_t>(dr); c.b = static_cast<uint8_t>(ds); c.f = static_cast<uint8_t>(db); }
                    proc_.controller().postCommand(c);
                };
                if (index == 7) {
                    // Copy (⇧ = Cut). Re-tap sur la même cible élargit le grain Pas→Row→Mesure.
                    const ClipScope sc = advanceClipScope(7);
                    stepClip_ = { sr, ss, editBar_, /*valid*/ true, /*cut*/ shiftActive(), sc };
                } else if (index == 8) {
                    // Paste : CopyStep/CopyRow/CopyBar(source -> curseur) selon le grain mémorisé.
                    if (stepClip_.valid) {
                        SequencerCommand c;
                        switch (stepClip_.scope) {
                            case ClipScope::Mesure:
                                c.id = SequencerCommandId::CopyBar;
                                c.a  = static_cast<uint8_t>(stepClip_.bar);    // barSrc
                                c.f  = static_cast<uint8_t>(editBar_);         // barDst
                                break;
                            case ClipScope::Row:
                                c.id = SequencerCommandId::CopyRow;
                                c.a  = static_cast<uint8_t>(stepClip_.row);    // rowSrc
                                c.b  = static_cast<uint8_t>(stepClip_.bar);    // barSrc
                                c.c  = static_cast<uint8_t>(sr);               // rowDst
                                c.f  = static_cast<uint8_t>(editBar_);         // barDst
                                break;
                            case ClipScope::Pas:
                                c.id = SequencerCommandId::CopyStep;
                                c.a  = static_cast<uint8_t>(stepClip_.row);    // rowSrc
                                c.b  = static_cast<uint8_t>(stepClip_.step);   // stepSrc
                                c.c  = static_cast<uint8_t>(sr);               // rowDst
                                c.d  = static_cast<uint8_t>(ss);               // stepDst
                                c.e  = static_cast<uint8_t>(stepClip_.bar);    // barSrc
                                c.f  = static_cast<uint8_t>(editBar_);         // barDst
                                break;
                        }
                        proc_.controller().postCommand(c);
                        if (stepClip_.cut) {
                            postClearScoped(stepClip_.scope, stepClip_.row, stepClip_.step, stepClip_.bar);
                            stepClip_.valid = false;   // le déplacement consomme le presse-papier
                        }
                        // Grain PAS : avance le curseur APRÈS le span collé (un drill/note longue
                        // occupe ss..ss+span-1) → chaînage : le prochain Paste tombe juste après.
                        if (stepClip_.scope == ClipScope::Pas) {
                            const int srcRow = juce::jlimit(0, static_cast<int>(pat.numRows) - 1, stepClip_.row);
                            const int srcN   = juce::jmax(1, static_cast<int>(pat.rows[static_cast<size_t>(srcRow)].numSteps));
                            const int srcStp = juce::jlimit(0, srcN - 1, stepClip_.step);
                            const int span   = juce::jmax(1, static_cast<int>(
                                pat.rows[static_cast<size_t>(srcRow)].step(
                                    static_cast<uint8_t>(stepClip_.bar), static_cast<uint8_t>(srcStp)).span));
                            selectedStep_ = juce::jlimit(0, n - 1, ss + span);
                        }
                        lastClipKey_ = 8;              // un Copy suivant repartira à Pas
                    }
                } else {   // index == 10 : Clear. Re-tap élargit le grain Pas→Row→Mesure.
                    const ClipScope sc = advanceClipScope(10);
                    postClearScoped(sc, sr, ss, editBar_);
                }
                break;
            }
            runFunction(index, /*allowOct*/ false);
            break;
        }
        case PatternScreenModel::Page::PianoRoll:
            if (shiftActive()) {
                // ⇧ + noire = fonction (R±/Page±/Sub/Mes±/Oct±) ; noire 9 = rel/abs du sub du pas sélectionné.
                if (index == 9) toggleSubMode();
                else            runFunction(index, /*allowOct*/ true);
            } else {
                // Noire SEULE = joue/pose une note chromatique, même chemin que onWhiteKey ROLL.
                if (pat.numRows == 0) return;
                const int   sr  = juce::jlimit(0, static_cast<int>(pat.numRows) - 1, selectedRow_);
                const auto& row = pat.rows[static_cast<size_t>(sr)];
                const int   n   = juce::jmax(1, static_cast<int>(row.numSteps));
                const int   ss  = juce::jlimit(0, n - 1, selectedStep_);
                const auto& sd  = row.step(static_cast<uint8_t>(editBar_), static_cast<uint8_t>(ss));
                SequencerCommand c;
                c.id = SequencerCommandId::SetStep;
                c.a  = static_cast<uint8_t>(sr);
                c.b  = static_cast<uint8_t>(ss);
                c.c  = static_cast<uint8_t>(rollBlackKeyMidi(index));
                c.d  = sd.velocity;
                c.e  = sd.gate;
                c.f  = static_cast<uint8_t>(editBar_);
                proc_.controller().postCommand(c);
                if (recArmed_)
                    selectedStep_ = (ss + 1) % n;   // step-record : avance le curseur
            }
            break;
        case PatternScreenModel::Page::Auto:
            if (index == 0)      autoSlot_ = juce::jmax(0, autoSlot_ - 1);
            else if (index == 1) autoSlot_ = juce::jmin(7, autoSlot_ + 1);
            else if (index == 2) stepPage_ = juce::jmax(0, stepPage_ - 1);                 // Page-
            else if (index == 3) stepPage_ = juce::jmin(stepPageCount() - 1, stepPage_ + 1); // Page+ (adaptatif)
            break;
        case PatternScreenModel::Page::Harmony: {
            // Noires 0..10 = bascule d'un bit d'extension (11 bits du core). 7e/5-alt mutuellement
            // exclusives par groupe. XOR du bit sur les extensions du slot courant, puis réémission
            // de SetChordSlot (split ext sur c.d|c.len ; extensions est un uint16).
            if (index < 0 || index > 10)
                break;
            static const uint16_t kBlackExtBit[11] = {
                kExtFlat7, kExtMaj7, kExtFlat5, kExtSharp5,
                kExt9, kExt11, kExt13, kExtFlat9, kExtSharp9, kExtSharp11, kExtFlat13};
            const auto& prog = proc_.engine().pattern().chordProgression;
            const int   len  = juce::jlimit(0, 32, static_cast<int>(prog.len));
            const int   cur  = juce::jlimit(0, 31, harmonyCursor_);
            // Slot d'ajout (cur >= len) : on crée d'abord le slot (seed via setChordField, qui
            // copie le dernier slot), puis on rebascule l'extension sur le slot fraîchement créé.
            int deg = 1, qual = 0, bass = 0, dur = 1;
            uint16_t ext = 0;
            if (cur < len) {
                const auto& cs = prog.slots[static_cast<size_t>(cur)];
                deg = cs.degree; qual = static_cast<int>(cs.quality); ext = cs.extensions;
                bass = cs.bassOffset; dur = cs.durationBeats;
            } else {
                setChordField(0, (len > 0) ? prog.slots[static_cast<size_t>(len - 1)].degree : 1);
                const auto& prog2 = proc_.engine().pattern().chordProgression;
                if (cur < static_cast<int>(prog2.len)) {
                    const auto& cs = prog2.slots[static_cast<size_t>(cur)];
                    deg = cs.degree; qual = static_cast<int>(cs.quality); ext = cs.extensions;
                    bass = cs.bassOffset; dur = cs.durationBeats;
                }
            }
            const uint16_t bit = kBlackExtBit[index];
            ext ^= bit;   // toggle du bit
            // Exclusivité de groupe : si on vient d'ACTIVER un bit, masque l'autre membre du groupe.
            if (ext & bit) {
                if      (bit == kExtFlat7) ext &= ~kExtMaj7;   // 7e : b7 ↔ 7♮
                else if (bit == kExtMaj7)  ext &= ~kExtFlat7;
                else if (bit == kExtFlat5) ext &= ~kExtSharp5; // 5-alt : b5 ↔ #5
                else if (bit == kExtSharp5) ext &= ~kExtFlat5;
            }
            SequencerCommand c;
            c.id = SequencerCommandId::SetChordSlot;
            c.a  = static_cast<uint8_t>(cur);
            c.b  = static_cast<uint8_t>(deg);
            c.c  = static_cast<uint8_t>(qual);
            c.d  = static_cast<uint8_t>(ext & 0xFF);            // octet bas
            c.len = static_cast<uint8_t>((ext >> 8) & 0xFF);    // octet haut (uint16)
            c.e  = static_cast<uint8_t>(static_cast<int8_t>(bass));
            c.f  = static_cast<uint8_t>(dur);
            proc_.controller().postCommand(c);
            break;
        }
        case PatternScreenModel::Page::Global:
            // R-/R+ = change la row sélectionnée (pour éditer son canal MIDI dans la liste GLOBAL).
            if (index == 0 || index == 1) runFunction(index, /*allowOct*/ false);
            break;
        case PatternScreenModel::Page::Song:
        default:
            break;
    }
    applyEncoderConfigForState();
    buildScreenModel();
}
