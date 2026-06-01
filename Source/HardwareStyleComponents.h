#pragma once

#include <array>
#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

/// Modèle d'affichage de l'écran TFT (rempli par l'éditeur depuis le moteur).
/// Le composant `PatternScreen` ne connaît pas le core : il dessine ce snapshot.
/// L'écran TFT est à ONGLETS (cahier §10.1 rév. 2026-05) : une page visible à la fois.
struct PatternScreenModel {
    enum class Page { Pattern, PianoRoll, Harmony, Auto, Global, Song };
    static constexpr int kNumPages = 6;

    struct Row {
        int           numSteps    = 16;     // N de la row (1..64)
        int           channel     = 1;      // 1..16 pour affichage
        int           harmonyMode = 1;      // 0=A 1=B1 2=B2 3=Chromatic
        bool          muted       = false;
        int           playhead    = -1;     // pas en cours, -1 si arrêté/hors champ
        bool          enabled[64] = {};
        unsigned char note[64]    = {};     // note MIDI par pas (affichage)
        unsigned char velocity[64] = {};    // vélocité par pas (0..127)
        unsigned char gate[64]    = {};     // gate par pas (1..100 %)
        signed char   subIdx[64]  = {};     // index du subpattern déclenché par ce pas (-1 = aucun)
        unsigned char span[64]    = {};     // nb de pas hôtes couverts par ce pas (1 = normal ; >1 = note longue / sub étalé)
        unsigned char playedNote[64] = {};  // note réellement émise après filtre harmonique (= note si row libre)
        bool          snapped[64]    = {};  // true si playedNote != note (note stockée hors filtre, tirée par l'harmonie)
        bool          harmonyBound   = false; // row soumise à l'harmonie (mode != Chromatic && harmonie active)
        juce::String  divLabel;             // valeur musicale du pas (1/16, 1/8T, 5:tps…), vide si non réductible
        int           stepMs        = 0;    // durée réelle d'un pas en ms (barDuration/numSteps), 0 = inconnu
    };

    // Aperçu d'un subpattern (tuplet imbriqué) pour l'affichage niché + l'édition.
    struct SubView {
        int           numSteps    = 0;      // N du sub (0 = slot libre)
        bool          enabled[16] = {};
        unsigned char note[16]    = {};     // note brute (absolue, ou offset centré sur 64 si relatif)
        bool          relative    = false;  // mode : sous-pas relatifs à la note hôte (ancre)
        int           duration    = 1;      // span : nombre de pas hôtes couverts (1..64)
    };
    static constexpr int kSubRelCenter = 64;   // miroir de kSubRelativeCenter (core)

    // Un paramètre listé sur la page GLOBAL (héritage des 8-params de l'OLED, désormais sur le TFT).
    struct GlobalParam {
        juce::String name;
        juce::String value;
    };

    Page  page         = Page::Pattern;

    // Barre de titre (commune à toutes les pages).
    bool  playing      = false;
    float bpm          = 120.0f;
    int   tsNum        = 4;
    int   tsDen        = 4;

    // Multi-mesures (1..kMaxBars). editBar = mesure ÉDITÉE (choix utilisateur) ;
    // playBar = mesure en LECTURE (moteur). numBars==1 => tout reste à 0 (rétrocompat).
    int   numBars      = 1;
    int   editBar      = 0;
    int   playBar      = 0;

    // Vue PATTERN.
    int   numRows      = 0;
    int   selectedRow  = 0;
    int   selectedStep = 0;
    bool  recArmed     = false;
    int   keyPageStart = -1;   // 1er pas de la fenêtre de 16 éditée par les touches (-1 = aucune)
    int   keyPageCount = 1;    // nb total de pages de 16 pas (pour l'indicateur P2/4)
    int   prBottomNote   = -1; // PIANO ROLL : note MIDI du bas de la fenêtre (défilement Oct± ; -1 = auto)
    int   prVisibleSemis = 0;  // PIANO ROLL : nb de demi-tons (lanes) à afficher = octaves×12 (0 = auto)
    int   rowZoom      = 0;    // PATTERN : nb de rows visibles (0 = auto)
    int   stepZoom     = 1;    // PATTERN : facteur de zoom horizontal des pas (1 = mesure entière)
    Row   rows[16];

    // Subpatterns (tuplets imbriqués).
    SubView subs[16];
    bool    inSub       = false;   // édition d'un sub en cours (re-cible les Vues)
    int     subEditIdx  = -1;      // sub en cours d'édition
    int     subStep     = 0;       // curseur de sous-pas
    int     subHostRow  = 0;       // fil d'Ariane : row…
    int     subHostStep = 0;       // …et pas qui héberge le sub
    int     subHostNote = 60;      // note du pas hôte (ancre du piano-roll relatif)

