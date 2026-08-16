#include "PluginEditor.h"
#include "PluginProcessor.h"

#include "EditorHelpers.h"
#include "MidiExporter.h"

#include <nidmi_seq/ScaleBank.h>
#include <nidmi_seq/HarmonyEngine.h>

#include <cmath>

NidmiSeqAudioProcessorEditor::NidmiSeqAudioProcessorEditor(NidmiSeqAudioProcessor& p)
    : AudioProcessorEditor(&p)
    , proc_(p) {
    setResizeLimits(620, 560, 2000, 1100);

    transportLook_.setKind(HardwareButtonLook::Kind::Transport);
    playBtn_.setLookAndFeel(&transportLook_);
    stopBtn_.setLookAndFeel(&transportLook_);
    recBtn_.setLookAndFeel(&transportLook_);
    editionBtn_.setLookAndFeel(&transportLook_);
    harmonieBtn_.setLookAndFeel(&transportLook_);
    projetBtn_.setLookAndFeel(&transportLook_);

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
    recBtn_.onClick = [this] {
        recArmed_ = !recArmed_;
        // Un seul REC pour deux captures qui ne se genent pas : les touches
        // ecrivent des notes pas a pas, les CC entrants se deposent dans les lanes
        // d'automation. Le drapeau doit traverser jusqu'au thread audio, seul
        // endroit ou l'on voit arriver le MIDI.
        proc_.setCCRecordArmed(recArmed_);
        buildScreenModel();
    };

    // Boutons de FAMILLE de vue (façon Elektron) : accès direct + re-appui = sous-vue suivante.
    editionBtn_.setButtonText("Edit");
    editionBtn_.setToggleable(true);
    editionBtn_.setClickingTogglesState(false);
    editionBtn_.onClick = [this] { selectViewFamily(0); };
    harmonieBtn_.setButtonText("Harmonie");   // VUE Harmonie (≠ bouton « Harm » = harmonie ON/OFF)
    harmonieBtn_.setToggleable(true);
    harmonieBtn_.setClickingTogglesState(false);
    harmonieBtn_.onClick = [this] { selectViewFamily(1); };
    projetBtn_.setButtonText("Projet");
    projetBtn_.setToggleable(true);
    projetBtn_.setClickingTogglesState(false);
    projetBtn_.onClick = [this] { selectViewFamily(2); };

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

    // Push du MASTER : bascule l'encodeur BPM en sélection de pattern de la banque.
    masterPushBtn_.setLookAndFeel(&transportLook_);
    masterPushBtn_.setButtonText("BPM");
    masterPushBtn_.setToggleable(true);
    masterPushBtn_.setClickingTogglesState(false);
    masterPushBtn_.onClick = [this] {
        masterPatternMode_ = !masterPatternMode_;
        configureMasterEncoder();
        buildScreenModel();
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

    // GRAMMAIRE À SIX MOLETTES : elle remplace les encodeurs contextuels ci-dessous,
    // qui restent instanciés mais MASQUÉS le temps du retrait — deux pilotes visibles
    // pour le même moteur seraient exactement le double rendu qu'on paie cher.
    setupGrammarEncoders();

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

    setupEncoder(masterEncoder_);
    masterEncoder_.onDragStart   = [this] { configureMasterEncoder(); };
    masterEncoder_.onValueChange = [this] { onMasterEncoderChanged(); };

    applyEncoderConfigForState();
    configureMasterEncoder();

    auto labelStyle = [](juce::Label& l, const juce::String& t) {
        l.setText(t, juce::dontSendNotification);
        l.setJustificationType(juce::Justification::centred);
        l.setFont(juce::Font(juce::FontOptions().withHeight(12.0f)));
    };
    labelStyle(navEncoderLabel_, "Curseur");
    labelStyle(valueEncoderLabel_, "Valeur");
    labelStyle(veloEncoderLabel_, juce::String(juce::CharPointer_UTF8("V\xc3\xa9lo")));
    labelStyle(zoomEncoderLabel_, "Zoom");
    labelStyle(masterEncoderLabel_, "Master");

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
        updateKeysForPage();   // rafraîchit les labels R1:A… (mode par mesure)
        buildScreenModel();
    };
    // Cliquer une case porte AUSSI le focus sur sa bande. Sans ca on designe un
    // marqueur de tonalite et les encodeurs continuent d'editer les accords —
    // le curseur bouge, les molettes agissent ailleurs.
    screen_.onHarmonySlot = [this](int slot) {
        harmonyCursor_ = juce::jlimit(0, 31, slot);
        harmonyFocus_  = HarmonyFocus::Chords;
        applyEncoderConfigForState();
        updateKeysForPage();
        buildScreenModel();
    };
    screen_.onKeySlot = [this](int slot) {
        keyCursor_    = juce::jlimit(0, 15, slot);
        harmonyFocus_ = HarmonyFocus::Tonality;
        applyEncoderConfigForState();
        updateKeysForPage();
        buildScreenModel();
    };
    screen_.onAutoSlot = [this](int slot) {
        autoSlot_ = juce::jlimit(PatternScreenModel::kAutoLaneSlot, 7, slot);   // -1 = LANE de la row
        // Le champ Interp n'existe que sur la LANE : en quittant celle-ci, on
        // retombe sur Valeur plutot que de laisser un champ sans objet.
        if (autoSlot_ != PatternScreenModel::kAutoLaneSlot) autoField_ = juce::jlimit(0, 1, autoField_);
        applyEncoderConfigForState();
        configurePushButtons();
        buildScreenModel();
    };
    screen_.onAutoField = [this](int field) {
        autoField_ = juce::jlimit(0, (autoSlot_ == PatternScreenModel::kAutoLaneSlot) ? 2 : 1, field);
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
        const auto& r = pat.rows[static_cast<size_t>(row)];
        const int   n = juce::jmax(1, static_cast<int>(r.numSteps));
        if (step < 0 || step >= n)
            return;
        // Si le pas cliqué est RECOUVERT par un owner antérieur (note longue / drill span>1),
        // on ne crée pas un pas par-dessus : on sélectionne l'owner (le drill) à la place.
        for (int o = step - 1; o >= 0; --o) {
            const auto& osd = r.step(static_cast<uint8_t>(editBar_), static_cast<uint8_t>(o));
            if (!osd.enabled) continue;
            const int osp = juce::jlimit(1, n - o, juce::jmax(1, static_cast<int>(osd.span)));
            if (o + osp > step) {           // owner couvrant le plus proche
                selectedRow_  = row;
                selectedStep_ = o;
                buildScreenModel();
                return;                     // pas de toggle dans la zone couverte
            }
        }
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
    // Les cinq encodeurs CONTEXTUELS sont supersédés par la grammaire à six molettes
    // (CONCEPTION §7). Ils restent instanciés — beaucoup de code les référence encore —
    // mais INVISIBLES : deux pilotes du même moteur à l'écran seraient exactement le
    // double rendu qui a coûté cher. Le retrait complet suivra, en une passe à part.
    for (juce::Component* dead : { (juce::Component*)&navEncoder_,
                                   (juce::Component*)&navEncoderLabel_,
                                   (juce::Component*)&valueEncoder_,
                                   (juce::Component*)&valueEncoderLabel_,
                                   (juce::Component*)&veloEncoder_,
                                   (juce::Component*)&veloEncoderLabel_,
                                   (juce::Component*)&zoomEncoder_,
                                   (juce::Component*)&zoomEncoderLabel_,
                                   (juce::Component*)&masterEncoder_,
                                   (juce::Component*)&masterEncoderLabel_,
                                   (juce::Component*)&masterPushBtn_ })
        addChildComponent(dead);
    addAndMakeVisible(playBtn_);
    addAndMakeVisible(stopBtn_);
    addAndMakeVisible(recBtn_);
    addAndMakeVisible(editionBtn_);
    addAndMakeVisible(harmonieBtn_);
    addAndMakeVisible(projetBtn_);
    addAndMakeVisible(exportBtn_);
    addAndMakeVisible(shiftBtn_);
    addAndMakeVisible(muteBtn_);
    addAndMakeVisible(harmBtn_);
    for (int i = 0; i < 4; ++i)
        addChildComponent(pushBtn_[i]);   // idem : supersédés par gPush_[]

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
    editionBtn_.toFront(false);
    harmonieBtn_.toFront(false);
    projetBtn_.toFront(false);
    masterEncoder_.toFront(false);
    masterPushBtn_.toFront(false);
    masterEncoderLabel_.toFront(false);
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
    editionBtn_.setLookAndFeel(nullptr);
    harmonieBtn_.setLookAndFeel(nullptr);
    projetBtn_.setLookAndFeel(nullptr);
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

    // Au-dessus de l’écran : Play, Stop, Rec, Vue, Export (5 boutons, cahier §11.3) + MASTER à droite.
    {
        auto        row = r.removeFromTop(56);
        // Encodeur MASTER à droite (contextuel) : knob plein-hauteur + label-lecture à gauche.
        if (row.getWidth() > 60) {
            auto      mrow = row.removeFromRight(150);
            const int kd   = juce::jmin(54, mrow.getHeight());
            masterEncoder_.setBounds(mrow.removeFromRight(kd).withSizeKeepingCentre(kd, kd));
            auto      lab  = mrow.reduced(2, 0);
            // À gauche du knob : label de lecture (haut) + bouton push BPM/Pat (bas).
            masterPushBtn_.setBounds(lab.removeFromBottom(juce::jmin(22, lab.getHeight() / 2)));
            masterEncoderLabel_.setBounds(lab);
        }
        // Boutons transport centrés dans le reste, hauteur ~42 (Play/Stop/Rec/Export).
        auto        brow = row.reduced(0, 7);
        const int   bw  = 92;
        const int   gap = 6;
        const int   totalW = bw * 4 + gap * 3;
        brow.removeFromLeft(juce::jmax(0, (brow.getWidth() - totalW) / 2));
        playBtn_.setBounds(brow.removeFromLeft(bw));
        brow.removeFromLeft(gap);
        stopBtn_.setBounds(brow.removeFromLeft(bw));
        brow.removeFromLeft(gap);
        recBtn_.setBounds(brow.removeFromLeft(bw));
        brow.removeFromLeft(gap);
        exportBtn_.setBounds(brow.removeFromLeft(bw));
    }

    // Rangée « Vues » (familles, façon Elektron) : Édition / Harmonie / Projet.
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

        // Six molettes a role fixe : Row / Pas / Valeur a gauche, Velo / Duree /
        // Master a droite. L'ecran reste au centre, comme sur le boitier.
        auto leftCol  = block.removeFromLeft(sideW);
        auto rightCol = block.removeFromRight(sideW);
        layoutGrammarEncoders(leftCol, rightCol);
        juce::ignoreUnused(placeKnob, halfH);

        screen_.setBounds(block.reduced(6, 2));
    }

    // Rangée du bas : familles de Vue (Édition/Harmonie/Projet) + contrôles (Mute/Shift/Harm). 32 px.
    {
        auto      row = r.removeFromTop(32);
        const int bw  = 70;
        const int gap = 6;
        const int totalW = bw * 6 + gap * 5;
        row.removeFromLeft(juce::jmax(0, (row.getWidth() - totalW) / 2));
        auto place = [&](juce::TextButton& b) {
            b.setBounds(row.removeFromLeft(bw).reduced(0, 2));
            row.removeFromLeft(gap);
        };
        place(editionBtn_);
        place(harmonieBtn_);
        place(projetBtn_);
        place(muteBtn_);
        place(shiftBtn_);
        harmBtn_.setBounds(row.removeFromLeft(bw).reduced(0, 2));   // dernier (sans gap final)
    }

    piano_.setBounds(r);
}

