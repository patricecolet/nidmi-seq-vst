#include "PluginEditor.h"
#include "PluginProcessor.h"

#include "MidiExporter.h"

#include <nidmi_seq/ScaleBank.h>

#include <cmath>

namespace {
constexpr const char* kOledParamIds[] = {"bpm",        "numSteps",   "numRows",    "tsNum",
                                         "tsDen",      "loop",       "useMidiClock", "followHost",
                                         "useHostBpm"};
constexpr const char* kOledTitles[]   = {"BPM",        "Pas/mesure", "Rangees",    "Mes. num.",
                                       "Mes. den.",  "Boucle",     "Clk MIDI",   "Suiv. hote",
                                       "BPM hote"};
static_assert(std::size(kOledParamIds) == std::size(kOledTitles));
constexpr int kNumOledParams = static_cast<int>(std::size(kOledParamIds));

// Page HARMONIE : édition des extensions par liste de bits unitaires (none/9/11/13/b9/#9/#11/b13).
constexpr uint8_t kHarmExtBits[8] = {0, 1u << 0, 1u << 1, 1u << 2, 1u << 3, 1u << 4, 1u << 5, 1u << 6};
int harmExtToIndex(int bits) {
    for (int i = 1; i < 8; ++i)
        if (bits == kHarmExtBits[i]) return i;
    return 0;  // none, ou combinaison non listée
}
const char* kRoman[7]         = {"I", "II", "III", "IV", "V", "VI", "VII"};
const char* kHarmExtName[8]   = {"-", "9", "11", "13", "b9", "#9", "#11", "b13"};
const char* kQualName[12]     = {"maj", "m", "dim", "aug", "7", "maj7",
                                 "m7", "mM7", "m7b5", "dim7", "sus2", "sus4"};

// Type de division musicale donné par N pas dans une mesure tsNum/tsDen.
// Ex. 4/4 : N=16 -> "1/16", N=12 -> "1/8T", N=20 -> "5:tps". Vide si non interprétable.
juce::String divisionLabel(int n, int tsNum, int tsDen) {
    if (n <= 0 || tsNum <= 0 || tsDen <= 0 || (n % tsNum) != 0)
        return {};
    const int spb = n / tsNum;                       // pas par temps
    auto isPow2 = [](int x) { return x > 0 && (x & (x - 1)) == 0; };
    if (isPow2(spb))
        return "1/" + juce::String(tsDen * spb);     // division binaire droite
    if ((spb % 3) == 0 && isPow2((spb / 3) * 2))     // spb = 1.5 × puissance de 2 -> triolet
        return "1/" + juce::String(tsDen * (spb / 3) * 2) + "T";
    return juce::String(spb) + ":tps";               // tuplet irrégulier (quintolet, septolet…)
}
}  // namespace

int NidmiSeqAudioProcessorEditor::numOledParams() {
    return kNumOledParams;
}

const char* NidmiSeqAudioProcessorEditor::oledParamId(int index) {
    return kOledParamIds[juce::jlimit(0, kNumOledParams - 1, index)];
}

const char* NidmiSeqAudioProcessorEditor::oledParamTitle(int index) {
    return kOledTitles[juce::jlimit(0, kNumOledParams - 1, index)];
}

