#pragma once

// Symboles internes PARTAGÉS entre les .cpp issus du découpage de l'ancien
// HardwareStyleComponents.cpp (constantes, couleurs, helpers de rendu, structs de layout).
// Tous au scope GLOBAL en `inline` pour ne pas toucher les sites d'appel et garder une seule
// définition à l'édition de liens. À n'inclure QUE par les .cpp de cette famille (PianoKeysPanel,
// ScreenPatternView, ScreenRollView, ScreenHarmonyView, ScreenAutoGlobalView, HardwareStyleComponents).

#include "HardwareStyleComponents.h"

#include <juce_gui_basics/juce_gui_basics.h>

/// Largeur retirée à chaque cellule blanche : crée un **vrai intervalle** entre deux pads (les # se lisent
/// dans cet intervalle ; avec ~1 px l’œil voit encore les # « sur » la grille).
inline constexpr float kWhiteKeyTrim = 5.0f;

// Dièse #k est entre la blanche d’index `left` et `left+1` (grille piano sur 16 blanches).
inline constexpr int kBlackLeftWhiteIndex[11] = {0, 1, 3, 4, 5, 7, 8, 10, 11, 12, 14};

/// Touches noires plus grandes que l’intervalle entre blanches (chevauchement) pour libellés.
inline constexpr float kBlackKeyWidthFactor  = 0.88f;
inline constexpr float kBlackKeyHeightFactor = 1.08f;

inline const char* harmonyModeShort(int m) {
    switch (m) {
        case 0:  return "A";
        case 1:  return "B1";
        case 2:  return "B2";
        default: return "CHR";
    }
}

inline juce::String midiNoteShort(int midi) {
    static const char* kN[12] = {"C", "C#", "D", "D#", "E", "F",
                                 "F#", "G", "G#", "A", "A#", "B"};
    if (midi < 0 || midi > 127) return "?";
    return juce::String(kN[midi % 12]) + juce::String(midi / 12 - 1);
}

// Palette TFT.
inline const juce::Colour kScreenBg     {0xff0a0c0a};
inline const juce::Colour kScreenBorder {0xff1a221a};
inline const juce::Colour kHeaderText   {0xff6ee7a0};
inline const juce::Colour kRowLabel     {0xff4dd888};
inline const juce::Colour kCellOn       {0xff7fd4a0};
inline const juce::Colour kCellOff      {0xff223026};
inline const juce::Colour kCellGrid     {0xff14301f};
inline const juce::Colour kPlayhead     {0xfff7a13a};
inline const juce::Colour kSelRowBg     {0x2240e090};
inline const juce::Colour kMutedText    {0xff8a5a5a};
inline const juce::Colour kSelStep      {0xffffffff};   // pas sélectionné (curseur) : blanc vif, hors palette
inline const juce::Colour kClipCut      {0xff5aa0ff};   // pas COUPÉ en attente (bleu) : « va être déplacé »
inline const juce::Colour kClipCopy     {0xff3a78c0};   // pas COPIÉ en attente (bleu plus sombre)
inline const juce::Colour kGhost        {0xff35d2d6};   // sous-pattern PARTAGÉ (alias) : triangle cyan, coin bas-gauche
inline const juce::Colour kSubSolo      {0xffff00ff};   // sous-pattern INDÉPENDANT : triangle magenta, coin bas-gauche

/// Teinte par pitch-class (0..11) : 12 couleurs distinctes, saturées mais non criardes,
/// lisibles sur le fond sombre TFT. Les cellules tombant sur la MÊME note jouée partagent
/// donc la même teinte → la « zone chromatique » du filtre harmonique se lit d'un coup d'œil.
inline juce::Colour pitchClassColour(int pc) {
    const int p = ((pc % 12) + 12) % 12;
    return juce::Colour::fromHSV(static_cast<float>(p) / 12.0f, 0.55f, 0.9f, 1.0f);
}

// Géométrie de la page PIANO ROLL, partagée par le rendu et le hit-test (cohérence garantie).
struct PrLayout {
    bool                   valid        = false;
    juce::Rectangle<float> plot;            // zone des notes (hors gouttière de libellés)
    float                  laneH        = 10.0f;
    float                  cellW        = 10.0f;
    int                    topNote      = 84; // note de la lane du haut (i=0)
    int                    visibleLanes = 1;
    int                    n            = 1;  // nombre de pas (tuplet) de la row
};