void NidmiSeqAudioProcessorEditor::timerCallback() {
    refreshPianoKeysFromEngine();
    // Les commandes partent par le fifo : l'effet n'est visible qu'au tour suivant.
    // On relit donc l'etat a chaque frame plutot que de deviner ce qu'il est devenu.
    refreshGrammarEncoders();

    const bool midiClk =
        proc_.apvts().getRawParameterValue("useMidiClock") != nullptr &&
        proc_.apvts().getRawParameterValue("useMidiClock")->load() > 0.5f;

    const bool manualTransportOk = !midiClk;
    playBtn_.setEnabled(manualTransportOk);
    stopBtn_.setEnabled(manualTransportOk);
    recBtn_.setEnabled(true);
    recBtn_.setButtonText(recArmed_ ? "REC●" : "Rec");
    // LED des boutons de FAMILLE : la famille de la vue courante est allumée.
    {
        const int pg = static_cast<int>(screenPage_);
        const bool edit = (pg == 0 || pg == 1 || pg == 3);   // Pattern/Roll/Auto
        const bool harm = (pg == 2);
        const bool proj = (pg == 4 || pg == 5);              // Global/Song
        editionBtn_.setToggleState(edit, juce::dontSendNotification);
        harmonieBtn_.setToggleState(harm, juce::dontSendNotification);
        projetBtn_.setToggleState(proj, juce::dontSendNotification);
    }
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
    // Borne le curseur de pas sur le N de la ROW sélectionnée (1..64), PAS sur le legacy
    // pat.numSteps (global, =16 par défaut) qui plafonnait la sélection à 16.
    const int selRowN = (pat.numRows > 0)
        ? juce::jlimit(1, 64, static_cast<int>(
              pat.rows[static_cast<size_t>(juce::jlimit(0, static_cast<int>(pat.numRows) - 1, selectedRow_))].numSteps))
        : 1;
    if (selectedStep_ >= selRowN)
        selectedStep_ = juce::jmax(0, selRowN - 1);
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

    // Push master : LED + libellé (BPM par défaut / Pat = sélection de pattern).
    masterPushBtn_.setToggleState(masterPatternMode_, juce::dontSendNotification);
    masterPushBtn_.setButtonText(masterPatternMode_ ? "Pat" : "BPM");

    // Re-synchro périodique des encodeurs sur l'état moteur — SUSPENDUE quelques frames après
    // une rotation (encSyncCooldown_) : sinon le timer relit une valeur pas encore drainée par
    // la FIFO et fait « revenir en arrière » l'encodeur (durée d'un slot d'accord, etc.).
    if (encSyncCooldown_ > 0) {
        --encSyncCooldown_;
    } else {
        if (!navEncoder_.isMouseButtonDown() && !valueEncoder_.isMouseButtonDown())
            applyEncoderConfigForState();
        if (!veloEncoder_.isMouseButtonDown())
            configureVeloEncoder();
        if (!masterEncoder_.isMouseButtonDown())
            configureMasterEncoder();   // master contextuel : valeur/label à jour (Accent/Swing/BPM)
    }
    if (masterHudFrames_ > 0)
        --masterHudFrames_;         // expiration du HUD transitoire
    configurePushButtons();   // labels/état/led des boutons PUSH (Suppr suit le curseur slot)
    if (screenPage_ == PatternScreenModel::Page::PianoRoll)
        zoomEncoderLabel_.setText("Zoom " + juce::String(rollOctaves_) + "oct", juce::dontSendNotification);
    else if (screenPage_ == PatternScreenModel::Page::Harmony
             && harmonyFocus_ == HarmonyFocus::Tonality) {
        // Focus TONALITE : Enc4 = GAMME du marqueur edite (Tonique est sur Enc2).
        //
        // Ce bloc tourne a CHAQUE frame. Il ne regardait pas le focus et ecrasait
        // donc l'etiquette posee par applyEncoderConfigForState : on cliquait un
        // marqueur, le bandeau annoncait « Enc4=Gamme » et la molette affichait
        // « Tonique ». Le traiter ici plutot que sauter le bloc — sauter faisait
        // tomber l'etiquette dans le cas par defaut, qui affichait « Row 1 ».
        const auto& kp = proc_.engine().pattern().keyProgression;
        const int   kc = juce::jlimit(0, juce::jmax(0, static_cast<int>(kp.len) - 1), keyCursor_);
        const int   sc = (kp.len > 0) ? static_cast<int>(kp.slots[static_cast<size_t>(kc)].scaleId)
                                      : static_cast<int>(proc_.engine().projectSettings().masterScaleId);
        zoomEncoderLabel_.setText(juce::String("Gamme ") + scalebank::getScale(static_cast<uint8_t>(
            juce::jlimit(0, static_cast<int>(scalebank::Count) - 1, sc))).name,
            juce::dontSendNotification);
    }
    else if (screenPage_ == PatternScreenModel::Page::Harmony) {
        // Enc4 = Tonique (ou Gamme si push →Gamme actif), lue sur la source EFFECTIVE.
        // (Harmonie ON/OFF = bouton « Harm » dédié, plus de ⇧Enc4.)
        //
        // « Effective » veut dire : la LANE DE TONALITE d'abord, car currentKey()
        // lui donne la priorite absolue des qu'elle a un marqueur. L'etiquette lisait
        // pattern/master et affichait donc « Tonique D# » pendant que « Key: » — et
        // le son — restaient en do. Meme source que l'ecriture, sinon la molette et
        // son etiquette racontent deux choses differentes.
        const auto& ph     = proc_.engine().pattern().harmony;
        const auto& ps     = proc_.engine().projectSettings();
        const auto& kp     = proc_.engine().pattern().keyProgression;
        const bool  toMas  = ph.followMasterTonality;
        const bool  toLane = (kp.len > 0);
        const int   kc     = juce::jlimit(0, juce::jmax(0, static_cast<int>(kp.len) - 1), keyCursor_);
        if (harmZoomScale_) {
            const int sc = toLane ? static_cast<int>(kp.slots[static_cast<size_t>(kc)].scaleId)
                         : toMas  ? static_cast<int>(ps.masterScaleId)
                                  : static_cast<int>(ph.scaleId);
            zoomEncoderLabel_.setText(juce::String("Gamme ") + scalebank::getScale(static_cast<uint8_t>(
                juce::jlimit(0, static_cast<int>(scalebank::Count) - 1, sc))).name,
                juce::dontSendNotification);
        } else {
            const int rp = toLane ? static_cast<int>(kp.slots[static_cast<size_t>(kc)].rootPc)
                         : toMas  ? static_cast<int>(ps.masterRootPc)
                                  : static_cast<int>(ph.rootPc);
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