NidmiSeqAudioProcessorEditor::NidmiSeqAudioProcessorEditor(NidmiSeqAudioProcessor& p)
    : AudioProcessorEditor(&p)
    , proc_(p) {
    setResizeLimits(620, 560, 2000, 1100);

    transportLook_.setKind(HardwareButtonLook::Kind::Transport);
    playBtn_.setLookAndFeel(&transportLook_);
    stopBtn_.setLookAndFeel(&transportLook_);
    recBtn_.setLookAndFeel(&transportLook_);
    vueBtn_.setLookAndFeel(&transportLook_);

    playBtn_.setButtonText("Play");
    playBtn_.onClick = [this] {
        SequencerCommand c;
        c.id = SequencerCommandId::Play;
        proc_.controller().postCommand(c);
    };

    stopBtn_.setButtonText("Stop");
    stopBtn_.onClick = [this] {
        SequencerCommand c;
        c.id = SequencerCommandId::Stop;
        proc_.controller().postCommand(c);
    };

    recBtn_.setButtonText("Rec");
    recBtn_.onClick = [this] { recArmed_ = !recArmed_; };

    vueBtn_.setButtonText("Vue");
    vueBtn_.onClick = [this] {
        // Cycle des Vues de l'écran TFT (cahier §10.1).
        setScreenPage((static_cast<int>(screenPage_) + 1) % PatternScreenModel::kNumPages);
    };

    exportBtn_.setLookAndFeel(&transportLook_);
    exportBtn_.setButtonText("Export");
    exportBtn_.onClick = [this] { launchExportFlow(); };

    // Octave du clavier ROLL (actifs seulement sur l'onglet PIANO ROLL, cf. updateKeysForPage()).
    octMinusBtn_.setLookAndFeel(&transportLook_);
    octMinusBtn_.setButtonText("Oct-");
    octMinusBtn_.setEnabled(false);
    octMinusBtn_.onClick = [this] { shiftKeyboardOctave(-1); };

    octPlusBtn_.setLookAndFeel(&transportLook_);
    octPlusBtn_.setButtonText("Oct+");
    octPlusBtn_.setEnabled(false);
    octPlusBtn_.onClick = [this] { shiftKeyboardOctave(+1); };

    // Mute toggle pour la row sélectionnée. État sync'd depuis l'engine en timerCallback.
    muteBtn_.setLookAndFeel(&transportLook_);
    muteBtn_.setButtonText("Mute");
    muteBtn_.setToggleable(true);
    muteBtn_.setClickingTogglesState(false);
    muteBtn_.onClick = [this] {
        const auto& pat = proc_.engine().pattern();
        if (selectedRow_ < 0 || selectedRow_ >= static_cast<int>(pat.numRows))
            return;
        SequencerCommand c;
        c.id = SequencerCommandId::SetRowMuted;
        c.a  = static_cast<uint8_t>(selectedRow_);
        c.x  = !pat.rows[static_cast<size_t>(selectedRow_)].muted;
        proc_.controller().postCommand(c);
    };

    auto setupEncoder = [](juce::Slider& s) {
        s.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        s.setRotaryParameters(juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f,
                              true);
        s.setMouseDragSensitivity(180);
        s.setScrollWheelEnabled(false);
    };

    setupEncoder(navEncoder_);
    navEncoder_.onValueChange = [this] { onNavEncoderChanged(); };
    setupEncoder(valueEncoder_);
    valueEncoder_.onValueChange = [this] { onValueEncoderChanged(); };

    setupEncoder(veloEncoder_);
    veloEncoder_.setRange(0.0, 127.0, 1.0);
    veloEncoder_.onDragStart   = [this] { configureVeloEncoder(); };  // cale Vélo/Gate selon Shift
    veloEncoder_.onValueChange = [this] { onVeloEncoderChanged(); };

    setupEncoder(zoomEncoder_);
    zoomEncoder_.setRange(0.0, 1000.0, 1.0);   // traité en relatif (accumulation)
    zoomEncoder_.setMouseDragSensitivity(1200); // moins twitchy
    zoomEncoder_.setValue(500.0, juce::dontSendNotification);
    lastZoomEnc_ = 500.0;
    zoomEncoder_.onValueChange = [this] { onZoomEncoderChanged(); };

    applyEncoderConfigForState();

    auto labelStyle = [](juce::Label& l, const juce::String& t) {
        l.setText(t, juce::dontSendNotification);
        l.setJustificationType(juce::Justification::centred);
        l.setFont(juce::Font(juce::FontOptions().withHeight(12.0f)));
    };
    labelStyle(navEncoderLabel_, "Curseur");
    labelStyle(valueEncoderLabel_, "Valeur");
    labelStyle(veloEncoderLabel_, "Vélo");
    labelStyle(zoomEncoderLabel_, "Zoom");

    // Cahier §10.3 : les touches sont le miroir de l'onglet actif → handlers dispatchés par page.
    for (int i = 0; i < 16; ++i)
        piano_.setWhiteKeyClick(i, [this, i] { onWhiteKey(i); });
    for (int k = 0; k < 11; ++k)
        piano_.setBlackKeyClick(k, [this, k] { onBlackKey(k); });

    // Page PATTERN (écran TFT émulé) : clic = sélection row + toggle pas.
    screen_.onRowSelected = [this](int row) {
        const auto& pat = proc_.engine().pattern();
        selectedRow_ = juce::jlimit(0, juce::jmax(0, static_cast<int>(pat.numRows) - 1), row);
    };
    screen_.onTabSelected = [this](int tab) { setScreenPage(tab); };
    screen_.onHarmonySlot = [this](int slot) {
        harmonyCursor_ = juce::jlimit(0, 31, slot);
        applyEncoderConfigForState();
        buildScreenModel();
    };
    screen_.onHarmonyField = [this](int field) {
        harmonyField_ = juce::jlimit(0, 4, field);
        applyEncoderConfigForState();
        buildScreenModel();
    };
    screen_.onAutoSlot = [this](int slot) {
        autoSlot_ = juce::jlimit(0, 7, slot);
        applyEncoderConfigForState();
        buildScreenModel();
    };
    screen_.onAutoField = [this](int field) {
        autoField_ = juce::jlimit(0, 1, field);
        applyEncoderConfigForState();
        buildScreenModel();
    };
    screen_.onAutoValueSet = [this](int step, int value) {
        selectedStep_ = step;
        postAutoValueAt(step, value);
    };
    screen_.onRollLaneValue = [this](int step, int value) {
        selectedStep_ = step;
        setStepField(step, 1, value);   // la lane édite la vélocité
    };
    screen_.onNoteSet = [this](int row, int step, int note) {
        const auto& pat = proc_.engine().pattern();
        if (row < 0 || row >= static_cast<int>(pat.numRows))
            return;
        if (step < 0 || step >= static_cast<int>(pat.rows[static_cast<size_t>(row)].numSteps))
            return;
        const auto& sd = pat.rows[static_cast<size_t>(row)].steps[static_cast<size_t>(step)];
        selectedRow_  = row;
        selectedStep_ = step;
        SequencerCommand c;
        c.id = SequencerCommandId::SetStep;   // pose la hauteur + active le pas
        c.a  = static_cast<uint8_t>(row);
        c.b  = static_cast<uint8_t>(step);
        c.c  = static_cast<uint8_t>(juce::jlimit(0, 127, note));
        c.d  = sd.velocity;
        c.e  = sd.gate;
        proc_.controller().postCommand(c);
    };
    screen_.onStepToggled = [this](int row, int step) {
        const auto& pat = proc_.engine().pattern();
        if (row < 0 || row >= static_cast<int>(pat.numRows))
            return;
        if (step < 0 || step >= static_cast<int>(pat.rows[static_cast<size_t>(row)].numSteps))
            return;
        selectedRow_  = row;
        selectedStep_ = step;
        SequencerCommand c;
        c.id = SequencerCommandId::ToggleStep;
        c.a  = static_cast<uint8_t>(row);
        c.b  = static_cast<uint8_t>(step);
        proc_.controller().postCommand(c);
    };

    // Ordre d’empilement : arrière-plan → premier plan (transport au-dessus).
    addAndMakeVisible(piano_);
    addAndMakeVisible(screen_);
    addAndMakeVisible(navEncoderLabel_);
    addAndMakeVisible(navEncoder_);
    addAndMakeVisible(valueEncoderLabel_);
    addAndMakeVisible(valueEncoder_);
    addAndMakeVisible(veloEncoderLabel_);
    addAndMakeVisible(veloEncoder_);
    addAndMakeVisible(zoomEncoderLabel_);
    addAndMakeVisible(zoomEncoder_);
    addAndMakeVisible(playBtn_);
    addAndMakeVisible(stopBtn_);
    addAndMakeVisible(recBtn_);
    addAndMakeVisible(vueBtn_);
    addAndMakeVisible(exportBtn_);
    addAndMakeVisible(octMinusBtn_);
    addAndMakeVisible(octPlusBtn_);
    addAndMakeVisible(muteBtn_);

    applyEncoderConfigForState();
    updateKeysForPage();
    buildScreenModel();

    // Taille après tous les enfants : le premier `resized()` doit voir toute l’UI.
    setSize(1000, 700);

    piano_.toFront(false);
    screen_.toFront(false);
    navEncoderLabel_.toFront(false);
    navEncoder_.toFront(false);
    valueEncoderLabel_.toFront(false);
    valueEncoder_.toFront(false);
    veloEncoderLabel_.toFront(false);
    veloEncoder_.toFront(false);
    zoomEncoderLabel_.toFront(false);
    zoomEncoder_.toFront(false);
    playBtn_.toFront(false);
    stopBtn_.toFront(false);
    recBtn_.toFront(false);
    vueBtn_.toFront(false);
    exportBtn_.toFront(false);
    octMinusBtn_.toFront(false);
    octPlusBtn_.toFront(false);
    muteBtn_.toFront(false);

    startTimerHz(24);  // playhead lisible jusqu'a ~180 BPM avec 16 pas/mesure
}