    // Page GLOBAL.
    int         numGlobalParams = 0;
    int         globalCursor    = 0;
    GlobalParam global[16];

    // Page HARMONIE (progression d'accords).
    struct ChordSlotView {
        int degree        = 1;   // 1..7
        int quality       = 0;   // index ChordQuality
        int extensions    = 0;   // bitfield
        int bassOffset    = 0;   // -12..+12
        int durationSlots = 1;
        int rootPc        = 0;   // pitch-class (0..11) de la racine réelle (calculée core-side)
    };
    int           progLen       = 0;   // nb de slots utilisés
    int           progCurrent   = -1;  // slot en cours de lecture
    int           harmonyCursor = 0;   // slot édité
    ChordSlotView chord[32];

    // Tonalité effective + suivi (la vue HARMONIE hérite/affiche l'état réel du pattern).
    int  harmonyRootPc          = 0;   // pitch-class de la tonique effective
    int  harmonyScaleId         = 0;   // id de gamme effectif (index ScaleBank)
    bool harmonyEnabled         = true;
    bool followProgression      = true;
    bool followMasterTonality   = true;
    int  harmonySharedMode      = 1;   // mode harmonique partagé par toutes les rows (-1 = mixte)

    // Page AUTO (P-locks CC de la row sélectionnée).
    int autoSlot     = 0;     // slot de P-lock actif (0..7)
    int autoField    = 0;     // 0=Valeur (par pas) 1=CC#
    int autoCc       = 74;    // numéro de CC du slot actif (affichage)
    int autoSlotCc[8] = {-1, -1, -1, -1, -1, -1, -1, -1};  // CC# par slot (-1 = inactif)
    int autoValue[64];        // valeur du slot actif par pas (-1 = pas de lock)
};

/// Écran TFT ~320×240 émulé, à pages.
/// Page PATTERN : grille multi-row — chaque row répartit ses `numSteps` cellules sur la MÊME
/// largeur (la mesure) → la polyrythmie est visible (N différents divergent puis réalignent au downbeat).
/// Page GLOBAL : liste de paramètres projet, curseur surligné (Enc2 = curseur, Enc1 = valeur).
/// Piloté par modèle ; les interactions remontent par callbacks (l'éditeur poste les commandes).
class PatternScreen : public juce::Component {
public:
    PatternScreen();

    void paint(juce::Graphics& g) override;
    void resized() override;

    /// Remplace le snapshot affiché et repaint.
    void setModel(const PatternScreenModel& m);

    std::function<void(int row)>                 onRowSelected;
    std::function<void(int row, int step)>       onStepToggled;
    std::function<void(int tabIndex)>            onTabSelected;
    std::function<void(int row, int step, int note)> onNoteSet;       // PIANO ROLL : pose une hauteur
    std::function<void(int step, int value)>         onRollLaneValue; // PIANO ROLL : vélo via la lane
    std::function<void(int slotIdx)>             onHarmonySlot;   // HARMONIE : sélection de slot
    std::function<void(int slotIdx)>             onAutoSlot;      // AUTO : slot de P-lock actif
    std::function<void(int field)>               onAutoField;     // AUTO : champ (Valeur/CC#)
    std::function<void(int step, int value)>     onAutoValueSet;  // AUTO : valeur d'un pas (clic lane)
    std::function<void(int bar)>                 onMeasureSelected; // PATTERN : clic sur le bandeau de mesures
    std::function<void(int subStep)>             onSubStepToggled;  // DRILL-IN (strip) : sélectionne + toggle un sous-pas
    std::function<void(int subStep, int note)>   onSubNoteSet;      // DRILL-IN (sub-roll) : pose une hauteur sur un sous-pas

    static const char* chordQualityShort(int quality);

    /// Libellés courts des onglets (index = valeur de Page).
    static const char* pageLabel(int pageIndex);

private:
    void paintTabBar(juce::Graphics& g);
    void paintPatternPage(juce::Graphics& g);
    void paintPianoRollPage(juce::Graphics& g);
    void paintSubRoll(juce::Graphics& g);   // piano-roll du sub (ligne d'ancrage si relatif)
    void paintHarmonyPage(juce::Graphics& g);
    void paintAutoPage(juce::Graphics& g);
    void paintGlobalPage(juce::Graphics& g);
    void paintStubPage(juce::Graphics& g, const juce::String& title, const juce::String& subtitle);
    void mouseDown(const juce::MouseEvent& e) override;
    void recomputeLayout();
    int  tabAtX(float x) const;
    /// Bandeau de mesures (PATTERN, numBars>1, hors-sub) : rect réservé en haut de bodyArea_,
    /// vide (height==0) sinon. Calculé à l'identique par paint et le hit-test.
    juce::Rectangle<float> measureBandArea() const;
    /// Zone de grille des rows = bodyArea_ moins le bandeau de mesures (si présent).
    juce::Rectangle<float> gridArea() const;
    int  measureAtX(float x) const;   // index de mesure cliqué dans le bandeau (-1 hors bandeau)

