#include "PluginEditor.h"
#include "PluginProcessor.h"

#include "MidiExporter.h"

#include <nidmi_seq/ScaleBank.h>
#include <nidmi_seq/HarmonyEngine.h>

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

const char* kRoman[7]         = {"I", "II", "III", "IV", "V", "VI", "VII"};

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

    // Shift logiciel (toggle) : active les secondes fonctions des touches/encodeurs de la Vue.
    // (Rendu ON/LED identique à muteBtn_.) L'octave du clavier ROLL passe par les noires 0/1.
    shiftBtn_.setLookAndFeel(&transportLook_);
    shiftBtn_.setButtonText("Shift");
    shiftBtn_.setToggleable(true);
    shiftBtn_.setClickingTogglesState(false);
    shiftBtn_.onClick = [this] {
        shiftHeld_ = !shiftHeld_;
        shiftBtn_.setToggleState(shiftHeld_, juce::dontSendNotification);
        lastShiftActive_ = shiftActive();
        applyEncoderConfigForState();
        configureVeloEncoder();
        updateKeysForPage();
        buildScreenModel();
    };

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

    // Harmonie ON/OFF du pattern (toggle + LED, visible toutes vues, miroir de muteBtn_).
    harmBtn_.setLookAndFeel(&transportLook_);
    harmBtn_.setButtonText("Harm");
    harmBtn_.setToggleable(true);
    harmBtn_.setClickingTogglesState(false);
    harmBtn_.onClick = [this] {
        const auto& ph = proc_.engine().pattern().harmony;
        SequencerCommand c;
        c.id = SequencerCommandId::SetPatternHarmony;
        c.x  = !ph.harmonyEnabled;                       // toggle
        c.a  = static_cast<uint8_t>(ph.scaleId);         // préserve gamme
        c.b  = static_cast<uint8_t>(ph.rootPc);          // préserve tonique
        c.y  = ph.followProgression;                     // préserve suivi progression
        proc_.controller().postCommand(c);
    };

    // Brique PUSH générique : 4 petits boutons, un sous chaque encodeur (cf. convention
    // pushBtn_[i] dans PluginEditor.h). Style transport, toggle géré manuellement
    // (configurePushButtons pose label/état/led ; onPushButton route l'action).
    for (int i = 0; i < 4; ++i) {
        pushBtn_[i].setLookAndFeel(&transportLook_);
        pushBtn_[i].setToggleable(true);
        pushBtn_[i].setClickingTogglesState(false);
        pushBtn_[i].onClick = [this, i] { onPushButton(i); };
    }

    auto setupEncoder = [](juce::Slider& s) {
        s.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        s.setRotaryParameters(juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f,
                              true);
        s.setMouseDragSensitivity(180);
        s.setScrollWheelEnabled(false);
    };

    setupEncoder(navEncoder_);
    navEncoder_.onDragStart    = [this] { applyEncoderConfigForState(); };  // cale le rôle selon la Vue/Shift
    navEncoder_.onValueChange = [this] { onNavEncoderChanged(); };
    setupEncoder(valueEncoder_);
    valueEncoder_.onDragStart   = [this] { applyEncoderConfigForState(); };  // cale (ex. HARMONIE Shift=Bass)
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
    screen_.onMeasureSelected = [this](int bar) {
        // Clic sur le bandeau de mesures : sélectionne la mesure éditée (= ⇧Page).
        const int nb = juce::jlimit(1, 4, static_cast<int>(proc_.engine().patternNumBars()));
        editBar_ = juce::jlimit(0, nb - 1, bar);
        applyEncoderConfigForState();
        buildScreenModel();
    };
    screen_.onHarmonySlot = [this](int slot) {
        harmonyCursor_ = juce::jlimit(0, 31, slot);
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
        const auto& sd = pat.rows[static_cast<size_t>(row)].step(static_cast<uint8_t>(editBar_), static_cast<uint8_t>(step));
        selectedRow_  = row;
        selectedStep_ = step;
        SequencerCommand c;
        c.id = SequencerCommandId::SetStep;   // pose la hauteur + active le pas
        c.a  = static_cast<uint8_t>(row);
        c.b  = static_cast<uint8_t>(step);
        c.c  = static_cast<uint8_t>(juce::jlimit(0, 127, note));
        c.d  = sd.velocity;
        c.e  = sd.gate;
        c.f  = static_cast<uint8_t>(editBar_);
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
        c.f  = static_cast<uint8_t>(editBar_);
        proc_.controller().postCommand(c);
    };
    // DRILL-IN (strip de sous-pas) : clic souris = sélectionne + toggle le sous-pas
    // (miroir de onWhiteKey en mode sub PATTERN).
    screen_.onSubStepToggled = [this](int subStep) {
        if (!inSub_) return;
        const int subIdx = activeSubIdx();
        if (subIdx < 0) return;
        const auto& pat = proc_.engine().pattern();
        const int   sn  = juce::jlimit(1, 16, static_cast<int>(pat.subPatterns[static_cast<size_t>(subIdx)].numSteps));
        if (subStep < 0 || subStep >= sn) return;
        subStep_ = subStep;
        SequencerCommand c;
        c.id = SequencerCommandId::ToggleSubStep;
        c.a  = static_cast<uint8_t>(subIdx);
        c.b  = static_cast<uint8_t>(subStep);
        proc_.controller().postCommand(c);
        applyEncoderConfigForState();
        buildScreenModel();
    };
    // DRILL-IN (sub-roll) : clic souris = pose une hauteur absolue sur le sous-pas
    // (miroir de onWhiteKey en mode sub ROLL ; postSubStepPitch gère relatif/absolu).
    screen_.onSubNoteSet = [this](int subStep, int note) {
        if (!inSub_) return;
        const int subIdx = activeSubIdx();
        if (subIdx < 0) return;
        const auto& pat = proc_.engine().pattern();
        const int   sn  = juce::jlimit(1, 16, static_cast<int>(pat.subPatterns[static_cast<size_t>(subIdx)].numSteps));
        if (subStep < 0 || subStep >= sn) return;
        subStep_ = subStep;
        postSubStepPitch(subStep, juce::jlimit(0, 127, note));
        applyEncoderConfigForState();
    };
    // DRILL-IN (sub-roll, lane vélo) : clic = règle la vélo du sous-pas (note/gate préservés).
    screen_.onSubLaneValue = [this](int subStep, int value) {
        if (!inSub_) return;
        const int subIdx = activeSubIdx();
        if (subIdx < 0) return;
        const auto& sp = proc_.engine().pattern().subPatterns[static_cast<size_t>(subIdx)];
        const int   sn = juce::jlimit(1, 16, static_cast<int>(sp.numSteps));
        if (subStep < 0 || subStep >= sn) return;
        const auto& sd = sp.steps[static_cast<size_t>(subStep)];
        subStep_ = subStep;
        SequencerCommand c;
        c.id = SequencerCommandId::SetSubStep;
        c.a  = static_cast<uint8_t>(subIdx);
        c.b  = static_cast<uint8_t>(subStep);
        c.c  = sd.note;                                              // note préservée
        c.d  = static_cast<uint8_t>(juce::jlimit(0, 127, value));    // nouvelle vélo
        c.e  = sd.gate;                                              // gate préservé
        proc_.controller().postCommand(c);
        applyEncoderConfigForState();
        buildScreenModel();
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
    addAndMakeVisible(shiftBtn_);
    addAndMakeVisible(muteBtn_);
    addAndMakeVisible(harmBtn_);
    for (int i = 0; i < 4; ++i)
        addAndMakeVisible(pushBtn_[i]);

    applyEncoderConfigForState();
    configurePushButtons();
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
    shiftBtn_.toFront(false);
    muteBtn_.toFront(false);
    harmBtn_.toFront(false);
    for (int i = 0; i < 4; ++i)
        pushBtn_[i].toFront(false);

    startTimerHz(24);  // playhead lisible jusqu'a ~180 BPM avec 16 pas/mesure
}

