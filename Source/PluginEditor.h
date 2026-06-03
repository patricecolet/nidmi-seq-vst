#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "HardwareStyleComponents.h"

class NidmiSeqAudioProcessor;

class NidmiSeqAudioProcessorEditor : public juce::AudioProcessorEditor,
                                      private juce::Timer {
public:
    explicit NidmiSeqAudioProcessorEditor(NidmiSeqAudioProcessor&);
    ~NidmiSeqAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshPianoKeysFromEngine();
    void syncValueEncoderFromParam();
    void applyValueEncoderToParam();

    // Encodeurs contextuels selon la page (cahier §10.2) :
    //  GLOBAL  : Enc2 = curseur param, Enc1 = valeur param.
    //  PATTERN : Enc2 = sélection row, Enc1 = N (tuplet) de la row → re-subdivision live.
    void onNavEncoderChanged();
    void onValueEncoderChanged();
    void applyEncoderConfigForState();

    static int  numOledParams();
    static const char* oledParamId(int index);
    static const char* oledParamTitle(int index);

    NidmiSeqAudioProcessor& proc_;

    bool recArmed_ = false;

    HardwareButtonLook transportLook_;
    PatternScreen        screen_;
    PianoKeysPanel       piano_;

    PatternScreenModel::Page screenPage_ = PatternScreenModel::Page::Pattern;
    void buildScreenModel();
    void setScreenPage(int pageIndex);

    juce::TextButton playBtn_;
    juce::TextButton stopBtn_;
    juce::TextButton recBtn_;
    juce::TextButton vueBtn_;
    juce::TextButton exportBtn_;
    juce::TextButton shiftBtn_;
    juce::TextButton muteBtn_;
    juce::TextButton harmBtn_;   // Harmonie ON/OFF du pattern (toggle + LED), visible partout

    // Brique PUSH générique : un petit bouton sous chaque encodeur.
    // Convention d'indexation pushBtn_[i] <-> encodeur :
    //   0 = Enc1 navEncoder_ (curseur)   3 = Enc4 zoomEncoder_ (zoom)
    //   1 = Enc2 valueEncoder_ (valeur)  2 = Enc3 veloEncoder_  (vélo)
    // (le même i sert pour les colonnes du resized() ci-dessous.)
    juce::TextButton pushBtn_[4];
    void configurePushButtons();   // labels + état toggle/led des 4 boutons selon la Vue/état
    void onPushButton(int idx);    // route le clic du bouton vers l'action/bascule de l'encodeur

    std::unique_ptr<juce::FileChooser> fileChooser_;

    // Shift logiciel (bouton, toggle) OU touche Shift clavier OS : les deux activent
    // les secondes fonctions (cf. shiftActive()). updateKeysForPage() est rappelée dès
    // que l'état change (onClick du bouton + suivi clavier en timerCallback).
    bool shiftHeld_       = false;   // bouton Shift maintenu (toggle)
    bool lastShiftActive_ = false;   // suivi inter-frames pour le Shift clavier OS
    int  lastRelevantSub_ = -2;      // suivi du sub pertinent (noire 9) pour rafraîchir le label rel/abs
    bool shiftActive() const {
        return shiftHeld_ || juce::ModifierKeys::getCurrentModifiers().isShiftDown();
    }

    void launchExportFlow();
    void shiftKeyboardOctave(int delta);

    /// Encodeur 1 : choix du paramètre éditable (affiché sur l’OLED).
    juce::Slider navEncoder_;
    juce::Label  navEncoderLabel_;
    /// Encodeur 2 : réglage de la valeur du paramètre sélectionné.
    juce::Slider valueEncoder_;
    juce::Label  valueEncoderLabel_;

    /// Encodeur 3 (ajout hardware) : vélocité du pas sélectionné (toute Vue).
    juce::Slider veloEncoder_;
    juce::Label  veloEncoderLabel_;
    /// Encodeur 4 (ajout hardware) : zoom — vertical (rows) ; Shift = horizontal (pas).
    juce::Slider zoomEncoder_;
    juce::Label  zoomEncoderLabel_;
    double       lastZoomEnc_ = 0.0;   // encodeur Zoom traité en relatif (direction)

    int rowZoom_     = 0;   // PATTERN : nb de rows visibles (0 = auto selon hauteur)
    int stepZoom_    = 1;   // PATTERN : facteur de zoom horizontal des pas (1 = mesure entière)
    int rollOctaves_ = 2;   // PIANO ROLL : nb d'octaves visibles (zoom vertical des hauteurs)

    void onVeloEncoderChanged();
    void onZoomEncoderChanged();
    void configureVeloEncoder();   // cale Enc3 sur Vélo, ou Gate si Shift (push émulé)

    int oledParamIndex_ = 0;

    int selectedRow_  = 0;
    int selectedStep_ = 0;

    // « Page » = fenêtre de 16 pas adressée par les touches (Vues PATTERN/AUTO) quand N>16.
    int stepPage_     = 0;
    int stepPageCount() const;   // nb de pages de 16 pas de la row sélectionnée

    // Mesure actuellement ÉDITÉE (0..numBars-1), INDÉPENDANTE de la mesure de LECTURE.
    // Choix utilisateur (⇧Page-/⇧Page+). Toutes les écritures de pas la passent via cmd.f.
    int editBar_      = 0;

    // Presse-papier de PAS (Vue PATTERN). Mémorise les COORDONNÉES de la SOURCE d'une copie ;
    // le collage poste CopyStep(source -> curseur). Limite assumée : si la source est modifiée
    // entre copier et coller, le coller reflète l'état au moment du coller.
    // cut=true → couper-coller : la source n'est effacée qu'AU collage (puis presse-papier consommé).
    struct { int row = 0; int step = 0; int bar = 0; bool valid = false; bool cut = false; } stepClip_;

    // Subpatterns (tuplets imbriqués) : édition « drill-in » re-ciblant la Vue PATTERN.
    bool inSub_       = false;
    int  subHostRow_  = 0;
    int  subHostStep_ = 0;
    int  subStep_     = 0;        // curseur de sous-pas
    int  activeSubIdx() const;   // index du sub édité (host step), -1 si aucun
    int  selectedStepSubIdx() const;  // index du sub porté par le pas SÉLECTIONNÉ (hors drill-in), -1 si aucun
    int  relevantSubIdx() const;      // sub pertinent selon le contexte : drill-in -> activeSubIdx, sinon selectedStepSubIdx
    int  subHostNote() const;    // note du pas hôte (ancre du mode relatif)
    void enterOrCreateSub();     // crée le sub si besoin puis entre
    void exitSub();
    void postSubStepPitch(int subStepIndex, int absolutePitch);  // pose une hauteur (gère abs/relatif)
    void toggleSubMode();        // bascule le sub courant relatif <-> absolu

    void setStepField(int step, int field, int value);   // applique Note/Vélo/Gate au pas (SetStep)

    // Vue HARMONIE : slot édité. Attributs sur encodeurs dédiés (modèle rotation/push) :
    //   Enc1 (curseur) : rotation = Slot ; PUSH = Suppr (DeleteChordSlot).
    //   Enc2 (valeur)  : rotation = Degré ; PUSH "→Bass" (toggle harmValBass_) = rotation Bass.
    //   Enc3 (vélo)    : rotation = Qualité ; PUSH "→Durée" (toggle harmVeloDur_) = rotation Durée.
    //   Enc4 (zoom)    : rotation = Tonique ; PUSH "→Gamme" (toggle harmZoomScale_) = rotation Gamme.
    //                    ⇧Enc4 = bascule Harmonie ON/OFF.
    // Noires = extensions (bitfield). Blanches 1..7 = degrés (⇧+blanche = cycle mode row).
    int  harmonyCursor_ = 0;
    bool harmValBass_   = false;   // Enc2 : rotation édite Bass au lieu du Degré
    bool harmVeloDur_   = false;   // Enc3 : rotation édite Durée au lieu de la Qualité
    bool harmZoomScale_ = false;   // Enc4 : rotation édite Gamme au lieu de la Tonique

    // Bascules push d'encodeur pour les autres vues (remplacent les anciens combos ⇧Enc).
    bool patternValSpan_ = false;   // PATTERN Enc2 : rotation édite Span au lieu de N
    bool veloGate_       = false;   // PATTERN Enc3 : rotation édite Gate au lieu de Vélo
    bool rollVeloGate_   = false;   // ROLL Enc3 : rotation édite Gate au lieu de Vélo
    bool subVeloGate_    = false;   // DRILL-IN Enc3 : rotation édite Gate au lieu de Vélo du sous-pas
    bool zoomNumBars_    = false;   // PATTERN Enc4 : rotation édite NumBars au lieu de Row
    void setChordField(int field, int value);   // field 0=deg 1=qual 2=ext 3=bass 4=dur
    int  sharedHarmonyMode() const;             // mode commun aux rows liées (-1 = mixte) — affichage seul

    // Page AUTO : slot de P-lock actif, champ (0=Valeur 1=CC#), CC# par défaut pour nouvelles écritures.
    int  autoSlot_       = 0;
    int  autoField_      = 0;
    int  autoCcDefault_  = 74;
    int  effectiveAutoCc() const;                 // CC# du slot actif (existant) ou défaut
    void postAutoValueAt(int step, int value);    // écrit/maj un P-lock CC
    void postAutoCcNumber(int ccNumber);          // change le CC# du slot actif

    // Clavier (cahier §10.3) : les touches sont le MIROIR de l'onglet actif.
    //  PATTERN    : blanches = pas (toggle), noires = R-/R+.
    //  PIANO ROLL : blanches = clavier diatonique → pose la hauteur du pas sous le curseur
    //               (REC ON = avance ensuite = step-record) ; noires = octave -/+.
    //  HARMONIE   : blanches 1..7 = degrés du slot édité ; noires = qualités.
    //  AUTO       : blanches = pas (toggle P-lock du slot actif) ; noires = slot -/+.
    int keyboardOctave_ = 0;   // décalage d'octave du clavier ROLL (en octaves)

    void onWhiteKey(int index);                 // tap blanche, dispatché selon l'onglet
    void onBlackKey(int index);                 // tap noire, dispatché selon l'onglet
    void updateKeysForPage();                   // labels + état des touches selon l'onglet
    int  rollWhiteKeyMidi(int index) const;     // degré diatonique -> note MIDI (ROLL)
    int  rollBlackKeyMidi(int blackIndex) const; // note chromatique d'une noire (ROLL) = blanche gauche + 1 demi-ton
    static juce::String noteNameFromMidi(int midi);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NidmiSeqAudioProcessorEditor)
};