inline PrLayout computePrLayout(const PatternScreenModel& m, juce::Rectangle<float> body) {
    PrLayout L;
    if (m.numRows <= 0)
        return L;
    const int   r   = juce::jlimit(0, m.numRows - 1, m.selectedRow);
    const auto& row = m.rows[static_cast<size_t>(r)];
    const int   n   = juce::jlimit(1, 64, row.numSteps);

    juce::Rectangle<float> plot = body.withTrimmedLeft(26.0f);
    if (plot.getWidth() < 10.0f || plot.getHeight() < 10.0f)
        return L;

    // Nombre de lanes : zoom octaves (prVisibleSemis) sinon auto (~25 lanes, laneH lisible).
    float laneH;
    int   visible;
    if (m.prVisibleSemis > 0) {
        visible = juce::jlimit(6, 128, m.prVisibleSemis);   // octaves × 12 (jusqu'à toute la plage MIDI)
        laneH   = plot.getHeight() / static_cast<float>(visible);
    } else {
        laneH   = juce::jlimit(7.0f, 16.0f, plot.getHeight() / 25.0f);
        visible = juce::jmax(1, static_cast<int>(plot.getHeight() / laneH));
    }

    // Bas de la fenêtre de hauteurs : suit le clavier (défilement Oct±) si fourni, sinon
    // centré sur la moyenne des notes actives (sinon C4 = 60).
    int low;
    if (m.prBottomNote >= 0) {
        low = m.prBottomNote;
    } else {
        int sum = 0, cnt = 0;
        for (int s = 0; s < n; ++s)
            if (row.enabled[static_cast<size_t>(s)]) { sum += row.note[static_cast<size_t>(s)]; ++cnt; }
        const int center = cnt ? (sum / cnt) : 60;
        low = center - visible / 2;
    }
    low = juce::jlimit(0, juce::jmax(0, 127 - (visible - 1)), low);

    L.valid        = true;
    L.plot         = plot;
    L.laneH        = laneH;
    L.cellW        = plot.getWidth() / static_cast<float>(n);
    L.topNote      = low + visible - 1;
    L.visibleLanes = visible;
    L.n            = n;
    return L;
}

inline const char* romanNumeral(int degree) {
    static const char* kR[7] = {"I", "II", "III", "IV", "V", "VI", "VII"};
    return kR[juce::jlimit(0, 6, degree - 1)];
}

// Noms de gamme lisibles (miroir des 12 gammes du ScaleBank ; PAS d'include du core ici).
inline const char* kScaleNames[12] = {
    "major", "natural minor", "harmonic minor", "melodic minor",
    "dorian", "phrygian", "lydian", "mixolydian",
    "pent major", "pent minor", "blues", "chromatic"};

inline const char* scaleNameShort(int scaleId) {
    return kScaleNames[juce::jlimit(0, 11, scaleId)];
}

// Nom d'une note à partir de sa pitch-class (0..11), sans octave.
inline juce::String pitchClassName(int pc) {
    static const char* kN[12] = {"C", "C#", "D", "D#", "E", "F",
                                 "F#", "G", "G#", "A", "A#", "B"};
    return juce::String(kN[((pc % 12) + 12) % 12]);
}

// ─── Miroir LOCAL des bits d'extension du core (StepTypes.h). PatternScreen est AGNOSTIQUE du
//     core et ne doit PAS l'inclure ; ces valeurs DOIVENT matcher le core à l'identique.
//     core : kExt9=1<<0 kExt11=1<<1 kExt13=1<<2 kExtFlat9=1<<3 kExtSharp9=1<<4 kExtSharp11=1<<5
//            kExtFlat13=1<<6 kExtFlat7=1<<7 kExtMaj7=1<<8 kExtFlat5=1<<9 kExtSharp5=1<<10.
namespace ext {
constexpr int k9       = 1 << 0;
constexpr int k11      = 1 << 1;
constexpr int k13      = 1 << 2;
constexpr int kFlat9   = 1 << 3;
constexpr int kSharp9  = 1 << 4;
constexpr int kSharp11 = 1 << 5;
constexpr int kFlat13  = 1 << 6;
constexpr int kFlat7   = 1 << 7;
constexpr int kMaj7    = 1 << 8;
constexpr int kFlat5   = 1 << 9;
constexpr int kSharp5  = 1 << 10;
}  // namespace ext