NidmiSeqAudioProcessorEditor::~NidmiSeqAudioProcessorEditor() {
    stopTimer();
    playBtn_.setLookAndFeel(nullptr);
    stopBtn_.setLookAndFeel(nullptr);
    recBtn_.setLookAndFeel(nullptr);
    vueBtn_.setLookAndFeel(nullptr);
    exportBtn_.setLookAndFeel(nullptr);
    shiftBtn_.setLookAndFeel(nullptr);
    muteBtn_.setLookAndFeel(nullptr);
    harmBtn_.setLookAndFeel(nullptr);
    for (int i = 0; i < 4; ++i)
        pushBtn_[i].setLookAndFeel(nullptr);
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
        // Chaque colonne : label (15px) en haut, knob carré au milieu, bouton PUSH (~16px) en bas.
        auto placeKnob = [](juce::Rectangle<int> col, juce::Label& lab, juce::Slider& knob,
                            juce::TextButton& push) {
            lab.setBounds(col.removeFromTop(15));
            push.setBounds(col.removeFromBottom(22).reduced(6, 2));   // bouton push plus haut/lisible
            auto k = col.reduced(2, 2);
            const int d = juce::jmin(k.getWidth(), k.getHeight());
            knob.setBounds(k.withSizeKeepingCentre(d, d));
        };
        const int halfH = block.getHeight() / 2;

        auto leftCol = block.removeFromLeft(sideW);
        placeKnob(leftCol.removeFromTop(halfH), valueEncoderLabel_, valueEncoder_, pushBtn_[1]);
        placeKnob(leftCol, veloEncoderLabel_, veloEncoder_, pushBtn_[2]);

        auto rightCol = block.removeFromRight(sideW);
        placeKnob(rightCol.removeFromTop(halfH), navEncoderLabel_, navEncoder_, pushBtn_[0]);
        placeKnob(rightCol, zoomEncoderLabel_, zoomEncoder_, pushBtn_[3]);

        screen_.setBounds(block.reduced(6, 2));
    }

    // Rangée « contrôles » : Mute (row) + Shift (2des fonctions) + Harm (harmonie ON/OFF). 32 px.
    {
        auto      row = r.removeFromTop(32);
        const int bw  = 70;
        const int gap = 6;
        const int totalW = bw * 3 + gap * 2;
        row.removeFromLeft(juce::jmax(0, (row.getWidth() - totalW) / 2));
        muteBtn_.setBounds(row.removeFromLeft(bw).reduced(0, 2));
        row.removeFromLeft(gap);
        shiftBtn_.setBounds(row.removeFromLeft(bw).reduced(0, 2));
        row.removeFromLeft(gap);
        harmBtn_.setBounds(row.removeFromLeft(bw).reduced(0, 2));
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
    // On reste dans le sub en changeant de Vue (PATTERN = on/off, ROLL = hauteurs…).
    screenPage_ = static_cast<PatternScreenModel::Page>(pageIndex);
    applyEncoderConfigForState();
    configurePushButtons();
    updateKeysForPage();
    buildScreenModel();
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
    applyEncoderConfigForState();
    configureVeloEncoder();
    configurePushButtons();
    updateKeysForPage();
    buildScreenModel();
}