NidmiSeqAudioProcessorEditor::~NidmiSeqAudioProcessorEditor() {
    stopTimer();
    playBtn_.setLookAndFeel(nullptr);
    stopBtn_.setLookAndFeel(nullptr);
    recBtn_.setLookAndFeel(nullptr);
    vueBtn_.setLookAndFeel(nullptr);
    exportBtn_.setLookAndFeel(nullptr);
    octMinusBtn_.setLookAndFeel(nullptr);
    octPlusBtn_.setLookAndFeel(nullptr);
    muteBtn_.setLookAndFeel(nullptr);
}

void NidmiSeqAudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void NidmiSeqAudioProcessorEditor::resized() {
    auto r = getLocalBounds().reduced(8);

    // Au-dessus de l’écran : Play, Stop, Rec, Vue, Export (5 boutons, cahier §11.3).
    {
        auto        row = r.removeFromTop(42);
        const int   bw  = 92;
        const int   gap = 6;
        const int   totalW = bw * 5 + gap * 4;
        row.removeFromLeft(juce::jmax(0, (row.getWidth() - totalW) / 2));
        playBtn_.setBounds(row.removeFromLeft(bw));
        row.removeFromLeft(gap);
        stopBtn_.setBounds(row.removeFromLeft(bw));
        row.removeFromLeft(gap);
        recBtn_.setBounds(row.removeFromLeft(bw));
        row.removeFromLeft(gap);
        vueBtn_.setBounds(row.removeFromLeft(bw));
        row.removeFromLeft(gap);
        exportBtn_.setBounds(row.removeFromLeft(bw));
    }

    // Écran TFT émulé (page PATTERN) au centre, encodeurs sur les côtés.
    // Cahier §10.1 (rév. 2026-05) : l'écran montre la grille ; encodeurs + touches pilotent.
    {
        const int sideW  = 118;
        const int blockH = juce::jmax(160, r.getHeight() - 220);
        auto      block  = r.removeFromTop(blockH);

        // 4 encodeurs (cahier §10.2 / §11.3) : gauche = Valeur + Vélo, droite = Curseur + Zoom.
        auto placeKnob = [](juce::Rectangle<int> col, juce::Label& lab, juce::Slider& knob) {
            lab.setBounds(col.removeFromTop(15));
            auto k = col.reduced(2, 2);
            const int d = juce::jmin(k.getWidth(), k.getHeight());
            knob.setBounds(k.withSizeKeepingCentre(d, d));
        };
        const int halfH = block.getHeight() / 2;

        auto leftCol = block.removeFromLeft(sideW);
        placeKnob(leftCol.removeFromTop(halfH), valueEncoderLabel_, valueEncoder_);
        placeKnob(leftCol, veloEncoderLabel_, veloEncoder_);

        auto rightCol = block.removeFromRight(sideW);
        placeKnob(rightCol.removeFromTop(halfH), navEncoderLabel_, navEncoder_);
        placeKnob(rightCol, zoomEncoderLabel_, zoomEncoder_);

        screen_.setBounds(block.reduced(6, 2));
    }

    // Rangée « contrôles row + pitch » : Mute (row sélectionnée) + Oct-/Oct+ (overlay PITCH). 32 px.
    {
        auto      row = r.removeFromTop(32);
        const int bw  = 70;
        const int gap = 6;
        const int totalW = bw * 3 + gap * 2;
        row.removeFromLeft(juce::jmax(0, (row.getWidth() - totalW) / 2));
        muteBtn_.setBounds(row.removeFromLeft(bw).reduced(0, 2));
        row.removeFromLeft(gap);
        octMinusBtn_.setBounds(row.removeFromLeft(bw).reduced(0, 2));
        row.removeFromLeft(gap);
        octPlusBtn_.setBounds(row.removeFromLeft(bw).reduced(0, 2));
    }

    piano_.setBounds(r);
}

void NidmiSeqAudioProcessorEditor::syncValueEncoderFromParam() {
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
    auto&       ap    = proc_.apvts();
    const char* id    = oledParamId(oledParamIndex_);
    auto*       param = ap.getParameter(id);
    auto*       rp    = dynamic_cast<juce::RangedAudioParameter*>(param);
    if (rp == nullptr)
        return;

    const float norm = rp->convertTo0to1(static_cast<float>(valueEncoder_.getValue()));
    rp->setValueNotifyingHost(norm);
}

void NidmiSeqAudioProcessorEditor::setScreenPage(int pageIndex) {
    pageIndex   = juce::jlimit(0, PatternScreenModel::kNumPages - 1, pageIndex);
    screenPage_ = static_cast<PatternScreenModel::Page>(pageIndex);
    applyEncoderConfigForState();
    updateKeysForPage();
    buildScreenModel();
}

int NidmiSeqAudioProcessorEditor::stepPageCount() const {
    const auto& pat = proc_.engine().pattern();
    if (pat.numRows == 0)
        return 1;
    const int sr = juce::jlimit(0, static_cast<int>(pat.numRows) - 1, selectedRow_);
    const int n  = juce::jmax(1, static_cast<int>(pat.rows[static_cast<size_t>(sr)].numSteps));
    return juce::jlimit(1, 4, (n + 15) / 16);   // pages de 16 pas (P1..P4)
}