// Liste UNIQUEMENT les tensions (9/11/13/b9/#9/#11/b13). La 7e (b7/7♮) et la 5-alt (b5/#5)
// sont gérées par chordSuffix (intégrées au nom d'accord), pas ici.
inline juce::String extensionsShort(int bits) {
    struct { int bit; const char* name; } kE[] = {
        {ext::k9, "9"}, {ext::k11, "11"}, {ext::k13, "13"},
        {ext::kFlat9, "b9"}, {ext::kSharp9, "#9"}, {ext::kSharp11, "#11"}, {ext::kFlat13, "b13"}};
    juce::StringArray parts;
    for (auto& e : kE)
        if (bits & e.bit) parts.add(e.name);
    return parts.isEmpty() ? juce::String() : ("+" + parts.joinIntoString(","));
}

// Suffixe d'accord USUEL recomposé depuis la triade + la 7e + la 5-alt + les tensions.
// quality : 0=maj 1=min 2=dim 3=aug 4=sus2 5=sus4. extensions : bitfield (miroir ci-dessus).
inline juce::String chordSuffix(int quality, int extensions) {
    const int q = juce::jlimit(0, 5, quality);
    const bool hasFlat7 = (extensions & ext::kFlat7) != 0;
    const bool hasMaj7  = (extensions & ext::kMaj7)  != 0;
    const bool hasFlat5 = (extensions & ext::kFlat5) != 0;
    const bool hasSharp5 = (extensions & ext::kSharp5) != 0;

    // Base : triade + type de 7e.
    juce::String base;
    switch (q) {
        case 0: base = hasMaj7 ? "maj7" : (hasFlat7 ? "7"     : "");      break;  // Major
        case 1: base = hasMaj7 ? "mM7"  : (hasFlat7 ? "m7"    : "m");     break;  // Minor
        case 2: base = hasMaj7 ? "dimM7": (hasFlat7 ? "m7b5"  : "dim");   break;  // Diminished
        case 3: base = hasMaj7 ? "augM7": (hasFlat7 ? "aug7"  : "aug");   break;  // Augmented
        case 4: base = hasMaj7 ? "maj7sus2" : (hasFlat7 ? "7sus2" : "sus2"); break; // Sus2
        default: base = hasMaj7 ? "maj7sus4" : (hasFlat7 ? "7sus4" : "sus4"); break; // Sus4
    }

    // 5-alt : n'affiche que si non DÉJÀ impliquée par le nom de base.
    //  - dim / m7b5 impliquent déjà b5 → ne pas réafficher b5.
    //  - aug implique déjà #5 → ne pas réafficher #5.
    juce::String alt5;
    if (hasFlat5  && q != 2) alt5 = "b5";   // b5 visible sauf si triade Diminished
    if (hasSharp5 && q != 3) alt5 = "#5";   // #5 visible sauf si triade Augmented

    // Tensions (sans 7e ni 5-alt).
    juce::StringArray tens;
    struct { int bit; const char* name; } kT[] = {
        {ext::k9, "9"}, {ext::k11, "11"}, {ext::k13, "13"},
        {ext::kFlat9, "b9"}, {ext::kSharp9, "#9"}, {ext::kSharp11, "#11"}, {ext::kFlat13, "b13"}};
    for (auto& t : kT)
        if (extensions & t.bit) tens.add(t.name);

    juce::String out = base + alt5;
    if (!tens.isEmpty()) {
        const bool seventh = hasFlat7 || hasMaj7;
        out += seventh ? ("(" + tens.joinIntoString(",") + ")")   // accord de 7e : tensions entre ()
                       : ("add" + tens.joinIntoString(","));      // sinon : style add9/add11…
    }
    return out;
}

struct HarmLayout {
    juce::Rectangle<float> info, keyBand, slotBand, rowsBand, detail, hint;  // info / tonalité / accords / Rows / détail / hint
    float slotW = 10.0f;
    int   slotsToShow = 1;
    float keyW = 10.0f;
    int   keysToShow = 1;
};