void NidmiSeqAudioProcessorEditor::exitSub() {
    inSub_ = false;
    applyEncoderConfigForState();
    configureVeloEncoder();
    configurePushButtons();
    updateKeysForPage();
    buildScreenModel();
}

int NidmiSeqAudioProcessorEditor::subHostNote() const {
    const auto& pat = proc_.engine().pattern();
    if (pat.numRows == 0) return 60;
    const int r = juce::jlimit(0, static_cast<int>(pat.numRows) - 1, subHostRow_);
    const int n = juce::jlimit(1, 64, static_cast<int>(pat.rows[static_cast<size_t>(r)].numSteps));
    const int s = juce::jlimit(0, n - 1, subHostStep_);
    return pat.rows[static_cast<size_t>(r)].step(static_cast<uint8_t>(editBar_), static_cast<uint8_t>(s)).note;
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
    applyEncoderConfigForState();
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
        // HARMONIE : Enc2 = Slot (jusqu'à len = slot d'ajout) — sans branche Shift.
        // Le cycle de mode par row se fait désormais par ⇧+blanche N.
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
    if (inSub_) {
        const int subIdx = activeSubIdx();
        if (subIdx < 0) return;
        if (screenPage_ == PatternScreenModel::Page::PianoRoll) {
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
        // Enc2 = Degré, ou Bass si push →Bass actif.
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
    // onglets en ossature : pas d'édition via Enc1 pour l'instant.
}

void NidmiSeqAudioProcessorEditor::setChordField(int field, int value) {
    const auto& prog = proc_.engine().pattern().chordProgression;
    const int   len  = juce::jlimit(0, 32, static_cast<int>(prog.len));
    const int   cur  = juce::jlimit(0, 31, harmonyCursor_);

    // Lit le slot existant, ou des valeurs par défaut s'il s'agit d'un ajout.
    int deg = 1, qual = 0, ext = 0, bass = 0, dur = 1;
    if (cur < len) {
        const auto& cs = prog.slots[static_cast<size_t>(cur)];
        deg = cs.degree; qual = static_cast<int>(cs.quality); ext = cs.extensions;
        bass = cs.bassOffset; dur = cs.durationSlots;
    } else if (len > 0) {
        // Ajout : copie complète du dernier slot édité (degré + qualité + ext + bass + durée).
        const auto& cs = prog.slots[static_cast<size_t>(len - 1)];
        deg = cs.degree; qual = static_cast<int>(cs.quality); ext = cs.extensions;
        bass = cs.bassOffset; dur = cs.durationSlots;
    }
    // (sinon : I major par défaut, deg=1 qual=0 ext=0 bass=0 dur=1 ci-dessus)
    switch (field) {
        case 0: deg  = juce::jlimit(1, 7, value); break;             // Degré
        case 1: qual = juce::jlimit(0, 5, value); break;             // Qualité (triade 0..5)
        // case 2 (Extensions par index) supprimé : les extensions sont des bits togglés par les
        //   noires (onBlackKey), pas un index. setChordField préserve les bits existants tels quels.
        case 3: bass = juce::jlimit(-12, 12, value); break;          // Bass
        default: dur = juce::jlimit(1, 16, value); break;            // Durée
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

int NidmiSeqAudioProcessorEditor::sharedHarmonyMode() const {
    // Ne considère QUE les rows liées (mode != Chromatic). Si toutes les rows liées
    // partagent le même mode → ce mode ; si elles divergent → -1 (mixte) ; s'il n'y
    // a aucune row liée → défaut B1 (1).
    const auto& pat = proc_.engine().pattern();
    const int   nr  = juce::jlimit(0, 16, static_cast<int>(pat.numRows));
    int shared = -2;   // -2 = aucune row liée vue encore
    for (int r = 0; r < nr; ++r) {
        const int rm = static_cast<int>(pat.rows[static_cast<size_t>(r)].harmonyMode);
        if (rm == static_cast<int>(RowHarmonyMode::Chromatic))
            continue;                            // row déliée → ignorée
        if (shared == -2)        shared = rm;
        else if (shared != rm) { return -1; }    // rows liées divergentes → mixte
    }
    return (shared == -2) ? 1 : shared;          // défaut B1 si aucune row liée
}

void NidmiSeqAudioProcessorEditor::setStepField(int step, int field, int value) {
    const auto& pat = proc_.engine().pattern();
    if (pat.numRows == 0)
        return;
    const int sr = juce::jlimit(0, static_cast<int>(pat.numRows) - 1, selectedRow_);
    const int n  = juce::jlimit(1, 64, static_cast<int>(pat.rows[static_cast<size_t>(sr)].numSteps));
    if (step < 0 || step >= n)
        return;
    const auto& sd = pat.rows[static_cast<size_t>(sr)].step(static_cast<uint8_t>(editBar_), static_cast<uint8_t>(step));
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
    c.f  = static_cast<uint8_t>(editBar_);
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
        const auto& lk = arow.step(static_cast<uint8_t>(editBar_), static_cast<uint8_t>(s)).ccLocks[static_cast<size_t>(slot)];
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
    c.f  = static_cast<uint8_t>(editBar_);
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
            const auto& lk = arow.step(static_cast<uint8_t>(editBar_), static_cast<uint8_t>(s)).ccLocks[static_cast<size_t>(slot)];
            if (lk.ccNumber == 0xFF)
                continue;
            SequencerCommand c;
            c.id = SequencerCommandId::SetStepCCLock;
            c.a  = static_cast<uint8_t>(ar);
            c.b  = static_cast<uint8_t>(s);
            c.c  = static_cast<uint8_t>(slot);
            c.d  = static_cast<uint8_t>(autoCcDefault_);
            c.e  = lk.value;
            c.f  = static_cast<uint8_t>(editBar_);
            proc_.controller().postCommand(c);
        }
    }
    applyEncoderConfigForState();
    buildScreenModel();
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
        if (screenPage_ == PatternScreenModel::Page::PianoRoll && subIdx >= 0) {
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
            valueEncoderLabel_.setText("Degré " + juce::String(kRoman[juce::jlimit(0, 6, deg - 1)]),
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
        // Drill-in sub : Enc1 = Sortir (action). Enc2 = contenu du sub (N ou note) en
        // rotation directe — le span du pas hôte se règle dans la vue normale, pas ici.
        setAction(0, juce::String(juce::CharPointer_UTF8("\xe2\x86\x91")) + " Sortir", true);
        hide(1);
        setToggle(2, arrow + "Gate", subVeloGate_);   // Enc3 : Vélo <-> Gate du sous-pas
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
    if (inSub_) {
        if (idx == 0) { exitSub(); return; }          // Enc1 = Sortir du sub
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
            veloEncoder_.setRange(1.0, 100.0, 1.0);
            veloEncoder_.setValue(static_cast<double>(sd.gate), juce::dontSendNotification);
            veloEncoderLabel_.setText("Gate " + juce::String(sd.gate), juce::dontSendNotification);
        } else {
            veloEncoder_.setRange(0.0, 127.0, 1.0);
            veloEncoder_.setValue(static_cast<double>(sd.velocity), juce::dontSendNotification);
            veloEncoderLabel_.setText("Vélo " + juce::String(sd.velocity), juce::dontSendNotification);
        }
        return;
    }

    // HARMONIE : la qualité (triade) passe sur les blanches 8..13 → Enc3 = Durée du slot.
    if (screenPage_ == PatternScreenModel::Page::Harmony) {
        const auto& prog = proc_.engine().pattern().chordProgression;
        const int   cur  = juce::jlimit(0, 31, harmonyCursor_);
        int dur = 1;
        if (cur < static_cast<int>(prog.len))
            dur = prog.slots[static_cast<size_t>(cur)].durationSlots;
        veloEncoder_.setRange(1.0, 16.0, 1.0);
        veloEncoder_.setValue(static_cast<double>(dur), juce::dontSendNotification);
        // durationSlots = nombre de MESURES pendant lesquelles l'accord reste actif.
        veloEncoderLabel_.setText("Duree " + juce::String(dur) + " mes", juce::dontSendNotification);
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
        veloEncoderLabel_.setText("Vélo " + juce::String(sd.velocity), juce::dontSendNotification);
    }
}

void NidmiSeqAudioProcessorEditor::onVeloEncoderChanged() {
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
        SequencerCommand c;
        c.id = SequencerCommandId::SetSubStep;
        c.a  = static_cast<uint8_t>(subIdx);
        c.b  = static_cast<uint8_t>(ss);
        c.c  = sd.note;                                                  // note préservée
        c.d  = subVeloGate_ ? sd.velocity : static_cast<uint8_t>(juce::jlimit(0, 127, v));
        c.e  = subVeloGate_ ? static_cast<uint8_t>(juce::jlimit(1, 100, v)) : sd.gate;
        proc_.controller().postCommand(c);
        buildScreenModel();
        return;
    }

    // HARMONIE : Enc3 = Durée du slot (la qualité est posée par les blanches 8..13).
    if (screenPage_ == PatternScreenModel::Page::Harmony) {
        setChordField(4, (int) std::lround(veloEncoder_.getValue()));
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
    shiftBtn_.setEnabled(true);
    shiftBtn_.setToggleState(shiftHeld_, juce::dontSendNotification);

    // Suivi du Shift clavier OS : quand l'état effectif (bouton OU touche) change entre deux
    // frames, on rafraîchit labels/surbrillances/encodeurs pour refléter les secondes fonctions.
    const bool shiftNow = shiftActive();
    if (shiftNow != lastShiftActive_) {
        lastShiftActive_ = shiftNow;
        applyEncoderConfigForState();
        configureVeloEncoder();
        updateKeysForPage();
    }

    // Apparition/disparition du label rel/abs sur la noire 9 quand le pas sélectionné change
    // de sub (le curseur Enc2 ne rappelle pas updateKeysForPage). La LED, elle, est continue.
    const int relSubNow = relevantSubIdx();
    if (relSubNow != lastRelevantSub_) {
        lastRelevantSub_ = relSubNow;
        updateKeysForPage();
    }

    const auto& pat = proc_.engine().pattern();
    if (selectedRow_ >= pat.numRows)
        selectedRow_ = juce::jmax(0, static_cast<int>(pat.numRows) - 1);
    if (selectedStep_ >= pat.numSteps)
        selectedStep_ = juce::jmax(0, static_cast<int>(pat.numSteps) - 1);
    // Mesure éditée : bornée à [0, numBars-1] (numBars==1 => toujours 0).
    editBar_ = juce::jlimit(0, juce::jmax(0, static_cast<int>(pat.numBars) - 1), editBar_);

    // HARMONIE : le curseur de slot hérite de l'état réel de la progression
    // (miroir du re-clampage de selectedRow_/selectedStep_ ci-dessus). Après un
    // chargement de projet / switch de pattern qui raccourcit la progression, on
    // évite qu'il pointe au-delà du dernier slot. La position « len » reste valide
    // (= slot d'ajout), donc on borne à len et non len-1.
    {
        const int progLen = juce::jlimit(0, 32, static_cast<int>(pat.chordProgression.len));
        if (harmonyCursor_ > progLen)
            harmonyCursor_ = progLen;
    }

    const bool muted = selectedRow_ < pat.numRows
                       && pat.rows[static_cast<size_t>(selectedRow_)].muted;
    muteBtn_.setToggleState(muted, juce::dontSendNotification);
    muteBtn_.setButtonText(muted ? "Muted" : "Mute");

    // Harmonie ON/OFF : LED + texte reflètent l'état réel du pattern.
    const bool harmOn = pat.harmony.harmonyEnabled;
    harmBtn_.setToggleState(harmOn, juce::dontSendNotification);
    harmBtn_.setButtonText(harmOn ? "Harm ON" : "Harm");

    if (!navEncoder_.isMouseButtonDown() && !valueEncoder_.isMouseButtonDown())
        applyEncoderConfigForState();
    if (!veloEncoder_.isMouseButtonDown())
        configureVeloEncoder();
    configurePushButtons();   // labels/état/led des boutons PUSH (Suppr suit le curseur slot)
    if (screenPage_ == PatternScreenModel::Page::PianoRoll)
        zoomEncoderLabel_.setText("Zoom " + juce::String(rollOctaves_) + "oct", juce::dontSendNotification);
    else if (screenPage_ == PatternScreenModel::Page::Harmony) {
        // Enc4 = Tonique (ou Gamme si push →Gamme actif), lue sur la source effective.
        // (Harmonie ON/OFF = bouton « Harm » dédié, plus de ⇧Enc4.)
        const auto& ph     = proc_.engine().pattern().harmony;
        const auto& ps     = proc_.engine().projectSettings();
        const bool  toMas  = ph.followMasterTonality;
        if (harmZoomScale_) {
            const int sc = toMas ? static_cast<int>(ps.masterScaleId) : static_cast<int>(ph.scaleId);
            zoomEncoderLabel_.setText(juce::String("Gamme ") + scalebank::getScale(static_cast<uint8_t>(
                juce::jlimit(0, static_cast<int>(scalebank::Count) - 1, sc))).name,
                juce::dontSendNotification);
        } else {
            const int rp = toMas ? static_cast<int>(ps.masterRootPc) : static_cast<int>(ph.rootPc);
            static const char* kPc[12] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
            zoomEncoderLabel_.setText(juce::String("Tonique ") + kPc[juce::jlimit(0, 11, rp)],
                                      juce::dontSendNotification);
        }
    } else if (screenPage_ == PatternScreenModel::Page::Pattern && zoomNumBars_) {
        // Push →Bars (PATTERN) = nombre de mesures du pattern.
        const int nb = juce::jlimit(1, 4, static_cast<int>(proc_.engine().patternNumBars()));
        zoomEncoderLabel_.setText("Mesures " + juce::String(nb), juce::dontSendNotification);
    } else
        zoomEncoderLabel_.setText("Row " + juce::String(selectedRow_ + 1), juce::dontSendNotification);

    buildScreenModel();
}

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

    // Filtre harmonique : tonalité effective (même logique que la section HARMONIE plus bas,
    // calculée ici en amont car la boucle rows en a besoin pour résoudre la note jouée).
    // effectiveHarmony() est privé côté core → on recompose root/scale manuellement.
    const auto& psHarm   = proc_.engine().projectSettings();
    const auto& phHarm   = pat.harmony;
    const int   effRootB = phHarm.followMasterTonality ? static_cast<int>(psHarm.masterRootPc)
                                                       : static_cast<int>(phHarm.rootPc);
    const int   effScaleB = phHarm.followMasterTonality ? static_cast<int>(psHarm.masterScaleId)
                                                        : static_cast<int>(phHarm.scaleId);
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
        dst.harmonyMode = static_cast<int>(row.harmonyMode);
        dst.muted       = row.muted;
        // Libellé musical (1/16, 1/8T, 5:tps…) + durée réelle d'un pas en ms.
        // Calculs côté éditeur → PatternScreen reste agnostique du core.
        dst.divLabel    = divisionLabel(n, pat.numerator, pat.denominator);
        const double stepUs = barUs / static_cast<double>(juce::jmax(1, n));
        dst.stepMs      = static_cast<int>(stepUs / 1000.0 + 0.5);
        // Row soumise à l'harmonie : harmonie active globale ET mode != Chromatic.
        const bool bound = harmActive && row.harmonyMode != RowHarmonyMode::Chromatic;
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
                    sd.note, row.harmonyMode, currentChord, progActive,
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
    const int effRoot  = ph.followMasterTonality ? static_cast<int>(ps.masterRootPc)
                                                 : static_cast<int>(ph.rootPc);
    const int effScale = ph.followMasterTonality ? static_cast<int>(ps.masterScaleId)
                                                 : static_cast<int>(ph.scaleId);
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
        cv.durationSlots = cs.durationSlots;
        cv.rootPc        = static_cast<int>(
            harmony::degreePitchClass(static_cast<uint8_t>(cs.degree),
                                      static_cast<uint8_t>(effScale),
                                      static_cast<uint8_t>(effRoot)));
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
            m.autoSlotCc[i] = cc;
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
        const bool ledOn = (subIdx >= 0)
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
            for (int i = 0; i < 16; ++i) {
                const int s = base + i;
                piano_.whiteKey(i).setToggleState(
                    s < rowN && row.step(static_cast<uint8_t>(editBar_), static_cast<uint8_t>(s)).enabled,
                    juce::dontSendNotification);
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
            int hl = -1;
            for (int i = 0; i < 16; ++i)
                if (rollWhiteKeyMidi(i) == note) { hl = i; break; }
            piano_.setPlayheadStep(hl);
            break;
        }

        case PatternScreenModel::Page::Harmony: {
            clearWhites();
            if (shiftActive()) {
                // ⇧ : surligne les blanches des rows LIÉES (mode != Chromatic) ; pas de degré.
                const int nr = juce::jlimit(0, 16, static_cast<int>(pat.numRows));
                for (int i = 0; i < nr && i < 16; ++i)
                    piano_.whiteKey(i).setToggleState(
                        pat.rows[static_cast<size_t>(i)].harmonyMode != RowHarmonyMode::Chromatic,
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
            if (step >= n) return;          // PAD : toggle le pas
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
            // ⇧ + blanche N : cycle l'état harmonique de la row N (les blanches ↔ rows 0..15).
            // Ordre : Chromatic(délié) → A → B1 → B2 → Chromatic.
            if (shiftActive()) {
                const int hr = index;   // blanche index ↔ row index
                if (hr >= static_cast<int>(pat.numRows)) return;   // pas de row N
                const int rm = static_cast<int>(pat.rows[static_cast<size_t>(hr)].harmonyMode);
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
            }
            // blanches 13..15 : inactives.
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
        case PatternScreenModel::Page::Pattern:
            // Blanches = pas (pas de hauteur à jouer) → noire SEULE = la fonction. Oct± inactif.
            // Noire 9 = bascule rel/abs du sub du pas sélectionné (inactif si pas de sub).
            if (index == 9) toggleSubMode();
            else            runFunction(index, /*allowOct*/ false);
            break;
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
                bass = cs.bassOffset; dur = cs.durationSlots;
            } else {
                setChordField(0, (len > 0) ? prog.slots[static_cast<size_t>(len - 1)].degree : 1);
                const auto& prog2 = proc_.engine().pattern().chordProgression;
                if (cur < static_cast<int>(prog2.len)) {
                    const auto& cs = prog2.slots[static_cast<size_t>(cur)];
                    deg = cs.degree; qual = static_cast<int>(cs.quality); ext = cs.extensions;
                    bass = cs.bassOffset; dur = cs.durationSlots;
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
        case PatternScreenModel::Page::Song:
        default:
            break;
    }
    applyEncoderConfigForState();
    buildScreenModel();
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
                        const int m = juce::jlimit(0, 3, static_cast<int>(pat.rows[static_cast<size_t>(i)].harmonyMode));
                        piano_.setWhiteKeyLabel(i, "R" + juce::String(i + 1) + ":" + kMode[m]);
                    } else {
                        piano_.setWhiteKeyLabel(i, {});   // au-delà de numRows : neutre
                    }
                }
            } else {
                for (int i = 0; i < 7; ++i)  piano_.setWhiteKeyLabel(i, kR[i]);        // 0..6 = degrés
                for (int i = 7; i < 13; ++i) piano_.setWhiteKeyLabel(i, kTri[i - 7]);  // 7..12 = triades
                for (int i = 13; i < 16; ++i) piano_.setWhiteKeyLabel(i, {});          // 13..15 = vides
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
            piano_.setBlackKeyLabel(2, "Pg-");
            piano_.setBlackKeyLabel(3, "Pg+");
            piano_.setBlackKeyLabel(4, "Sub");
            piano_.setBlackKeyLabel(5, "Mes-");
            piano_.setBlackKeyLabel(6, "Mes+");
            for (int i = 7; i < 11; ++i) piano_.setBlackKeyLabel(i, {});   // Oct± inactif en pattern
            piano_.setBlackKeyLabel(9, (selectedStepSubIdx() >= 0) ? "Mode" : juce::String());  // rel/abs sub
            break;
        }
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