void NidmiSeqAudioProcessorEditor::onNavEncoderChanged() {
    if (screenPage_ == PatternScreenModel::Page::Global) {
        const int ni = juce::jlimit(0, kNumOledParams - 1, (int) std::lround(navEncoder_.getValue()));
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
        // HARMONIE : l'encodeur curseur sélectionne le slot (jusqu'à len = slot d'ajout).
        const int len = juce::jlimit(0, 32, static_cast<int>(proc_.engine().pattern().chordProgression.len));
        const int cur = juce::jlimit(0, juce::jmin(31, len), (int) std::lround(navEncoder_.getValue()));
        if (cur != harmonyCursor_) {
            harmonyCursor_ = cur;
            applyEncoderConfigForState();  // recharge Enc1 avec le champ du nouveau slot
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
    } else {
        return;  // onglets en ossature : encodeurs inertes pour l'instant.
    }
    buildScreenModel();
}

void NidmiSeqAudioProcessorEditor::onValueEncoderChanged() {
    if (screenPage_ == PatternScreenModel::Page::Global) {
        applyValueEncoderToParam();
        return;
    }
    if (screenPage_ == PatternScreenModel::Page::Pattern) {
        // PATTERN : Enc1 règle le N (tuplet) de la row sélectionnée → re-subdivision live.
        const auto& pat = proc_.engine().pattern();
        if (selectedRow_ < 0 || selectedRow_ >= static_cast<int>(pat.numRows))
            return;
        const int n = juce::jlimit(1, 64, (int) std::lround(valueEncoder_.getValue()));
        if (n == static_cast<int>(pat.rows[static_cast<size_t>(selectedRow_)].numSteps))
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
        // PIANO ROLL : Enc1 = Note du pas sous le curseur (vélo = Enc3, gate = Shift+Enc3).
        const auto& pat = proc_.engine().pattern();
        const int   nr  = juce::jmax(1, static_cast<int>(pat.numRows));
        const int   sr  = juce::jlimit(0, nr - 1, selectedRow_);
        const int   n   = juce::jlimit(1, 64, static_cast<int>(pat.rows[static_cast<size_t>(sr)].numSteps));
        const int   ss  = juce::jlimit(0, n - 1, selectedStep_);
        setStepField(ss, 0, (int) std::lround(valueEncoder_.getValue()));
        return;
    }
    if (screenPage_ == PatternScreenModel::Page::Harmony) {
        postChordSlotEdit((int) std::lround(valueEncoder_.getValue()));
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
    // onglets en ossature : pas d'édition via Enc1 pour l'instant.
}

void NidmiSeqAudioProcessorEditor::postChordSlotEdit(int newFieldValue) {
    const auto& prog = proc_.engine().pattern().chordProgression;
    const int   len  = juce::jlimit(0, 32, static_cast<int>(prog.len));
    const int   cur  = juce::jlimit(0, 31, harmonyCursor_);

    // Lit le slot existant, ou des valeurs par défaut s'il s'agit d'un ajout.
    int deg = 1, qual = 0, ext = 0, bass = 0, dur = 1;
    if (cur < len) {
        const auto& cs = prog.slots[static_cast<size_t>(cur)];
        deg = cs.degree; qual = static_cast<int>(cs.quality); ext = cs.extensions;
        bass = cs.bassOffset; dur = cs.durationSlots;
    }
    switch (harmonyField_) {
        case 0: deg  = juce::jlimit(1, 7, newFieldValue); break;
        case 1: qual = juce::jlimit(0, 11, newFieldValue); break;
        case 2: ext  = kHarmExtBits[juce::jlimit(0, 7, newFieldValue)]; break;
        case 3: bass = juce::jlimit(-12, 12, newFieldValue); break;
        default: dur = juce::jlimit(1, 16, newFieldValue); break;
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
    c.d  = static_cast<uint8_t>(ext);
    c.e  = static_cast<uint8_t>(static_cast<int8_t>(bass));
    c.f  = static_cast<uint8_t>(dur);
    proc_.controller().postCommand(c);
    buildScreenModel();
}

void NidmiSeqAudioProcessorEditor::setStepField(int step, int field, int value) {
    const auto& pat = proc_.engine().pattern();
    if (pat.numRows == 0)
        return;
    const int sr = juce::jlimit(0, static_cast<int>(pat.numRows) - 1, selectedRow_);
    const int n  = juce::jlimit(1, 64, static_cast<int>(pat.rows[static_cast<size_t>(sr)].numSteps));
    if (step < 0 || step >= n)
        return;
    const auto& sd = pat.rows[static_cast<size_t>(sr)].steps[static_cast<size_t>(step)];
    int note = sd.note, vel = sd.velocity, gate = sd.gate;
    switch (field) {
        case 0:  note = juce::jlimit(0, 127, value); break;   // Note
        case 1:  vel  = juce::jlimit(0, 127, value); break;   // Vélo
        default: gate = juce::jlimit(1, 100, value); break;   // Gate (%)
    }
    SequencerCommand c;
    c.id = SequencerCommandId::SetStep;
    c.a  = static_cast<uint8_t>(sr);
    c.b  = static_cast<uint8_t>(step);
    c.c  = static_cast<uint8_t>(note);
    c.d  = static_cast<uint8_t>(vel);
    c.e  = static_cast<uint8_t>(gate);
    proc_.controller().postCommand(c);
    buildScreenModel();
}

int NidmiSeqAudioProcessorEditor::effectiveAutoCc() const {
    const auto& pat = proc_.engine().pattern();
    if (pat.numRows == 0)
        return autoCcDefault_;
    const int   ar   = juce::jlimit(0, static_cast<int>(pat.numRows) - 1, selectedRow_);
    const auto& arow = pat.rows[static_cast<size_t>(ar)];
    const int   an   = juce::jlimit(1, 64, static_cast<int>(arow.numSteps));
    const int   slot = juce::jlimit(0, 7, autoSlot_);
    for (int s = 0; s < an; ++s) {
        const auto& lk = arow.steps[static_cast<size_t>(s)].ccLocks[static_cast<size_t>(slot)];
        if (lk.ccNumber != 0xFF)
            return lk.ccNumber;
    }
    return autoCcDefault_;
}

void NidmiSeqAudioProcessorEditor::postAutoValueAt(int step, int value) {
    const auto& pat = proc_.engine().pattern();
    if (pat.numRows == 0)
        return;
    const int ar = juce::jlimit(0, static_cast<int>(pat.numRows) - 1, selectedRow_);
    const int an = juce::jlimit(1, 64, static_cast<int>(pat.rows[static_cast<size_t>(ar)].numSteps));
    if (step < 0 || step >= an)
        return;
    SequencerCommand c;
    c.id = SequencerCommandId::SetStepCCLock;
    c.a  = static_cast<uint8_t>(ar);
    c.b  = static_cast<uint8_t>(step);
    c.c  = static_cast<uint8_t>(juce::jlimit(0, 7, autoSlot_));
    c.d  = static_cast<uint8_t>(juce::jlimit(0, 127, effectiveAutoCc()));
    c.e  = static_cast<uint8_t>(juce::jlimit(0, 127, value));
    proc_.controller().postCommand(c);
    buildScreenModel();
}

void NidmiSeqAudioProcessorEditor::postAutoCcNumber(int ccNumber) {
    autoCcDefault_ = juce::jlimit(0, 127, ccNumber);
    const auto& pat = proc_.engine().pattern();
    if (pat.numRows > 0) {
        // Réémet les P-locks existants du slot actif avec le nouveau numéro de CC.
        const int   ar   = juce::jlimit(0, static_cast<int>(pat.numRows) - 1, selectedRow_);
        const auto& arow = pat.rows[static_cast<size_t>(ar)];
        const int   an   = juce::jlimit(1, 64, static_cast<int>(arow.numSteps));
        const int   slot = juce::jlimit(0, 7, autoSlot_);
        for (int s = 0; s < an; ++s) {
            const auto& lk = arow.steps[static_cast<size_t>(s)].ccLocks[static_cast<size_t>(slot)];
            if (lk.ccNumber == 0xFF)
                continue;
            SequencerCommand c;
            c.id = SequencerCommandId::SetStepCCLock;
            c.a  = static_cast<uint8_t>(ar);
            c.b  = static_cast<uint8_t>(s);
            c.c  = static_cast<uint8_t>(slot);
            c.d  = static_cast<uint8_t>(autoCcDefault_);
            c.e  = lk.value;
            proc_.controller().postCommand(c);
        }
    }
    applyEncoderConfigForState();
    buildScreenModel();
}

void NidmiSeqAudioProcessorEditor::applyEncoderConfigForState() {
    if (screenPage_ == PatternScreenModel::Page::Global) {
        navEncoderLabel_.setText("Param", juce::dontSendNotification);
        valueEncoderLabel_.setText("Valeur", juce::dontSendNotification);
        navEncoder_.setRange(0.0, static_cast<double>(juce::jmax(1, kNumOledParams - 1)), 1.0);
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
        valueEncoderLabel_.setText("N " + juce::String(n) + (div.isEmpty() ? juce::String() : " " + div),
                                   juce::dontSendNotification);
        navEncoder_.setRange(0.0, static_cast<double>(juce::jmax(1, n - 1)), 1.0);
        navEncoder_.setValue(static_cast<double>(ss), juce::dontSendNotification);
        valueEncoder_.setRange(1.0, 64.0, 1.0);
        valueEncoder_.setValue(static_cast<double>(n), juce::dontSendNotification);
    } else if (screenPage_ == PatternScreenModel::Page::PianoRoll) {
        const auto& pat = proc_.engine().pattern();
        const int   nr  = juce::jmax(1, static_cast<int>(pat.numRows));
        const int   sr  = juce::jlimit(0, nr - 1, selectedRow_);
        const int   n   = juce::jlimit(1, 64, static_cast<int>(pat.rows[static_cast<size_t>(sr)].numSteps));
        const int   ss  = juce::jlimit(0, n - 1, selectedStep_);
        const auto& sd  = pat.rows[static_cast<size_t>(sr)].steps[static_cast<size_t>(ss)];
        navEncoderLabel_.setText("Pas " + juce::String(ss + 1), juce::dontSendNotification);
        navEncoder_.setRange(0.0, static_cast<double>(juce::jmax(1, n - 1)), 1.0);
        navEncoder_.setValue(static_cast<double>(ss), juce::dontSendNotification);
        // Enc1 = Note (vélo sur Enc3, gate sur Shift+Enc3).
        valueEncoderLabel_.setText("Note " + noteNameFromMidi(sd.note), juce::dontSendNotification);
        valueEncoder_.setRange(0.0, 127.0, 1.0);
        valueEncoder_.setValue(static_cast<double>(sd.note), juce::dontSendNotification);
    } else if (screenPage_ == PatternScreenModel::Page::Harmony) {
        const auto& prog = proc_.engine().pattern().chordProgression;
        const int   len  = juce::jlimit(0, 32, static_cast<int>(prog.len));
        const int   cur  = juce::jlimit(0, juce::jmin(31, len), harmonyCursor_);  // == len = slot d'ajout
        int deg = 1, qual = 0, ext = 0, bass = 0, dur = 1;
        if (cur < len) {
            const auto& cs = prog.slots[static_cast<size_t>(cur)];
            deg = cs.degree; qual = static_cast<int>(cs.quality); ext = cs.extensions;
            bass = cs.bassOffset; dur = cs.durationSlots;
        }
        juce::String vlab;
        switch (harmonyField_) {
            case 0: vlab = "Degré " + juce::String(kRoman[juce::jlimit(0, 6, deg - 1)]); break;
            case 1: vlab = "Qual " + juce::String(kQualName[juce::jlimit(0, 11, qual)]); break;
            case 2: vlab = "Ext " + juce::String(kHarmExtName[juce::jlimit(0, 7, harmExtToIndex(ext))]); break;
            case 3: vlab = "Bass " + juce::String(bass); break;
            default: vlab = "Durée " + juce::String(dur); break;
        }
        navEncoderLabel_.setText("Slot " + juce::String(cur + 1), juce::dontSendNotification);
        valueEncoderLabel_.setText(vlab, juce::dontSendNotification);
        navEncoder_.setRange(0.0, static_cast<double>(juce::jmax(1, len)), 1.0);
        navEncoder_.setValue(static_cast<double>(cur), juce::dontSendNotification);
        switch (harmonyField_) {
            case 0: valueEncoder_.setRange(1.0, 7.0, 1.0);    valueEncoder_.setValue(deg,  juce::dontSendNotification); break;
            case 1: valueEncoder_.setRange(0.0, 11.0, 1.0);   valueEncoder_.setValue(qual, juce::dontSendNotification); break;
            case 2: valueEncoder_.setRange(0.0, 7.0, 1.0);    valueEncoder_.setValue(harmExtToIndex(ext), juce::dontSendNotification); break;
            case 3: valueEncoder_.setRange(-12.0, 12.0, 1.0); valueEncoder_.setValue(bass, juce::dontSendNotification); break;
            default: valueEncoder_.setRange(1.0, 16.0, 1.0);  valueEncoder_.setValue(dur,  juce::dontSendNotification); break;
        }
    } else if (screenPage_ == PatternScreenModel::Page::Auto) {
        const auto& pat = proc_.engine().pattern();
        const int   nr  = juce::jmax(1, static_cast<int>(pat.numRows));
        const int   ar  = juce::jlimit(0, nr - 1, selectedRow_);
        const int   an  = juce::jlimit(1, 64, static_cast<int>(pat.rows[static_cast<size_t>(ar)].numSteps));
        if (autoField_ == 0) {
            const int   ss = juce::jlimit(0, an - 1, selectedStep_);
            const auto& lk = pat.rows[static_cast<size_t>(ar)].steps[static_cast<size_t>(ss)]
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
            valueEncoderLabel_.setText("CC " + juce::String(effectiveAutoCc()), juce::dontSendNotification);
            navEncoder_.setRange(0.0, 7.0, 1.0);
            navEncoder_.setValue(static_cast<double>(juce::jlimit(0, 7, autoSlot_)), juce::dontSendNotification);
            valueEncoder_.setRange(0.0, 127.0, 1.0);
            valueEncoder_.setValue(static_cast<double>(effectiveAutoCc()), juce::dontSendNotification);
        }
    } else {
        // Onglets en ossature : encodeurs inertes, libellés neutres.
        navEncoderLabel_.setText(juce::CharPointer_UTF8("\xe2\x80\x94"), juce::dontSendNotification);   // —
        valueEncoderLabel_.setText(juce::CharPointer_UTF8("\xe2\x80\x94"), juce::dontSendNotification); // —
    }
}

void NidmiSeqAudioProcessorEditor::configureVeloEncoder() {
    // Cale Enc3 sur Vélo, ou Gate si Shift maintenu (émule le push de l'encodeur).
    const auto& pat = proc_.engine().pattern();
    if (pat.numRows == 0)
        return;
    const int sr = juce::jlimit(0, static_cast<int>(pat.numRows) - 1, selectedRow_);
    const int n  = juce::jlimit(1, 64, static_cast<int>(pat.rows[static_cast<size_t>(sr)].numSteps));
    const int ss = juce::jlimit(0, n - 1, selectedStep_);
    const auto& sd = pat.rows[static_cast<size_t>(sr)].steps[static_cast<size_t>(ss)];
    if (juce::ModifierKeys::getCurrentModifiers().isShiftDown()) {
        veloEncoder_.setRange(1.0, 100.0, 1.0);
        veloEncoder_.setValue(static_cast<double>(sd.gate), juce::dontSendNotification);
        veloEncoderLabel_.setText("Gate " + juce::String(sd.gate), juce::dontSendNotification);
    } else {
        veloEncoder_.setRange(0.0, 127.0, 1.0);
        veloEncoder_.setValue(static_cast<double>(sd.velocity), juce::dontSendNotification);
        veloEncoderLabel_.setText("Vélo " + juce::String(sd.velocity), juce::dontSendNotification);
    }
}

void NidmiSeqAudioProcessorEditor::onVeloEncoderChanged() {
    // Tourne = Vélo ; Shift+tourne = Gate (push émulé). Seulement sur un pas actif.
    const auto& pat = proc_.engine().pattern();
    if (pat.numRows == 0)
        return;
    const int sr = juce::jlimit(0, static_cast<int>(pat.numRows) - 1, selectedRow_);
    const int n  = juce::jlimit(1, 64, static_cast<int>(pat.rows[static_cast<size_t>(sr)].numSteps));
    const int ss = juce::jlimit(0, n - 1, selectedStep_);
    if (!pat.rows[static_cast<size_t>(sr)].steps[static_cast<size_t>(ss)].enabled)
        return;
    const bool gate = juce::ModifierKeys::getCurrentModifiers().isShiftDown();
    setStepField(ss, gate ? 2 : 1, (int) std::lround(veloEncoder_.getValue()));
}

void NidmiSeqAudioProcessorEditor::onZoomEncoderChanged() {
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
    } else {
        // PATTERN/AUTO : Enc4 = sélection de Row (remplace le zoom).
        const int nr = juce::jmax(1, static_cast<int>(proc_.engine().pattern().numRows));
        selectedRow_ = juce::jlimit(0, nr - 1, selectedRow_ + dir);
        applyEncoderConfigForState();
    }
    buildScreenModel();
}

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

void NidmiSeqAudioProcessorEditor::timerCallback() {
    refreshPianoKeysFromEngine();

    const bool midiClk =
        proc_.apvts().getRawParameterValue("useMidiClock") != nullptr &&
        proc_.apvts().getRawParameterValue("useMidiClock")->load() > 0.5f;

    const bool manualTransportOk = !midiClk;
    playBtn_.setEnabled(manualTransportOk);
    stopBtn_.setEnabled(manualTransportOk);
    recBtn_.setEnabled(true);
    recBtn_.setButtonText(recArmed_ ? "REC●" : "Rec");
    vueBtn_.setEnabled(true);

    // Oct-/Oct+ ne servent que sur l'onglet PIANO ROLL (octave du clavier).
    const bool roll = (screenPage_ == PatternScreenModel::Page::PianoRoll);
    octMinusBtn_.setEnabled(roll);
    octPlusBtn_.setEnabled(roll);

    const auto& pat = proc_.engine().pattern();
    if (selectedRow_ >= pat.numRows)
        selectedRow_ = juce::jmax(0, static_cast<int>(pat.numRows) - 1);
    if (selectedStep_ >= pat.numSteps)
        selectedStep_ = juce::jmax(0, static_cast<int>(pat.numSteps) - 1);

    const bool muted = selectedRow_ < pat.numRows
                       && pat.rows[static_cast<size_t>(selectedRow_)].muted;
    muteBtn_.setToggleState(muted, juce::dontSendNotification);
    muteBtn_.setButtonText(muted ? "Muted" : "Mute");

    if (!navEncoder_.isMouseButtonDown() && !valueEncoder_.isMouseButtonDown())
        applyEncoderConfigForState();
    if (!veloEncoder_.isMouseButtonDown())
        configureVeloEncoder();
    zoomEncoderLabel_.setText(
        screenPage_ == PatternScreenModel::Page::PianoRoll
            ? "Zoom " + juce::String(rollOctaves_) + "oct"
            : "Row " + juce::String(selectedRow_ + 1),
        juce::dontSendNotification);

    buildScreenModel();
}

void NidmiSeqAudioProcessorEditor::buildScreenModel() {
    const auto& pat = proc_.engine().pattern();
    const auto  st  = proc_.engine().state();
    const bool  playing = (st == SequencerEngine::State::PLAYING);

    PatternScreenModel m;
    m.page        = screenPage_;
    m.numRows     = juce::jlimit(0, 16, static_cast<int>(pat.numRows));
    m.selectedRow = juce::jlimit(0, juce::jmax(0, m.numRows - 1), selectedRow_);
    m.selectedStep = selectedStep_;
    m.recArmed    = recArmed_;
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
    m.playing     = playing;
    m.tsNum       = pat.numerator;
    m.tsDen       = pat.denominator;

    if (auto* bpm = proc_.apvts().getRawParameterValue("bpm"))
        m.bpm = bpm->load();

    // Page GLOBAL : liste des params projet (héritée de l'OLED, désormais rendue sur le TFT).
    m.numGlobalParams = juce::jlimit(0, 16, kNumOledParams);
    m.globalCursor    = juce::jlimit(0, juce::jmax(0, m.numGlobalParams - 1), oledParamIndex_);
    for (int i = 0; i < m.numGlobalParams; ++i) {
        m.global[static_cast<size_t>(i)].name  = oledParamTitle(i);
        m.global[static_cast<size_t>(i)].value = formatParamValue(proc_.apvts(), oledParamId(i));
    }

    for (int r = 0; r < m.numRows; ++r) {
        const auto& row = pat.rows[static_cast<size_t>(r)];
        auto&       dst = m.rows[static_cast<size_t>(r)];
        const int   n   = juce::jlimit(1, 64, static_cast<int>(row.numSteps));
        dst.numSteps    = n;
        dst.channel     = static_cast<int>(row.channel) + 1;
        dst.harmonyMode = static_cast<int>(row.harmonyMode);
        dst.muted       = row.muted;
        for (int s = 0; s < n; ++s) {
            const auto& sd = row.steps[static_cast<size_t>(s)];
            dst.enabled[static_cast<size_t>(s)]  = sd.enabled;
            dst.note[static_cast<size_t>(s)]     = sd.note;
            dst.velocity[static_cast<size_t>(s)] = sd.velocity;
            dst.gate[static_cast<size_t>(s)]     = sd.gate;
        }
        const uint8_t raw = proc_.engine().currentStepForRow(static_cast<uint8_t>(r));
        dst.playhead = (playing && raw != 0xFF && raw < n) ? static_cast<int>(raw) : -1;
    }

    // Page HARMONIE : progression d'accords du pattern.
    const auto& prog = pat.chordProgression;
    m.progLen       = juce::jlimit(0, 32, static_cast<int>(prog.len));
    m.progCurrent   = (playing && prog.len > 0) ? static_cast<int>(prog.idx) : -1;
    m.harmonyCursor = juce::jlimit(0, 31, harmonyCursor_);
    m.harmonyField  = juce::jlimit(0, 4, harmonyField_);
    for (int i = 0; i < m.progLen; ++i) {
        const auto& cs = prog.slots[static_cast<size_t>(i)];
        auto&       cv = m.chord[static_cast<size_t>(i)];
        cv.degree        = cs.degree;
        cv.quality       = static_cast<int>(cs.quality);
        cv.extensions    = cs.extensions;
        cv.bassOffset    = cs.bassOffset;
        cv.durationSlots = cs.durationSlots;
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
                const auto& lk = arow.steps[static_cast<size_t>(s)].ccLocks[static_cast<size_t>(i)];
                if (lk.ccNumber != 0xFF) { cc = lk.ccNumber; break; }
            }
            m.autoSlotCc[i] = cc;
        }
        int activeCc = -1;
        for (int s = 0; s < an; ++s) {
            const auto& lk = arow.steps[static_cast<size_t>(s)].ccLocks[static_cast<size_t>(m.autoSlot)];
            m.autoValue[s] = (lk.ccNumber != 0xFF) ? static_cast<int>(lk.value) : -1;
            if (activeCc < 0 && lk.ccNumber != 0xFF) activeCc = lk.ccNumber;
        }
        m.autoCc = (activeCc >= 0) ? activeCc : autoCcDefault_;
    }

    screen_.setModel(m);
}