inline HarmLayout computeHarmLayout(const PatternScreenModel& m, juce::Rectangle<float> body) {
    HarmLayout L;
    auto b      = body;
    // En-tête : 3 lignes d'info (Key / Harmonie / Suivi) réservées au-dessus des slots.
    L.info      = b.removeFromTop(juce::jmin(48.0f, b.getHeight() * 0.34f));
    b.removeFromTop(4.0f);
    // Bas : hint (ligne fine) puis détail du slot, réservés AVANT la bande de slots.
    L.hint      = b.removeFromBottom(14.0f);
    b.removeFromBottom(2.0f);
    L.detail    = b.removeFromBottom(20.0f);
    b.removeFromBottom(4.0f);
    // Bande "Rows" (marqueurs lié/délié) juste sous les slots.
    L.rowsBand  = b.removeFromBottom(16.0f);
    b.removeFromBottom(4.0f);
    // Bande TONALITÉ (marqueurs root+gamme) au-dessus des accords.
    L.keyBand   = b.removeFromTop(18.0f);
    b.removeFromTop(4.0f);
    L.slotBand  = b;   // le reste = bande de slots d'accords
    L.slotsToShow = juce::jlimit(1, 32, juce::jmax(m.progLen, m.harmonyCursor + 1));
    L.slotW  = L.slotBand.getWidth() / static_cast<float>(L.slotsToShow);
    L.keysToShow = juce::jlimit(1, 16, juce::jmax(m.keyLen, m.keyCursor + 1));
    L.keyW   = L.keyBand.getWidth() / static_cast<float>(L.keysToShow);
    return L;
}

// Les 5 touches noires d'une octave, par classe de hauteur.
// Sert a teinter les lanes du piano-roll : sans elles on ne distingue pas un
// mi d'un fa sans compter les lignes depuis le do.
inline bool isBlackKeyPc(int note) {
    const int pc = ((note % 12) + 12) % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}

inline constexpr int kAutoNumSlots  = 8;
inline constexpr int kAutoNumFields = 2;
inline const char* autoFieldName(int field) {
    static const char* kF[2] = {"Valeur", "CC#"};
    return kF[juce::jlimit(0, 1, field)];
}

struct AutoLayout {
    juce::Rectangle<float> slotBand, fieldBand, lane, detail;
    float slotW = 10.0f, fieldW = 10.0f, cellW = 10.0f;
    int   n = 1;
};

inline AutoLayout computeAutoLayout(const PatternScreenModel& m, juce::Rectangle<float> body) {
    AutoLayout L;
    auto b      = body;
    L.slotBand  = b.removeFromTop(22.0f);
    b.removeFromTop(4.0f);
    L.fieldBand = b.removeFromTop(20.0f);
    b.removeFromTop(4.0f);
    L.detail    = b.removeFromBottom(20.0f);
    b.removeFromBottom(2.0f);
    L.lane      = b;
    const int r = (m.numRows > 0) ? juce::jlimit(0, m.numRows - 1, m.selectedRow) : 0;
    L.n      = (m.numRows > 0) ? juce::jlimit(1, 64, m.rows[static_cast<size_t>(r)].numSteps) : 1;
    L.slotW  = L.slotBand.getWidth() / static_cast<float>(kAutoNumSlots);
    L.fieldW = L.fieldBand.getWidth() / static_cast<float>(kAutoNumFields);
    L.cellW  = L.lane.getWidth() / static_cast<float>(L.n);
    return L;
}

// PIANO ROLL : découpe le corps en grille de notes / lane vélo (bas).
struct RollFrame {
    juce::Rectangle<float> grid, lane;
};
inline RollFrame computeRollFrame(juce::Rectangle<float> body) {
    RollFrame f;
    auto b = body;
    f.lane = b.removeFromBottom(30.0f);
    b.removeFromBottom(3.0f);
    f.grid = b;
    return f;
}

// Géométrie du bandeau de mesures : préfixe "Mes" optionnel à gauche puis numBars chips égales.
// Renvoie le rect des chips (hors préfixe) et la largeur d'une chip via les paramètres out.
inline juce::Rectangle<float> measureChipsArea(juce::Rectangle<float> band, int numBars,
                                               float& chipW) {
    constexpr float kPrefixW = 30.0f;
    auto chips = band;
    if (band.getWidth() > kPrefixW + static_cast<float>(numBars) * 14.0f)
        chips.removeFromLeft(kPrefixW);   // place pour le préfixe "Mes"
    chipW = chips.getWidth() / static_cast<float>(juce::jmax(1, numBars));
    return chips;
}