    PatternScreenModel model_;

    // Métriques calculées dans recomputeLayout(), réutilisées par paint() et le hit-test.
    juce::Rectangle<float> tabBarArea_;
    juce::Rectangle<float> headerArea_;
    juce::Rectangle<float> bodyArea_;
    float rowH_      = 0.0f;
    int   firstVisibleRow_ = 0;   // viewport vertical : 1re row affichée (défile avec la sélection)
    int   visibleRows_     = 1;   // nb de rows affichables à hauteur lisible
    float gutterW_   = 30.0f;   // libellé "R1" à gauche
    float infoW_     = 96.0f;   // infos N/canal/mode à droite
};

/// Look : pads carrés type « blanches » / « dièses » / transport.
class HardwareButtonLook : public juce::LookAndFeel_V4 {
public:
    enum class Kind { WhiteKey, BlackKey, Transport };

    void setKind(Kind k) { kind_ = k; }
    void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    void drawButtonText(juce::Graphics& g, juce::TextButton& button, bool shouldDrawButtonAsHighlighted,
                        bool shouldDrawButtonAsDown) override;

private:
    Kind kind_ = Kind::WhiteKey;
};

/// 16 pads blancs (pas) + 11 pads dièses (#) entre les blanches, disposition type piano (carrés).
/// Les blanches exposent press/release distincts (gestes long-press, HOLD-TO-PITCH).
class PianoKeysPanel : public juce::Component {
public:
    PianoKeysPanel();

    void resized() override;

    void setWhiteKeyClick(int stepIndex, std::function<void()> fn);
    /// Press/release séparés sur les blanches : permet la détection long-press côté éditeur.
    /// Si setWhiteKeyDown/Up est défini, setWhiteKeyClick n'est pas appelé pour cette touche.
    void setWhiteKeyDown(int stepIndex, std::function<void()> fn);
    void setWhiteKeyUp(int stepIndex, std::function<void()> fn);
    void setBlackKeyClick(int funcIndex, std::function<void()> fn);

    /// Libellé affiché sur une touche noire (nom de fonction, 1–2 lignes possibles).
    void setBlackKeyLabel(int funcIndex, const juce::String& name);
    /// Libellé surimprimé sur la touche blanche (note name pendant HOLD-TO-PITCH). Vide = caché.
    void setWhiteKeyLabel(int stepIndex, const juce::String& name);

    juce::TextButton& whiteKey(int i) { return *whiteKeys_[static_cast<size_t>(i)]; }
    juce::TextButton& blackKey(int i) { return *blackKeys_[static_cast<size_t>(i)]; }

    /// Souligne visuellement la touche blanche correspondant au pas en cours.
    /// `step` ∈ [0, kNumWhite-1] = surbrillance ; -1 = aucun playhead.
    void setPlayheadStep(int step);
    /// Surbrillance d'une noire (utilisée pendant l'overlay PITCH si l'altération est choisie).
    void setBlackKeyHighlight(int blackIdx);
    /// LED d'ÉTAT persistante sur une noire (vert = ON). Indépendante du highlight transitoire.
    /// idx ∈ [0, kNumBlack-1] ; on=true allume. À idx==-1 : éteint la LED actuelle.
    void setBlackKeyLed(int blackIdx, bool on);

private:
    static constexpr int kNumWhite = 16;
    static constexpr int kNumBlack = 11;

    std::array<std::unique_ptr<juce::TextButton>, kNumWhite>  whiteKeys_;
    std::array<std::unique_ptr<juce::TextButton>, kNumBlack> blackKeys_;
    std::array<HardwareButtonLook, kNumWhite>                 whiteLooks_;
    std::array<HardwareButtonLook, kNumBlack>                 blackLooks_;
    std::array<std::function<void()>, kNumWhite>              whiteDown_;
    std::array<std::function<void()>, kNumWhite>              whiteUp_;
    int                                                       playheadStep_  = -1;
    int                                                       blackHighlight_ = -1;
    int                                                       blackLed_       = -1;   // noire portant la LED d'état (-1 = aucune)

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    int  whiteKeyIndexFromComponent(const juce::Component* c) const;
};