void NidmiSeqAudioProcessorEditor::refreshPianoKeysFromEngine() {
    const auto& pat = proc_.engine().pattern();
    if (pat.numRows == 0)
        return;
    const int    sr      = juce::jlimit(0, static_cast<int>(pat.numRows) - 1, selectedRow_);
    const auto&  row     = pat.rows[static_cast<size_t>(sr)];
    const int    rowN    = juce::jmax<int>(1, row.numSteps);
    stepPage_            = juce::jlimit(0, stepPageCount() - 1, stepPage_);
    const int    base    = stepPage_ * 16;   // 1er pas de la fenêtre de page (touches)
    const bool   playing = (proc_.engine().state() == SequencerEngine::State::PLAYING);
    const uint8_t rawStep = proc_.engine().currentStepForRow(static_cast<uint8_t>(sr));
    // Playhead relatif à la fenêtre de page de 16 touches.
    const int    livePh  = (playing && rawStep != 0xFF && rawStep >= base && rawStep < base + 16
                            && rawStep < rowN) ? static_cast<int>(rawStep - base) : -1;

    auto clearWhites = [this] {
        for (int i = 0; i < 16; ++i)
            piano_.whiteKey(i).setToggleState(false, juce::dontSendNotification);
    };
    piano_.setBlackKeyHighlight(-1);

    switch (screenPage_) {
        case PatternScreenModel::Page::Pattern:
            for (int i = 0; i < 16; ++i) {
                const int s = base + i;
                piano_.whiteKey(i).setToggleState(s < rowN && row.steps[static_cast<size_t>(s)].enabled,
                                                  juce::dontSendNotification);
            }
            piano_.setPlayheadStep(livePh);
            break;

        case PatternScreenModel::Page::Auto: {
            const int slot = juce::jlimit(0, 7, autoSlot_);
            for (int i = 0; i < 16; ++i) {
                const int s = base + i;
                piano_.whiteKey(i).setToggleState(
                    s < rowN && row.steps[static_cast<size_t>(s)].ccLocks[static_cast<size_t>(slot)].ccNumber != 0xFF,
                    juce::dontSendNotification);
            }
            piano_.setPlayheadStep(livePh);
            break;
        }

        case PatternScreenModel::Page::PianoRoll: {
            clearWhites();
            const int ss   = juce::jlimit(0, rowN - 1, selectedStep_);
            const int note = row.steps[static_cast<size_t>(ss)].note;
            int hl = -1;
            for (int i = 0; i < 16; ++i)
                if (rollWhiteKeyMidi(i) == note) { hl = i; break; }
            piano_.setPlayheadStep(hl);
            break;
        }

        case PatternScreenModel::Page::Harmony: {
            clearWhites();
            int deg = -1;
            if (harmonyCursor_ < static_cast<int>(pat.chordProgression.len))
                deg = pat.chordProgression.slots[static_cast<size_t>(juce::jlimit(0, 31, harmonyCursor_))].degree;
            piano_.setPlayheadStep((deg >= 1 && deg <= 7) ? deg - 1 : -1);
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

void NidmiSeqAudioProcessorEditor::launchExportFlow() {
    // 1. Choix du mode via popup, 2. file chooser, 3. export + retour utilisateur.
    juce::PopupMenu menu;
    menu.addItem(1, "Bake (NoteOn/CC absolus)");
    menu.addItem(2, "Full (Bake + blob NiDMI)");

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&exportBtn_),
                       [this](int choice) {
        if (choice == 0)
            return;
        const auto mode = (choice == 2) ? MidiExporter::Mode::Full : MidiExporter::Mode::Bake;

        const auto defaultDir =
            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
        const juce::String suggested =
            (mode == MidiExporter::Mode::Full ? "NidmiSeq_full" : "NidmiSeq_bake")
            + juce::String(".mid");

        fileChooser_ = std::make_unique<juce::FileChooser>(
            "Exporter MIDI", defaultDir.getChildFile(suggested), "*.mid");

        const int flags =
            juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting;

        fileChooser_->launchAsync(flags, [this, mode](const juce::FileChooser& fc) {
            const auto dest = fc.getResult();
            if (dest == juce::File{})
                return;
            const auto withExt =
                dest.hasFileExtension(".mid") ? dest : dest.withFileExtension(".mid");
            const auto result = proc_.exportMidi(withExt, mode);

            juce::MessageBoxOptions opts =
                juce::MessageBoxOptions()
                    .withIconType(result.success ? juce::MessageBoxIconType::InfoIcon
                                                  : juce::MessageBoxIconType::WarningIcon)
                    .withTitle(result.success ? "Export OK" : "Export raté")
                    .withMessage(result.message)
                    .withButton("OK");
            juce::AlertWindow::showAsync(opts, nullptr);
        });
    });
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

void NidmiSeqAudioProcessorEditor::shiftKeyboardOctave(int delta) {
    keyboardOctave_ = juce::jlimit(-4, 4, keyboardOctave_ + delta);
    updateKeysForPage();
    buildScreenModel();   // la fenêtre de hauteurs (PIANO ROLL) défile aussitôt
}

void NidmiSeqAudioProcessorEditor::onWhiteKey(int index) {
    const auto& pat = proc_.engine().pattern();
    if (pat.numRows == 0)
        return;
    const int   sr  = juce::jlimit(0, static_cast<int>(pat.numRows) - 1, selectedRow_);
    const auto& row = pat.rows[static_cast<size_t>(sr)];
    const int   n   = juce::jmax(1, static_cast<int>(row.numSteps));

    switch (screenPage_) {
        case PatternScreenModel::Page::Pattern: {
            const int step = stepPage_ * 16 + index;   // fenêtre de page
            if (step >= n) return;          // PAD : toggle le pas
            selectedStep_ = step;
            SequencerCommand c;
            c.id = SequencerCommandId::ToggleStep;
            c.a  = static_cast<uint8_t>(sr);
            c.b  = static_cast<uint8_t>(step);
            proc_.controller().postCommand(c);
            break;
        }
        case PatternScreenModel::Page::PianoRoll: {
            // Pose la hauteur (degré diatonique) sur le pas sous le curseur ; REC ON = step-record.
            const int   ss = juce::jlimit(0, n - 1, selectedStep_);
            const auto& sd = row.steps[static_cast<size_t>(ss)];
            SequencerCommand c;
            c.id = SequencerCommandId::SetStep;
            c.a  = static_cast<uint8_t>(sr);
            c.b  = static_cast<uint8_t>(ss);
            c.c  = static_cast<uint8_t>(rollWhiteKeyMidi(index));
            c.d  = sd.velocity;
            c.e  = sd.gate;
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
            const auto& lk   = row.steps[static_cast<size_t>(step)].ccLocks[static_cast<size_t>(slot)];
            if (lk.ccNumber != 0xFF) {
                SequencerCommand c;
                c.id = SequencerCommandId::ClearStepCCLock;
                c.a  = static_cast<uint8_t>(sr);
                c.b  = static_cast<uint8_t>(step);
                c.c  = static_cast<uint8_t>(slot);
                proc_.controller().postCommand(c);
            } else {
                postAutoValueAt(step, 100);  // pose un lock à valeur par défaut
            }
            break;
        }
        case PatternScreenModel::Page::Harmony: {
            if (index >= 7) return;          // blanches 1..7 = degrés
            harmonyField_ = 0;
            postChordSlotEdit(index + 1);
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
    const auto& pat = proc_.engine().pattern();
    switch (screenPage_) {
        case PatternScreenModel::Page::Pattern:
            if (index == 0)      selectedRow_ = juce::jmax(0, selectedRow_ - 1);
            else if (index == 1) selectedRow_ = juce::jmin(static_cast<int>(pat.numRows) - 1, selectedRow_ + 1);
            else if (index == 2) stepPage_ = juce::jmax(0, stepPage_ - 1);                 // Page-
            else if (index == 3) stepPage_ = juce::jmin(stepPageCount() - 1, stepPage_ + 1); // Page+ (adaptatif)
            break;
        case PatternScreenModel::Page::PianoRoll:
            if (index == 0)      shiftKeyboardOctave(-1);
            else if (index == 1) shiftKeyboardOctave(+1);
            break;
        case PatternScreenModel::Page::Auto:
            if (index == 0)      autoSlot_ = juce::jmax(0, autoSlot_ - 1);
            else if (index == 1) autoSlot_ = juce::jmin(7, autoSlot_ + 1);
            else if (index == 2) stepPage_ = juce::jmax(0, stepPage_ - 1);                 // Page-
            else if (index == 3) stepPage_ = juce::jmin(stepPageCount() - 1, stepPage_ + 1); // Page+ (adaptatif)
            break;
        case PatternScreenModel::Page::Harmony:
            harmonyField_ = 1;                       // noires = qualités
            postChordSlotEdit(juce::jlimit(0, 11, index));
            break;
        case PatternScreenModel::Page::Global:
        case PatternScreenModel::Page::Song:
        default:
            break;
    }
    applyEncoderConfigForState();
    buildScreenModel();
}

void NidmiSeqAudioProcessorEditor::updateKeysForPage() {
    switch (screenPage_) {
        case PatternScreenModel::Page::PianoRoll:
            for (int i = 0; i < 16; ++i) piano_.setWhiteKeyLabel(i, noteNameFromMidi(rollWhiteKeyMidi(i)));
            piano_.setBlackKeyLabel(0, "Oct-");
            piano_.setBlackKeyLabel(1, "Oct+");
            for (int i = 2; i < 11; ++i) piano_.setBlackKeyLabel(i, {});
            break;
        case PatternScreenModel::Page::Harmony: {
            static const char* kR[7] = {"I", "II", "III", "IV", "V", "VI", "VII"};
            static const char* kQ[12] = {"maj", "m", "dim", "aug", "7", "maj7",
                                         "m7", "mM7", "m7b5", "dim7", "sus2", "sus4"};
            for (int i = 0; i < 7; ++i)  piano_.setWhiteKeyLabel(i, kR[i]);
            for (int i = 7; i < 16; ++i) piano_.setWhiteKeyLabel(i, {});
            for (int i = 0; i < 11; ++i) piano_.setBlackKeyLabel(i, kQ[i]);
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
        case PatternScreenModel::Page::Pattern:
            for (int i = 0; i < 16; ++i) piano_.setWhiteKeyLabel(i, {});
            piano_.setBlackKeyLabel(0, "R-");
            piano_.setBlackKeyLabel(1, "R+");
            piano_.setBlackKeyLabel(2, "Pg-");
            piano_.setBlackKeyLabel(3, "Pg+");
            for (int i = 4; i < 11; ++i) piano_.setBlackKeyLabel(i, {});
            break;
        case PatternScreenModel::Page::Global:
        case PatternScreenModel::Page::Song:
        default:
            for (int i = 0; i < 16; ++i) piano_.setWhiteKeyLabel(i, {});
            piano_.setBlackKeyLabel(0, "R-");
            piano_.setBlackKeyLabel(1, "R+");
            for (int i = 2; i < 11; ++i) piano_.setBlackKeyLabel(i, {});
            break;
    }
}
