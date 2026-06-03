#include "HardwareStyleComponents.h"

namespace {

/// Largeur retirée à chaque cellule blanche : crée un **vrai intervalle** entre deux pads (les # se lisent
/// dans cet intervalle ; avec ~1 px l’œil voit encore les # « sur » la grille).
constexpr float kWhiteKeyTrim = 5.0f;

// Dièse #k est entre la blanche d’index `left` et `left+1` (grille piano sur 16 blanches).
constexpr int kBlackLeftWhiteIndex[11] = {0, 1, 3, 4, 5, 7, 8, 10, 11, 12, 14};

/// Touches noires plus grandes que l’intervalle entre blanches (chevauchement) pour libellés.
constexpr float kBlackKeyWidthFactor  = 0.88f;
constexpr float kBlackKeyHeightFactor = 1.08f;

}  // namespace

// --- PatternScreen ----------------------------------------------------------

namespace {
const char* harmonyModeShort(int m) {
    switch (m) {
        case 0:  return "A";
        case 1:  return "B1";
        case 2:  return "B2";
        default: return "CHR";
    }
}

juce::String midiNoteShort(int midi) {
    static const char* kN[12] = {"C", "C#", "D", "D#", "E", "F",
                                 "F#", "G", "G#", "A", "A#", "B"};
    if (midi < 0 || midi > 127) return "?";
    return juce::String(kN[midi % 12]) + juce::String(midi / 12 - 1);
}

// Palette TFT.
const juce::Colour kScreenBg     {0xff0a0c0a};
const juce::Colour kScreenBorder {0xff1a221a};
const juce::Colour kHeaderText   {0xff6ee7a0};
const juce::Colour kRowLabel     {0xff4dd888};
const juce::Colour kCellOn       {0xff7fd4a0};
const juce::Colour kCellOff      {0xff223026};
const juce::Colour kCellGrid     {0xff14301f};
const juce::Colour kPlayhead     {0xfff7a13a};
const juce::Colour kSelRowBg     {0x2240e090};
const juce::Colour kMutedText    {0xff8a5a5a};
const juce::Colour kSelStep      {0xffffffff};   // pas sélectionné (curseur) : blanc vif, hors palette
const juce::Colour kClipCut      {0xff5aa0ff};   // pas COUPÉ en attente (bleu) : « va être déplacé »
const juce::Colour kClipCopy     {0xff3a78c0};   // pas COPIÉ en attente (bleu plus sombre)
const juce::Colour kGhost        {0xff35d2d6};   // sous-pattern PARTAGÉ (alias) : triangle cyan, coin bas-gauche
const juce::Colour kSubSolo      {0xffff00ff};   // sous-pattern INDÉPENDANT : triangle magenta, coin bas-gauche

/// Teinte par pitch-class (0..11) : 12 couleurs distinctes, saturées mais non criardes,
/// lisibles sur le fond sombre TFT. Les cellules tombant sur la MÊME note jouée partagent
/// donc la même teinte → la « zone chromatique » du filtre harmonique se lit d'un coup d'œil.
juce::Colour pitchClassColour(int pc) {
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

PrLayout computePrLayout(const PatternScreenModel& m, juce::Rectangle<float> body) {
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

const char* romanNumeral(int degree) {
    static const char* kR[7] = {"I", "II", "III", "IV", "V", "VI", "VII"};
    return kR[juce::jlimit(0, 6, degree - 1)];
}

// Noms de gamme lisibles (miroir des 12 gammes du ScaleBank ; PAS d'include du core ici).
const char* kScaleNames[12] = {
    "major", "natural minor", "harmonic minor", "melodic minor",
    "dorian", "phrygian", "lydian", "mixolydian",
    "pent major", "pent minor", "blues", "chromatic"};

const char* scaleNameShort(int scaleId) {
    return kScaleNames[juce::jlimit(0, 11, scaleId)];
}

// Nom d'une note à partir de sa pitch-class (0..11), sans octave.
juce::String pitchClassName(int pc) {
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
juce::String extensionsShort(int bits) {
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
juce::String chordSuffix(int quality, int extensions) {
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
    juce::Rectangle<float> info, slotBand, rowsBand, detail, hint;  // info / slots / Rows / détail / hint
    float slotW = 10.0f;
    int   slotsToShow = 1;
};

HarmLayout computeHarmLayout(const PatternScreenModel& m, juce::Rectangle<float> body) {
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
    L.slotBand  = b;   // le reste = bande de slots d'accords
    L.slotsToShow = juce::jlimit(1, 32, juce::jmax(m.progLen, m.harmonyCursor + 1));
    L.slotW  = L.slotBand.getWidth() / static_cast<float>(L.slotsToShow);
    return L;
}

constexpr int kAutoNumSlots  = 8;
constexpr int kAutoNumFields = 2;
const char* autoFieldName(int field) {
    static const char* kF[2] = {"Valeur", "CC#"};
    return kF[juce::jlimit(0, 1, field)];
}

struct AutoLayout {
    juce::Rectangle<float> slotBand, fieldBand, lane, detail;
    float slotW = 10.0f, fieldW = 10.0f, cellW = 10.0f;
    int   n = 1;
};

AutoLayout computeAutoLayout(const PatternScreenModel& m, juce::Rectangle<float> body) {
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
RollFrame computeRollFrame(juce::Rectangle<float> body) {
    RollFrame f;
    auto b = body;
    f.lane = b.removeFromBottom(30.0f);
    b.removeFromBottom(3.0f);
    f.grid = b;
    return f;
}
}  // namespace

const char* PatternScreen::chordQualityShort(int quality) {
    // 6 triades de base (ChordQuality du core). Le suffixe complet (7e/5-alt/tensions) est
    // recomposé par chordSuffix ; cette fonction ne donne que la triade nue.
    static const char* kQ[6] = {"", "m", "dim", "aug", "sus2", "sus4"};
    return kQ[juce::jlimit(0, 5, quality)];
}

PatternScreen::PatternScreen() {
    setOpaque(false);
}

void PatternScreen::setModel(const PatternScreenModel& m) {
    model_ = m;
    recomputeLayout();
    repaint();
}

void PatternScreen::resized() {
    recomputeLayout();
}

const char* PatternScreen::pageLabel(int pageIndex) {
    static const char* kLabels[PatternScreenModel::kNumPages] =
        {"PAT", "ROLL", "HARM", "AUTO", "GLOB", "SONG"};
    return kLabels[juce::jlimit(0, PatternScreenModel::kNumPages - 1, pageIndex)];
}

void PatternScreen::recomputeLayout() {
    auto b      = getLocalBounds().toFloat().reduced(6.0f);
    tabBarArea_ = b.removeFromTop(20.0f);
    headerArea_ = b.removeFromTop(16.0f);
    bodyArea_   = b.reduced(2.0f);

    // Viewport vertical : nb de rows visibles = zoom utilisateur (rowZoom) sinon auto (≥ 22 px/row).
    // Fenêtre centrée sur la row sélectionnée (défile avec elle). Émule la contrainte 320×240.
    // La grille démarre SOUS le bandeau de mesures (PATTERN multi-mesures), sinon = bodyArea_.
    constexpr float kMinRowH = 22.0f;
    const auto grid   = gridArea();
    const int nr = juce::jmax(1, model_.numRows);
    const int autoVis = juce::jlimit(1, nr, static_cast<int>(grid.getHeight() / kMinRowH));
    visibleRows_ = (model_.rowZoom > 0) ? juce::jlimit(1, nr, model_.rowZoom) : autoVis;
    rowH_        = grid.getHeight() / static_cast<float>(visibleRows_);
    const int sel = juce::jlimit(0, nr - 1, model_.selectedRow);
    firstVisibleRow_ = juce::jlimit(0, juce::jmax(0, nr - visibleRows_), sel - visibleRows_ / 2);
}

// Bandeau de mesures : seulement en PATTERN, hors-sub, et si numBars>1. Hauteur fixe ~15px,
// rogné en haut de bodyArea_. Sinon rect vide (les autres vues ignorent ce bandeau).
juce::Rectangle<float> PatternScreen::measureBandArea() const {
    if (model_.page != PatternScreenModel::Page::Pattern || model_.inSub || model_.numBars <= 1)
        return {};
    constexpr float kBandH = 15.0f;
    return bodyArea_.withHeight(juce::jmin(kBandH, bodyArea_.getHeight() * 0.4f));
}

juce::Rectangle<float> PatternScreen::gridArea() const {
    const auto band = measureBandArea();
    return band.isEmpty() ? bodyArea_ : bodyArea_.withTrimmedTop(band.getHeight());
}

int PatternScreen::tabAtX(float x) const {
    if (tabBarArea_.getWidth() <= 0.0f)
        return -1;
    const float tabW = tabBarArea_.getWidth() / static_cast<float>(PatternScreenModel::kNumPages);
    const int   i    = static_cast<int>((x - tabBarArea_.getX()) / tabW);
    return juce::jlimit(0, PatternScreenModel::kNumPages - 1, i);
}

// Géométrie du bandeau de mesures : préfixe "Mes" optionnel à gauche puis numBars chips égales.
// Renvoie le rect des chips (hors préfixe) et la largeur d'une chip via les paramètres out.
static juce::Rectangle<float> measureChipsArea(juce::Rectangle<float> band, int numBars,
                                               float& chipW) {
    constexpr float kPrefixW = 30.0f;
    auto chips = band;
    if (band.getWidth() > kPrefixW + static_cast<float>(numBars) * 14.0f)
        chips.removeFromLeft(kPrefixW);   // place pour le préfixe "Mes"
    chipW = chips.getWidth() / static_cast<float>(juce::jmax(1, numBars));
    return chips;
}

int PatternScreen::measureAtX(float x) const {
    const auto band = measureBandArea();
    if (band.isEmpty())
        return -1;
    float chipW = 0.0f;
    const auto chips = measureChipsArea(band, model_.numBars, chipW);
    if (chipW <= 0.0f || x < chips.getX() || x >= chips.getRight())
        return -1;
    const int i = static_cast<int>((x - chips.getX()) / chipW);
    return juce::jlimit(0, model_.numBars - 1, i);
}

void PatternScreen::paintTabBar(juce::Graphics& g) {
    const int   active = static_cast<int>(model_.page);
    const float tabW   = tabBarArea_.getWidth() / static_cast<float>(PatternScreenModel::kNumPages);
    for (int i = 0; i < PatternScreenModel::kNumPages; ++i) {
        juce::Rectangle<float> chip(tabBarArea_.getX() + static_cast<float>(i) * tabW,
                                    tabBarArea_.getY(), tabW, tabBarArea_.getHeight());
        const auto inner = chip.reduced(1.5f, 1.0f);
        const bool on    = (i == active);
        g.setColour(on ? kHeaderText : kCellOff);
        g.fillRoundedRectangle(inner, 3.0f);
        g.setColour(on ? kScreenBg : kRowLabel);
        g.setFont(juce::Font(juce::FontOptions().withHeight(11.0f)
                                 .withStyle(on ? "Bold" : "")));
        g.drawText(pageLabel(i), inner, juce::Justification::centred);
    }
}

void PatternScreen::paint(juce::Graphics& g) {
    auto full = getLocalBounds().toFloat();
    g.setColour(kScreenBg);
    g.fillRoundedRectangle(full, 6.0f);
    g.setColour(kScreenBorder);
    g.drawRoundedRectangle(full.reduced(1.0f), 5.0f, 1.0f);

    paintTabBar(g);

    // Ligne de statut : fil d'Ariane (gauche) + transport + tempo/signature (droite).
    {
        auto header = headerArea_;
        const juce::String glyph =
            model_.playing ? juce::String(juce::CharPointer_UTF8("\xe2\x96\xb6"))   // ▶
                           : juce::String(juce::CharPointer_UTF8("\xe2\x96\xa0"));  // ■
        juce::String crumb;
        if (model_.inSub) {
            const juce::String arrow(juce::CharPointer_UTF8(" \xe2\x96\xb8 "));   // ▸
            const bool valid = (model_.subEditIdx >= 0 && model_.subEditIdx < 16);
            const bool rel   = valid && model_.subs[static_cast<size_t>(model_.subEditIdx)].relative;
            const int  dur   = valid ? model_.subs[static_cast<size_t>(model_.subEditIdx)].duration : 1;
            crumb = "R" + juce::String(model_.subHostRow + 1) + arrow
                  + "P" + juce::String(model_.subHostStep + 1) + arrow
                  + "SUB " + (rel ? "REL" : "ABS")
                  + (dur > 1 ? (" x" + juce::String(dur)) : juce::String());
        } else if (model_.page == PatternScreenModel::Page::Pattern
            || model_.page == PatternScreenModel::Page::PianoRoll
            || model_.page == PatternScreenModel::Page::Auto) {
            crumb = "R" + juce::String(model_.selectedRow + 1) + "/" + juce::String(juce::jmax(1, model_.numRows));
            // Indicateur de mesure éditée "Mes e/n". Si la lecture est sur une autre
            // mesure, on l'indique discrètement (▸joue X).
            crumb += "  Mes " + juce::String(model_.editBar + 1) + "/" + juce::String(juce::jmax(1, model_.numBars));
            if (model_.playing && model_.numBars > 1 && model_.playBar != model_.editBar) {
                const juce::String play(juce::CharPointer_UTF8("\xe2\x96\xb8"));   // ▸
                crumb += play + "joue" + juce::String(model_.playBar + 1);
            }
            if (model_.keyPageStart >= 0 && model_.keyPageCount > 1) {
                const int rowN = (model_.selectedRow < model_.numRows)
                                     ? juce::jlimit(1, 64, model_.rows[static_cast<size_t>(model_.selectedRow)].numSteps)
                                     : 16;
                const int end = juce::jmin(rowN, model_.keyPageStart + 16);
                crumb += "  P" + juce::String(model_.keyPageStart / 16 + 1) + "/" + juce::String(model_.keyPageCount)
                         + " p" + juce::String(model_.keyPageStart + 1) + "-" + juce::String(end);
            }
            if (model_.recArmed)
                crumb += "  REC";
        }
        g.setColour(kRowLabel);
        g.setFont(juce::Font(juce::FontOptions().withHeight(12.0f)));
        g.drawText(crumb, header.removeFromLeft(220.0f), juce::Justification::centredLeft);
        juce::String right = glyph + "  " + juce::String(model_.bpm, 1) + " BPM   Mesure "
                             + juce::String(model_.tsNum) + "/" + juce::String(model_.tsDen);
        g.drawText(right, header, juce::Justification::centredRight);
    }

    switch (model_.page) {
        case PatternScreenModel::Page::Pattern:   paintPatternPage(g); break;
        case PatternScreenModel::Page::Global:    paintGlobalPage(g); break;
        case PatternScreenModel::Page::PianoRoll: paintPianoRollPage(g); break;
        case PatternScreenModel::Page::Harmony:   paintHarmonyPage(g); break;
        case PatternScreenModel::Page::Auto:      paintAutoPage(g); break;
        case PatternScreenModel::Page::Song:
            paintStubPage(g, "SONG", "arrangement / chain - V1.5"); break;
    }
}

void PatternScreen::paintStubPage(juce::Graphics& g, const juce::String& title,
                                  const juce::String& subtitle) {
    g.setColour(kHeaderText);
    g.setFont(juce::Font(juce::FontOptions().withHeight(22.0f).withStyle("Bold")));
    auto top = bodyArea_.withTrimmedBottom(bodyArea_.getHeight() * 0.45f);
    g.drawText(title, top, juce::Justification::centredBottom);
    g.setColour(kRowLabel);
    g.setFont(juce::Font(juce::FontOptions().withHeight(13.0f)));
    g.drawText(subtitle, bodyArea_.withTrimmedTop(bodyArea_.getHeight() * 0.55f),
               juce::Justification::centredTop);
}

void PatternScreen::paintPatternPage(juce::Graphics& g) {
    if (model_.numRows <= 0)
        return;

    // Édition d'un subpattern (drill-in) : strip des N sous-pas du sub (tuplet imbriqué).
    if (model_.inSub && model_.subEditIdx >= 0 && model_.subEditIdx < 16) {
        const auto& sv   = model_.subs[static_cast<size_t>(model_.subEditIdx)];
        const int   sn   = juce::jlimit(1, 16, juce::jmax(1, sv.numSteps));
        auto        area = bodyArea_.reduced(4.0f);
        const float cw   = area.getWidth() / static_cast<float>(sn);
        for (int k = 0; k < sn; ++k) {
            juce::Rectangle<float> cell(area.getX() + static_cast<float>(k) * cw, area.getY(), cw, area.getHeight());
            const auto inner = cell.reduced(2.0f);
            g.setColour(sv.enabled[static_cast<size_t>(k)] ? kCellOn : kCellOff);
            g.fillRoundedRectangle(inner, 3.0f);
            g.setColour(kCellGrid);
            g.drawRoundedRectangle(inner.reduced(0.5f), 3.0f, 0.6f);
            if (k == model_.subStep) {
                g.setColour(kSelStep);   // blanc vif : sous-pas sélectionné, distinct du vert des cellules
                g.drawRoundedRectangle(inner.reduced(0.4f), 3.0f, 2.2f);
            }
        }
        return;
    }

    // Bandeau de mesures (multi-mesures) : chips cliquables au-dessus de la grille.
    // Mesure éditée = surlignée ; mesure jouée = liseré ambre (playhead).
    {
        const auto band = measureBandArea();
        if (!band.isEmpty()) {
            float chipW = 0.0f;
            const auto chips = measureChipsArea(band, model_.numBars, chipW);
            if (chips.getX() > band.getX() + 1.0f) {   // préfixe "Mes" si la place existe
                g.setColour(kRowLabel);
                g.setFont(juce::Font(juce::FontOptions().withHeight(10.0f)));
                g.drawText("Mes", band.withWidth(chips.getX() - band.getX()).reduced(2.0f, 0.0f),
                           juce::Justification::centredLeft);
            }
            for (int b = 0; b < model_.numBars; ++b) {
                juce::Rectangle<float> chip(chips.getX() + static_cast<float>(b) * chipW,
                                            chips.getY(), chipW, chips.getHeight());
                const auto inner = chip.reduced(1.5f, 1.5f);
                const bool edited = (b == model_.editBar);
                const bool played = model_.playing && (b == model_.playBar);
                g.setColour(edited ? kHeaderText : kCellOff);
                g.fillRoundedRectangle(inner, 3.0f);
                g.setColour(edited ? kScreenBg : kRowLabel);
                g.setFont(juce::Font(juce::FontOptions().withHeight(10.0f)
                                         .withStyle(edited ? "Bold" : "")));
                g.drawText(juce::String(b + 1), inner, juce::Justification::centred);
                if (played) {   // marqueur de lecture : liseré ambre autour de la chip
                    g.setColour(kPlayhead);
                    g.drawRoundedRectangle(inner.reduced(0.4f), 3.0f, 1.6f);
                }
            }
        }
    }

    const auto  grid   = gridArea();
    const float stripX = grid.getX() + gutterW_;
    const float stripW = juce::jmax(10.0f, grid.getWidth() - gutterW_ - infoW_);

    // Zoom horizontal : fenêtre = 1/stepZoom de la mesure, centrée sur le pas sélectionné.
    const int   zoom    = juce::jlimit(1, 8, model_.stepZoom);
    const float winLen  = 1.0f / static_cast<float>(zoom);
    float       winStart = 0.0f;
    if (zoom > 1) {
        const int   selRow = juce::jlimit(0, model_.numRows - 1, model_.selectedRow);
        const int   selN   = juce::jlimit(1, 64, model_.rows[static_cast<size_t>(selRow)].numSteps);
        const float selPos = (static_cast<float>(model_.selectedStep) + 0.5f) / static_cast<float>(selN);
        winStart = juce::jlimit(0.0f, 1.0f - winLen, selPos - winLen * 0.5f);
    }
    auto barToX = [stripX, stripW, winStart, winLen](float frac) {
        return stripX + (frac - winStart) / winLen * stripW;
    };
    const juce::Rectangle<int> stripClip(juce::roundToInt(stripX), juce::roundToInt(grid.getY()),
                                         juce::roundToInt(stripW), juce::roundToInt(grid.getHeight()));

    const int rEnd = juce::jmin(model_.numRows, firstVisibleRow_ + visibleRows_);
    for (int r = firstVisibleRow_; r < rEnd; ++r) {
        const auto& row = model_.rows[static_cast<size_t>(r)];
        const float y   = grid.getY() + static_cast<float>(r - firstVisibleRow_) * rowH_;
        juce::Rectangle<float> rowRect(grid.getX(), y, grid.getWidth(), rowH_);

        if (r == model_.selectedRow) {
            g.setColour(kSelRowBg);
            g.fillRoundedRectangle(rowRect.reduced(1.0f), 3.0f);
        }

        // Libellé row (gauche).
        g.setColour(r == model_.selectedRow ? kHeaderText : kRowLabel);
        g.setFont(juce::Font(juce::FontOptions().withHeight(12.0f)
                                 .withStyle(r == model_.selectedRow ? "Bold" : "")));
        g.drawText("R" + juce::String(r + 1),
                   juce::Rectangle<float>(grid.getX(), y, gutterW_, rowH_).reduced(2.0f),
                   juce::Justification::centredLeft);

        // Strip de pas : N cellules réparties sur la fenêtre-mesure (zoom horizontal), clippées au strip.
        const int   n        = juce::jlimit(1, 64, row.numSteps);
        const float cellWz   = (1.0f / static_cast<float>(n)) / winLen * stripW;
        const float cellPad  = cellWz > 10.0f ? 1.5f : 0.5f;
        const bool  showNote = cellWz >= 22.0f;
        // Pas RECOUVERTS : pour chaque s, chercher un owner antérieur (enabled, owner+span(owner) > s).
        // Même règle que le moteur (masquage stateless) : le pas couvert conserve son contenu mais
        // ne déclenche pas → on ne le dessine pas comme cellule active (la note longue/sub de l'owner
        // couvre visuellement la zone). Le pas sélectionné reste affiché (liseré) pour rester éditable.
        bool covered[64] = {};
        for (int s = 0; s < n; ++s) {
            for (int o = s - 1; o >= 0; --o) {
                if (!row.enabled[static_cast<size_t>(o)])
                    continue;
                const int osp = juce::jlimit(1, n - o, juce::jmax(1, static_cast<int>(row.span[static_cast<size_t>(o)])));
                if (o + osp > s) { covered[s] = true; break; }
            }
        }
        {
            juce::Graphics::ScopedSaveState clip(g);
            g.reduceClipRegion(stripClip);
            for (int s = 0; s < n; ++s) {
                const float cx = barToX(static_cast<float>(s) / static_cast<float>(n));
                if (cx + cellWz <= stripX || cx >= stripX + stripW)
                    continue;  // hors fenêtre
                juce::Rectangle<float> cell(cx, y + 2.0f, cellWz, rowH_ - 4.0f);
                const auto inner = cell.reduced(cellPad, cellPad);
                const bool isSel = (r == model_.selectedRow && s == model_.selectedStep);
                // Pas recouvert par un owner antérieur : masqué (pas de rendu actif), sauf s'il est
                // sélectionné (on le rend pour l'éditer). Le liseré blanc de sélection est dessiné plus bas.
                if (covered[static_cast<size_t>(s)] && !isSel)
                    continue;
                const bool on    = row.enabled[static_cast<size_t>(s)];
                const bool ph    = (s == row.playhead);
                // Span du pas (note longue / étalement sub) : largeur réelle couverte, clampée à n-s.
                const int  span  = juce::jlimit(1, juce::jmax(1, n - s),
                                                juce::jmax(1, static_cast<int>(row.span[static_cast<size_t>(s)])));

                const int  subI   = row.subIdx[static_cast<size_t>(s)];
                const bool hasSub = (subI >= 0 && subI < 16
                                     && model_.subs[static_cast<size_t>(subI)].numSteps > 0);
                // Étendue visuelle du slot (cellules-hôtes) : un sub s'étale sur le span du pas
                // hôte (le core fait primer hostSpan ; fallback sv.duration), une note longue sur
                // son span. Le SUB et la NOTE LONGUE occupent donc une SEULE cellule étirée — même
                // apparence qu'à span=1, juste plus large (pas de carré + sub superposés).
                const int  subSpan = hasSub
                    ? ((span > 1) ? span
                       : juce::jlimit(1, juce::jmax(1, n - s),
                                      juce::jmax(1, static_cast<int>(model_.subs[static_cast<size_t>(subI)].duration))))
                    : 1;
                const bool longNote = (on && span > 1 && !hasSub);
                const int  visSpan  = hasSub ? subSpan : (longNote ? span : 1);
                const float fillW   = (visSpan > 1) ? (cellWz * static_cast<float>(visSpan) - 2.0f * cellPad)
                                                    : inner.getWidth();
                juce::Rectangle<float> fill(inner.getX(), inner.getY(),
                                            juce::jmax(inner.getWidth(), fillW), inner.getHeight());

                // Luminosité de la cellule active ∝ vélocité (feedback d'un coup d'œil).
                const float velA = 0.45f + 0.55f * (static_cast<float>(row.velocity[static_cast<size_t>(s)]) / 127.0f);
                // Atténuation visuelle d'un pas-hôte DÉSACTIVÉ qui porte un sub : sub + fond grisés
                // pour qu'on distingue clairement « ce pas (et son sub) ne joue pas » d'un pas activé.
                const float subAlpha = (hasSub && !on) ? 0.30f : 1.0f;
                if (hasSub) {
                    // Sub : fond sombre ; les sous-pas verts se dessinent dessus (les écarts entre
                    // eux laissent voir ce fond = séparateurs). Désactivé = fond encore plus atténué.
                    g.setColour(kCellOff.withMultipliedAlpha(subAlpha));
                } else if (on && !row.muted && row.harmonyBound) {
                    // Row soumise à l'harmonie : teinte ∝ pitch-class de la note JOUÉE (après filtre)
                    // → les cellules d'une même « zone chromatique » partagent la même couleur.
                    g.setColour(pitchClassColour(row.playedNote[static_cast<size_t>(s)] % 12)
                                    .withMultipliedBrightness(velA));
                } else {
                    g.setColour(on ? (row.muted ? kCellOn.withAlpha(0.30f) : kCellOn.withAlpha(velA)) : kCellOff);
                }
                g.fillRoundedRectangle(fill, 2.0f);
                // Bordure : ambre UNIQUEMENT pour le pas joué (playhead) ; sinon grille. Un sub se
                // distingue déjà par son contenu (sous-pas), inutile de lui donner la couleur du playhead.
                if (ph) {
                    g.setColour(kPlayhead);
                    g.drawRoundedRectangle(fill.reduced(0.5f), 2.0f, 1.5f);
                } else {
                    g.setColour(kCellGrid);
                    g.drawRoundedRectangle(fill.reduced(0.5f), 2.0f, 0.6f);
                }
                // Subpattern niché : sous-pas en sens NORMAL (actif = vert, inactif = sombre),
                // avec séparateurs visibles (écart révélant le fond sombre).
                if (hasSub) {
                    const auto& sv  = model_.subs[static_cast<size_t>(subI)];
                    const int   sn  = juce::jlimit(1, 16, sv.numSteps);
                    const float mw  = fill.getWidth() / static_cast<float>(sn);
                    for (int k = 0; k < sn; ++k) {
                        juce::Rectangle<float> mc(fill.getX() + static_cast<float>(k) * mw,
                                                  fill.getY(), mw, fill.getHeight());
                        g.setColour((sv.enabled[static_cast<size_t>(k)] ? kCellOn : kCellGrid)
                                        .withMultipliedAlpha(subAlpha));
                        g.fillRect(mc.reduced(0.8f, 1.5f));   // l'écart = séparateur (fond sombre derrière)
                    }
                } else if (showNote && on) {
                    g.setColour(kScreenBg);
                    g.setFont(juce::Font(juce::FontOptions().withHeight(juce::jmin(11.0f, rowH_ - 6.0f))));
                    // Note RÉSOLUE (ce qui sonne) : sur une row liée, c'est la note tirée par le filtre.
                    // Centrée sur la cellule étendue `fill` (note longue) pour rester lisible.
                    g.drawText(midiNoteShort(row.playedNote[static_cast<size_t>(s)]), fill,
                               juce::Justification::centred);
                }
                // Marqueur « snappé » : la note stockée était hors filtre (tirée vers une note autorisée).
                // Petit triangle discret au coin supérieur gauche ; omis si la cellule est trop étroite.
                if (on && row.snapped[static_cast<size_t>(s)] && cellWz >= 10.0f) {
                    juce::Path tri;
                    const float tx = inner.getX() + 1.0f;
                    const float ty = inner.getY() + 1.0f;
                    const float ts = juce::jmin(4.0f, inner.getWidth() * 0.4f);
                    tri.addTriangle(tx, ty, tx + ts, ty, tx, ty + ts);
                    g.setColour(kPlayhead.withAlpha(0.85f));
                    g.fillPath(tri);
                }
                // Badge mode REL/ABS sur CHAQUE pas portant un sub (vue d'ensemble, pas seulement
                // le pas édité). Compact : "REL"/"ABS" si la place existe, sinon lettre R/A, sinon
                // pastille. Coin sup. droit de l'étendue du sub. Vert = REL, ambre = ABS.
                if (hasSub) {
                    const bool rel = model_.subs[static_cast<size_t>(subI)].relative;
                    const juce::Colour bg = (rel ? kCellOn : kPlayhead).withAlpha(0.92f * subAlpha);
                    const float fw = fill.getWidth();
                    if (fw >= 22.0f) {
                        juce::Rectangle<float> badge(fill.getRight() - 21.0f, fill.getY() + 1.0f, 20.0f, 9.0f);
                        g.setColour(bg);
                        g.fillRoundedRectangle(badge, 2.0f);
                        g.setColour(kScreenBg);
                        g.setFont(juce::Font(juce::FontOptions().withHeight(7.5f).withStyle("Bold")));
                        g.drawText(rel ? "REL" : "ABS", badge, juce::Justification::centred);
                    } else if (fw >= 11.0f) {
                        juce::Rectangle<float> badge(fill.getRight() - 8.0f, fill.getY() + 1.0f, 7.0f, 8.0f);
                        g.setColour(bg);
                        g.fillRoundedRectangle(badge, 1.5f);
                        g.setColour(kScreenBg);
                        g.setFont(juce::Font(juce::FontOptions().withHeight(7.0f).withStyle("Bold")));
                        g.drawText(rel ? "R" : "A", badge, juce::Justification::centred);
                    } else {
                        g.setColour(bg);
                        g.fillEllipse(fill.getRight() - 4.0f, fill.getY() + 1.0f, 3.0f, 3.0f);
                    }
                }
                // Marqueur de STATUT du sub au coin BAS-GAUCHE (miroir du triangle « snappé » ambre
                // en haut-gauche) : MAGENTA = sub indépendant (propre à ce pas), CYAN = sub partagé
                // (alias/ghost référencé par ≥2 pas). Toujours présent dès qu'un pas porte un sub.
                if (hasSub) {
                    const float gs = juce::jmin(8.0f, fill.getWidth() * 0.55f);
                    const float gx = fill.getX() + 0.5f;
                    const float gy = fill.getBottom() - 0.5f;
                    juce::Path gtri;
                    gtri.addTriangle(gx, gy, gx + gs, gy, gx, gy - gs);
                    g.setColour((row.subShared[static_cast<size_t>(s)] ? kGhost : kSubSolo).withAlpha(subAlpha));
                    g.fillPath(gtri);
                }
                // Presse-papier (copié/coupé) en attente. Le GRAIN (clipScope) fixe l'étendue :
                // PAS = cellule ancre seule ; ROW = toute la row source (bande) ; MESURE = toute
                // la grille. Bande = voile bleu translucide ; la cellule ANCRE garde un liseré pour
                // repérer l'origine. Coupé = bleu vif, copié = bleu sombre.
                const bool clipActive = (model_.clipRow >= 0);
                const bool inClipScope = clipActive
                    && (model_.clipScope == 2
                        || (model_.clipScope == 1 && r == model_.clipRow)
                        || (model_.clipScope == 0 && r == model_.clipRow && s == model_.clipStep));
                if (inClipScope) {
                    const auto clipRect = (visSpan > 1 ? fill : inner);
                    const float wash = model_.clipCut ? 0.22f : 0.14f;
                    g.setColour((model_.clipCut ? kClipCut : kClipCopy).withAlpha(wash));
                    g.fillRoundedRectangle(clipRect.reduced(0.5f), 2.0f);
                }
                const bool isClipAnchor = clipActive && r == model_.clipRow && s == model_.clipStep;
                if (isClipAnchor) {
                    const auto clipRect = (visSpan > 1 ? fill : inner);
                    g.setColour(model_.clipCut ? kClipCut : kClipCopy);
                    g.drawRoundedRectangle(clipRect.reduced(0.3f), 2.0f, 1.6f);
                }
                // Pas sélectionné (curseur Enc2) sur la row sélectionnée : liseré blanc vif
                // (hors palette vert/ambre) pour bien distinguer le slot édité du playhead et du reste.
                if (isSel) {
                    g.setColour(kSelStep);
                    // Le liseré englobe toute l'étendue du slot (note longue ou sub multi-pas).
                    const auto selRect = (visSpan > 1 ? fill : inner);
                    g.drawRoundedRectangle(selRect.reduced(0.3f), 2.0f, 2.2f);
                }
            }
        }

        // Infos row (droite) : N · valeur musicale · durée d'un pas (ms)  canal mode [mute].
        // N = divisions de la mesure ; la signature (Mesure x/y, en-tête) en est indépendante.
        juce::String info = "N" + juce::String(n);
        if (row.divLabel.isNotEmpty())
            info += juce::String::fromUTF8(" \xc2\xb7 ") + row.divLabel;   // « · »
        if (row.stepMs > 0)
            info += juce::String::fromUTF8(" \xc2\xb7 ") + juce::String(row.stepMs) + "ms";
        info += "  c" + juce::String(row.channel) + " " + harmonyModeShort(row.harmonyMode);
        if (row.muted)
            info += " M";
        g.setColour(row.muted ? kMutedText : kRowLabel);
        // Police réduite quand l'info est enrichie (zone infoW_ étroite ~96px).
        g.setFont(juce::Font(juce::FontOptions().withHeight(row.divLabel.isNotEmpty() ? 9.5f : 11.0f)));
        g.drawText(info,
                   juce::Rectangle<float>(stripX + stripW + 4.0f, y, infoW_ - 4.0f, rowH_),
                   juce::Justification::centredLeft);
    }

    // Indicateur de fenêtre de page (16 pas que les touches éditent) sur la row sélectionnée.
    if (model_.keyPageStart >= 0 && model_.selectedRow >= 0 && model_.selectedRow < model_.numRows) {
        const auto& row   = model_.rows[static_cast<size_t>(model_.selectedRow)];
        const int   n     = juce::jlimit(1, 64, row.numSteps);
        const int   start = juce::jlimit(0, n - 1, model_.keyPageStart);
        const int   end   = juce::jmin(n, start + 16);
        const float bx0   = juce::jlimit(stripX, stripX + stripW, barToX(static_cast<float>(start) / n));
        const float bx1   = juce::jlimit(stripX, stripX + stripW, barToX(static_cast<float>(end) / n));
        const float y     = grid.getY() + static_cast<float>(model_.selectedRow - firstVisibleRow_) * rowH_;
        if (bx1 - bx0 > 1.0f) {
            juce::Rectangle<float> box(bx0, y + 1.0f, bx1 - bx0, rowH_ - 2.0f);
            g.setColour(kPlayhead.withAlpha(0.12f));
            g.fillRoundedRectangle(box, 2.5f);
            g.setColour(kPlayhead);
            g.drawRoundedRectangle(box.reduced(0.5f), 2.5f, 1.8f);
        }
    }

    // Indicateurs de défilement vertical (rows hors champ au-dessus / en dessous).
    g.setColour(kRowLabel);
    g.setFont(juce::Font(juce::FontOptions().withHeight(10.0f)));
    if (firstVisibleRow_ > 0)
        g.drawText(juce::CharPointer_UTF8("\xe2\x96\xb2"),  // ▲
                   juce::Rectangle<float>(grid.getRight() - 12.0f, grid.getY(), 12.0f, 10.0f),
                   juce::Justification::centred);
    if (firstVisibleRow_ + visibleRows_ < model_.numRows)
        g.drawText(juce::CharPointer_UTF8("\xe2\x96\xbc"),  // ▼
                   juce::Rectangle<float>(grid.getRight() - 12.0f, grid.getBottom() - 10.0f, 12.0f, 10.0f),
                   juce::Justification::centred);
}

void PatternScreen::paintSubRoll(juce::Graphics& g) {
    if (model_.subEditIdx < 0 || model_.subEditIdx >= 16) {
        paintStubPage(g, "SUB", "—");
        return;
    }
    const auto& sv = model_.subs[static_cast<size_t>(model_.subEditIdx)];
    const int   sn = juce::jlimit(1, 16, juce::jmax(1, sv.numSteps));
    // Réserve une lane vélo en bas (même esprit que le piano-roll normal).
    const float subLaneH = juce::jlimit(14.0f, 26.0f, bodyArea_.getHeight() * 0.22f);
    auto        bodyTop  = bodyArea_.withTrimmedBottom(subLaneH);
    const juce::Rectangle<float> velLane(bodyArea_.getX(), bodyTop.getBottom(),
                                         bodyArea_.getWidth(), subLaneH);
    auto        plot = bodyTop.withTrimmedLeft(26.0f).reduced(2.0f);
    if (plot.getHeight() < 10.0f || plot.getWidth() < 10.0f)
        return;

    const float laneH   = juce::jlimit(7.0f, 16.0f, plot.getHeight() / 24.0f);
    const int   visible = juce::jmax(1, static_cast<int>(plot.getHeight() / laneH));
    auto displayPitch = [&](int k) {
        return sv.relative ? juce::jlimit(0, 127, model_.subHostNote + (static_cast<int>(sv.note[static_cast<size_t>(k)]) - 64))
                           : static_cast<int>(sv.note[static_cast<size_t>(k)]);
    };
    int center = sv.relative ? model_.subHostNote : 60;
    if (!sv.relative) {
        int sum = 0, cnt = 0;
        for (int k = 0; k < sn; ++k) if (sv.enabled[static_cast<size_t>(k)]) { sum += displayPitch(k); ++cnt; }
        if (cnt) center = sum / cnt;
    }
    int low = juce::jlimit(0, juce::jmax(0, 127 - (visible - 1)), center - visible / 2);
    const int   topNote = low + visible - 1;
    const float cellW   = plot.getWidth() / static_cast<float>(sn);

    // Lanes + libellés Do.
    for (int i = 0; i < visible; ++i) {
        const int   note = topNote - i;
        const float y    = plot.getY() + static_cast<float>(i) * laneH;
        if ((note % 12) == 0) {
            g.setColour(kCellGrid);
            g.fillRect(juce::Rectangle<float>(plot.getX(), y, plot.getWidth(), laneH));
            g.setColour(kRowLabel);
            g.setFont(juce::Font(juce::FontOptions().withHeight(9.0f)));
            g.drawText(midiNoteShort(note), juce::Rectangle<float>(bodyArea_.getX(), y, 24.0f, laneH),
                       juce::Justification::centredLeft);
        }
        g.setColour(kScreenBorder);
        g.drawLine(plot.getX(), y, plot.getRight(), y, 0.4f);
    }
    // Ligne d'ancrage (mode relatif) = la note du pas hôte.
    if (sv.relative) {
        const int lane = topNote - model_.subHostNote;
        if (lane >= 0 && lane < visible) {
            const float y = plot.getY() + static_cast<float>(lane) * laneH + laneH * 0.5f;
            g.setColour(kPlayhead);
            g.drawLine(plot.getX(), y, plot.getRight(), y, 1.4f);
            g.setFont(juce::Font(juce::FontOptions().withHeight(9.0f)));
            g.drawText("ancre " + midiNoteShort(model_.subHostNote),
                       juce::Rectangle<float>(plot.getX() + 2.0f, y - laneH, 70.0f, laneH),
                       juce::Justification::centredLeft);
        }
    }
    // Colonnes (sous-pas) + curseur + blocs de notes.
    for (int k = 0; k < sn; ++k) {
        const float x = plot.getX() + static_cast<float>(k) * cellW;
        if (k == model_.subStep) {
            g.setColour(kSelStep.withAlpha(0.16f));   // colonne du sous-pas sélectionné : voile blanc
            g.fillRect(juce::Rectangle<float>(x, plot.getY(), cellW, plot.getHeight()));
        }
        g.setColour(kScreenBorder);
        g.drawLine(x, plot.getY(), x, plot.getBottom(), 0.4f);
        if (sv.enabled[static_cast<size_t>(k)]) {
            const int lane = topNote - displayPitch(k);
            if (lane >= 0 && lane < visible) {
                juce::Rectangle<float> blk(x, plot.getY() + static_cast<float>(lane) * laneH, cellW, laneH);
                g.setColour(kCellOn);
                g.fillRoundedRectangle(blk.reduced(1.0f), 2.0f);
            }
        }
    }

    // Lane vélo (bas) : histogramme de la vélo par sous-pas (visualisation + clic).
    // Alignée sur les colonnes du sub-roll (même plot.getX()/cellW).
    {
        auto lane = velLane.reduced(2.0f);
        g.setColour(kRowLabel);
        g.setFont(juce::Font(juce::FontOptions().withHeight(9.0f)));
        g.drawText("Vélo",
                   juce::Rectangle<float>(bodyArea_.getX(), lane.getY(), 24.0f, lane.getHeight()),
                   juce::Justification::centredLeft);
        g.setColour(kScreenBorder);
        g.drawLine(plot.getX(), lane.getY(), plot.getRight(), lane.getY(), 0.6f);
        for (int k = 0; k < sn; ++k) {
            const float x = plot.getX() + static_cast<float>(k) * cellW;
            if (k == model_.subStep) {
                g.setColour(kSelStep.withAlpha(0.16f));
                g.fillRect(juce::Rectangle<float>(x, lane.getY(), cellW, lane.getHeight()));
            }
            if (sv.enabled[static_cast<size_t>(k)]) {
                const float h = lane.getHeight()
                              * juce::jlimit(0.0f, 1.0f, static_cast<float>(sv.velocity[static_cast<size_t>(k)]) / 127.0f);
                g.setColour(kCellOn);
                g.fillRoundedRectangle(juce::Rectangle<float>(x + 1.0f, lane.getBottom() - h,
                                                              cellW - 2.0f, h), 1.0f);
            }
        }
    }
}

void PatternScreen::paintPianoRollPage(juce::Graphics& g) {
    if (model_.inSub) { paintSubRoll(g); return; }   // re-cible le piano-roll sur le sub
    const RollFrame F = computeRollFrame(bodyArea_);
    const PrLayout  L = computePrLayout(model_, F.grid);
    if (!L.valid) {
        paintStubPage(g, "PIANO ROLL", "selectionne une row");
        return;
    }
    const int   r   = juce::jlimit(0, model_.numRows - 1, model_.selectedRow);
    const auto& row = model_.rows[static_cast<size_t>(r)];

    // Lanes de hauteur : bande + libellé sur les Do, ligne fine ailleurs.
    for (int i = 0; i < L.visibleLanes; ++i) {
        const int   note = L.topNote - i;
        const float y    = L.plot.getY() + static_cast<float>(i) * L.laneH;
        if ((note % 12) == 0) {
            g.setColour(kCellGrid);
            g.fillRect(juce::Rectangle<float>(L.plot.getX(), y, L.plot.getWidth(), L.laneH));
            g.setColour(kRowLabel);
            g.setFont(juce::Font(juce::FontOptions().withHeight(9.0f)));
            g.drawText(midiNoteShort(note),
                       juce::Rectangle<float>(bodyArea_.getX(), y, 24.0f, L.laneH),
                       juce::Justification::centredLeft);
        }
        g.setColour(kScreenBorder);
        g.drawLine(L.plot.getX(), y, L.plot.getRight(), y, 0.4f);
    }

    // Pas RECOUVERTS par le span d'un pas-hôte antérieur (même règle que paintPatternPage) :
    // ils ne déclenchent pas → ni note, ni sous-pas, ni badge de mode dessinés. Recalculé
    // chaque frame, suit le span courant.
    bool coveredRoll[64] = {};
    for (int s = 0; s < L.n; ++s)
        for (int o = s - 1; o >= 0; --o) {
            if (!row.enabled[static_cast<size_t>(o)])
                continue;
            const int osp = juce::jlimit(1, L.n - o, juce::jmax(1, static_cast<int>(row.span[static_cast<size_t>(o)])));
            if (o + osp > s) { coveredRoll[s] = true; break; }
        }

    // Colonnes (cases du tuplet) : curseur de pas + playhead + séparateurs.
    for (int s = 0; s < L.n; ++s) {
        const float x = L.plot.getX() + static_cast<float>(s) * L.cellW;
        if (s == model_.selectedStep) {
            // La surbrillance du pas sélectionné couvre tout son SPAN (note longue / sub étalé),
            // pas une seule cellule — elle suit la taille réglée par le push →Span.
            const int   sp = juce::jlimit(1, juce::jmax(1, L.n - s),
                                          juce::jmax(1, static_cast<int>(row.span[static_cast<size_t>(s)])));
            g.setColour(kSelRowBg);
            g.fillRect(juce::Rectangle<float>(x, L.plot.getY(),
                                              L.cellW * static_cast<float>(sp), L.plot.getHeight()));
        }
        if (s == row.playhead) {
            g.setColour(kPlayhead.withAlpha(0.22f));
            g.fillRect(juce::Rectangle<float>(x, L.plot.getY(), L.cellW, L.plot.getHeight()));
        }
        g.setColour(kScreenBorder);
        g.drawLine(x, L.plot.getY(), x, L.plot.getBottom(), 0.4f);
    }

    // Badge mode REL/ABS sur CHAQUE pas portant un sub (vue d'ensemble), en haut de sa colonne.
    // Compact selon la largeur : "REL"/"ABS", sinon R/A, sinon pastille. Vert = REL, ambre = ABS.
    for (int s = 0; s < L.n; ++s) {
        if (coveredRoll[static_cast<size_t>(s)])
            continue;   // pas recouvert par un span antérieur : pas de badge
        const int subI = static_cast<int>(row.subIdx[static_cast<size_t>(s)]);
        if (subI < 0 || subI >= 16 || model_.subs[static_cast<size_t>(subI)].numSteps == 0)
            continue;
        const bool  rel = model_.subs[static_cast<size_t>(subI)].relative;
        const juce::Colour bg = (rel ? kCellOn : kPlayhead).withAlpha(0.92f);
        const float x   = L.plot.getX() + static_cast<float>(s) * L.cellW;
        if (L.cellW >= 22.0f) {
            juce::Rectangle<float> badge(x + 1.0f, L.plot.getY() + 1.0f, 20.0f, 9.0f);
            g.setColour(bg);
            g.fillRoundedRectangle(badge, 2.0f);
            g.setColour(kScreenBg);
            g.setFont(juce::Font(juce::FontOptions().withHeight(7.5f).withStyle("Bold")));
            g.drawText(rel ? "REL" : "ABS", badge, juce::Justification::centred);
        } else if (L.cellW >= 10.0f) {
            juce::Rectangle<float> badge(x + 1.0f, L.plot.getY() + 1.0f, 7.0f, 8.0f);
            g.setColour(bg);
            g.fillRoundedRectangle(badge, 1.5f);
            g.setColour(kScreenBg);
            g.setFont(juce::Font(juce::FontOptions().withHeight(7.0f).withStyle("Bold")));
            g.drawText(rel ? "R" : "A", badge, juce::Justification::centred);
        } else {
            g.setColour(bg);
            g.fillEllipse(x + 1.0f, L.plot.getY() + 1.0f, 3.0f, 3.0f);
        }
    }

    // Notes posées : un bloc par pas actif dont la hauteur tombe dans la fenêtre.
    // NB : un pas qui porte un sub valide est rendu plus bas (mini-blocs) — on le saute ici
    // pour éviter le doublon avec le bloc « note simple » de la note hôte.
    for (int s = 0; s < L.n; ++s) {
        if (!row.enabled[static_cast<size_t>(s)] || coveredRoll[static_cast<size_t>(s)])
            continue;
        {
            const int subI = static_cast<int>(row.subIdx[static_cast<size_t>(s)]);
            if (subI >= 0 && subI < 16 && model_.subs[static_cast<size_t>(subI)].numSteps > 0)
                continue;   // rendu par la boucle sous-patterns (mini-blocs)
        }
        const int lane = L.topNote - static_cast<int>(row.note[static_cast<size_t>(s)]);
        if (lane < 0 || lane >= L.visibleLanes)
            continue;
        const float x = L.plot.getX() + static_cast<float>(s) * L.cellW;
        const float y = L.plot.getY() + static_cast<float>(lane) * L.laneH;
        // Largeur ∝ gate (articulation), luminosité ∝ vélocité.
        const float gateFrac = juce::jlimit(0.1f, 1.0f, static_cast<float>(row.gate[static_cast<size_t>(s)]) / 100.0f);
        const float velA     = 0.45f + 0.55f * (static_cast<float>(row.velocity[static_cast<size_t>(s)]) / 127.0f);
        juce::Rectangle<float> block(x, y, juce::jmax(2.0f, L.cellW * gateFrac), L.laneH);
        g.setColour(row.muted ? kCellOn.withAlpha(0.4f) : kCellOn.withAlpha(velA));
        g.fillRoundedRectangle(block.reduced(1.0f, 1.0f), 2.0f);
        if (s == row.playhead) {
            g.setColour(kPlayhead);
            g.drawRoundedRectangle(block.reduced(0.8f, 0.8f), 2.0f, 1.2f);
        }
    }

    // Sous-patterns (LECTURE SEULE) : pour chaque pas actif portant un sub valide, on dessine
    // ses sous-pas comme des mini-blocs répartis horizontalement sur le span du pas hôte, placés
    // verticalement à LEUR vraie hauteur (relatif = note hôte + offset ; absolu = note brute).
    // L'édition reste le drill-in : aucun hit-test n'est ajouté ici.
    for (int s = 0; s < L.n; ++s) {
        if (!row.enabled[static_cast<size_t>(s)] || coveredRoll[static_cast<size_t>(s)])
            continue;
        const int subI = static_cast<int>(row.subIdx[static_cast<size_t>(s)]);
        if (subI < 0 || subI >= 16 || model_.subs[static_cast<size_t>(subI)].numSteps <= 0)
            continue;
        const auto& sv = model_.subs[static_cast<size_t>(subI)];
        const int   sn = juce::jlimit(1, 16, juce::jmax(1, sv.numSteps));
        // Étendue (cellules-hôtes) : le span du pas hôte prime, fallback sv.duration ; clamp à n-s.
        const int   hostSpan = static_cast<int>(row.span[static_cast<size_t>(s)]);
        const int   span = juce::jlimit(1, juce::jmax(1, L.n - s),
                                        (hostSpan > 1) ? hostSpan
                                                       : juce::jmax(1, sv.duration));
        const float x0       = L.plot.getX() + static_cast<float>(s) * L.cellW;
        const float zoneW    = static_cast<float>(span) * L.cellW;
        const float subCellW = zoneW / static_cast<float>(sn);
        for (int k = 0; k < sn; ++k) {
            if (!sv.enabled[static_cast<size_t>(k)])
                continue;   // sous-pas inactif : pas de bloc
            const int pitch = juce::jlimit(0, 127,
                sv.relative ? (static_cast<int>(row.note[static_cast<size_t>(s)])
                               + (static_cast<int>(sv.note[static_cast<size_t>(k)]) - PatternScreenModel::kSubRelCenter))
                            : static_cast<int>(sv.note[static_cast<size_t>(k)]));
            const int lane = L.topNote - pitch;
            if (lane < 0 || lane >= L.visibleLanes)
                continue;   // hors fenêtre verticale (comme les notes simples)
            const float x = x0 + static_cast<float>(k) * subCellW;
            const float y = L.plot.getY() + static_cast<float>(lane) * L.laneH;
            juce::Rectangle<float> mc(x, y, subCellW, L.laneH);
            // Teinte légèrement distincte (plus claire) pour signaler un sous-pas vs note simple.
            g.setColour(row.muted ? kCellOn.withAlpha(0.4f) : kCellOn.brighter(0.25f).withAlpha(0.9f));
            g.fillRoundedRectangle(mc.reduced(0.8f, 1.0f), 1.5f);
        }
        // Marqueur de STATUT du sub au coin bas-gauche de la zone : magenta = indépendant,
        // cyan = partagé (alias/ghost). Toujours présent (cohérent avec la vue PATTERN).
        {
            const float gs = juce::jmin(7.0f, zoneW * 0.5f);
            const float gx = x0 + 0.5f;
            const float gy = L.plot.getBottom() - 0.5f;
            juce::Path gtri;
            gtri.addTriangle(gx, gy, gx + gs, gy, gx, gy - gs);
            g.setColour(row.subShared[static_cast<size_t>(s)] ? kGhost : kSubSolo);
            g.fillPath(gtri);
        }
    }

    // Lane vélo (bas) : histogramme de la vélocité par pas (visualisation + clic).
    {
        g.setColour(kRowLabel);
        g.setFont(juce::Font(juce::FontOptions().withHeight(9.0f)));
        g.drawText("Vélo",
                   juce::Rectangle<float>(bodyArea_.getX(), F.lane.getY(), 24.0f, F.lane.getHeight()),
                   juce::Justification::centredLeft);
        g.setColour(kScreenBorder);
        g.drawLine(L.plot.getX(), F.lane.getBottom(), L.plot.getRight(), F.lane.getBottom(), 0.6f);
        for (int s = 0; s < L.n; ++s) {
            const float x = L.plot.getX() + static_cast<float>(s) * L.cellW;
            if (s == model_.selectedStep) {
                g.setColour(kSelRowBg);
                g.fillRect(juce::Rectangle<float>(x, F.lane.getY(), L.cellW, F.lane.getHeight()));
            }
            if (row.enabled[static_cast<size_t>(s)]) {
                const float h = F.lane.getHeight()
                              * juce::jlimit(0.0f, 1.0f, static_cast<float>(row.velocity[static_cast<size_t>(s)]) / 127.0f);
                g.setColour(s == row.playhead ? kPlayhead : kCellOn);
                g.fillRoundedRectangle(juce::Rectangle<float>(x + 1.0f, F.lane.getBottom() - h,
                                                              L.cellW - 2.0f, h), 1.0f);
            }
        }
    }
}

void PatternScreen::paintHarmonyPage(juce::Graphics& g) {
    const HarmLayout L = computeHarmLayout(model_, bodyArea_);

    // En-tête : la vue hérite et affiche la tonalité + le suivi réels du pattern.
    const juce::String check(juce::CharPointer_UTF8("\xe2\x9c\x93"));  // ✓
    const juce::String dash (juce::CharPointer_UTF8("\xe2\x80\x94"));  // —
    const juce::String arrow(juce::CharPointer_UTF8(" \xe2\x96\xb8 "));// ▸
    {
        auto info = L.info;
        const float lineH = info.getHeight() / 3.0f;

        // Ligne A : tonalité effective.
        g.setColour(kHeaderText);
        g.setFont(juce::Font(juce::FontOptions().withHeight(13.0f).withStyle("Bold")));
        g.drawText("Key: " + pitchClassName(model_.harmonyRootPc) + " " + scaleNameShort(model_.harmonyScaleId)
                       + (model_.followMasterTonality ? juce::String(" (master)") : juce::String()),
                   info.removeFromTop(lineH), juce::Justification::centredLeft);

        // Ligne B : mode harmonique partagé + nombre de rows liées (mode != Chromatic).
        g.setColour(kRowLabel);
        g.setFont(juce::Font(juce::FontOptions().withHeight(12.0f)));
        juce::String modeLine;
        if (!model_.harmonyEnabled) {
            modeLine = "Harmonie: OFF (notes brutes)";
        } else {
            int linked = 0;
            const int nr = juce::jlimit(0, 16, model_.numRows);
            for (int r = 0; r < nr; ++r)
                if (model_.rows[static_cast<size_t>(r)].harmonyMode != 3)   // 3 = Chromatic
                    ++linked;
            const juce::String modeName = (model_.harmonySharedMode < 0)
                                              ? juce::String("mixte")
                                              : juce::String(harmonyModeShort(model_.harmonySharedMode));
            juce::String suffix;
            if (linked == 0)          suffix = "aucune row liee";
            else if (linked == nr)    suffix = "toutes les rows";
            else                      suffix = juce::String(linked) + "/" + juce::String(nr) + " rows liees";
            modeLine = "Harmonie: " + modeName + arrow + suffix;
        }
        g.drawText(modeLine, info.removeFromTop(lineH), juce::Justification::centredLeft);

        // Ligne C : suivi progression / tonalité.
        g.drawText("Suivi: progression " + (model_.followProgression ? check : dash)
                       + "   tonalite " + (model_.followMasterTonality ? check : dash),
                   info, juce::Justification::centredLeft);
    }

    // Bande de slots d'accords (chips) : romain+qualité en haut, nom d'accord réel en bas.
    for (int i = 0; i < L.slotsToShow; ++i) {
        juce::Rectangle<float> chip(L.slotBand.getX() + static_cast<float>(i) * L.slotW,
                                    L.slotBand.getY(), L.slotW, L.slotBand.getHeight());
        const auto inner = chip.reduced(3.0f, 2.0f);
        const bool used  = (i < model_.progLen);
        const bool cur   = (i == model_.progCurrent);
        const bool sel   = (i == model_.harmonyCursor);

        g.setColour(sel ? kSelRowBg : kCellOff);
        g.fillRoundedRectangle(inner, 3.0f);
        g.setColour(cur ? kPlayhead : kScreenBorder);
        g.drawRoundedRectangle(inner.reduced(0.8f), 3.0f, cur ? 1.5f : 0.6f);

        if (used) {
            const auto&  c         = model_.chord[static_cast<size_t>(i)];
            // Haut : chiffrage romain + suffixe usuel ; bas : nom d'accord réel (note + suffixe).
            const juce::String suffix = chordSuffix(c.quality, c.extensions);
            juce::String roman     = juce::String(romanNumeral(c.degree)) + suffix;
            juce::String chordName = pitchClassName(c.rootPc) + suffix;

            auto top   = inner;
            auto lower = top.removeFromBottom(inner.getHeight() * 0.42f);
            g.setColour(sel ? kHeaderText : kRowLabel);
            g.setFont(juce::Font(juce::FontOptions().withHeight(15.0f).withStyle("Bold")));
            g.drawText(roman, top, juce::Justification::centred);
            g.setColour(sel ? kCellOn : kRowLabel);
            g.setFont(juce::Font(juce::FontOptions().withHeight(12.0f).withStyle("Bold")));
            g.drawText(chordName, lower, juce::Justification::centred);
        } else {
            g.setColour(kCellGrid);
            g.setFont(juce::Font(juce::FontOptions().withHeight(18.0f)));
            g.drawText("+", inner, juce::Justification::centred);
        }
    }

    // Ligne de détail du slot sélectionné : inclut le nom d'accord réel.
    juce::String detail;
    if (model_.harmonyCursor < model_.progLen) {
        const auto&  c      = model_.chord[static_cast<size_t>(model_.harmonyCursor)];
        const juce::String suffix = chordSuffix(c.quality, c.extensions);
        const juce::String ext    = extensionsShort(c.extensions);   // tensions seules (détail)
        const juce::String name   = pitchClassName(c.rootPc) + suffix;
        detail = "Slot " + juce::String(model_.harmonyCursor + 1) + "/" + juce::String(model_.progLen)
               + "   " + romanNumeral(c.degree) + suffix + " " + name
               + "   ext " + (ext.isEmpty() ? juce::String("-") : ext)
               + "   bass " + juce::String(c.bassOffset)
               + "   duree " + juce::String(c.durationSlots) + " mes";
    } else {
        detail = "Slot " + juce::String(model_.harmonyCursor + 1) + " vide - edite un champ pour l'ajouter";
    }
    g.setColour(kRowLabel);
    g.setFont(juce::Font(juce::FontOptions().withHeight(12.0f)));
    g.drawText(detail, L.detail, juce::Justification::centredLeft);

    // Bande "Rows" : chaque row affiche son propre mode (A/B1/B2) ou ○ si Chromatic (délié).
    {
        const juce::String ring (juce::CharPointer_UTF8("\xe2\x97\x8b"));  // ○ creux = délié
        const int nr   = juce::jlimit(0, 16, model_.numRows);
        const int cap  = juce::jmin(nr, 12);   // abrège au-delà de ~12 pour tenir en largeur
        juce::String rowsLine = "Rows ";
        for (int r = 0; r < cap; ++r) {
            const int m = model_.rows[static_cast<size_t>(r)].harmonyMode;  // 0=A 1=B1 2=B2 3=Chromatic
            const juce::String tag = (m == 3) ? ring : juce::String(harmonyModeShort(m));
            rowsLine += "R" + juce::String(r + 1) + ":" + tag + "  ";
        }
        if (cap < nr) rowsLine += juce::String(juce::CharPointer_UTF8("\xe2\x80\xa6"));   // …
        g.setColour(kRowLabel);
        g.setFont(juce::Font(juce::FontOptions().withHeight(12.0f).withStyle("Bold")));
        g.drawText(rowsLine, L.rowsBand, juce::Justification::centredLeft);
    }

    // Hint bas (ligne fine dédiée) : raccourcis Shift de la Vue HARMONIE.
    g.setColour(kCellGrid);
    g.setFont(juce::Font(juce::FontOptions().withHeight(10.0f)));
    g.drawText(juce::String(juce::CharPointer_UTF8("\xe2\x87\xa7"))
                   + "+blanche N : cycle mode row N (CHR/A/B1/B2)   "
                   + "bouton Harm : Harmonie ON/OFF",
               L.hint, juce::Justification::centredLeft);
}

void PatternScreen::paintAutoPage(juce::Graphics& g) {
    const AutoLayout L = computeAutoLayout(model_, bodyArea_);
    const int r = (model_.numRows > 0) ? juce::jlimit(0, model_.numRows - 1, model_.selectedRow) : 0;
    const int playhead = (model_.numRows > 0) ? model_.rows[static_cast<size_t>(r)].playhead : -1;

    // Bande des 8 slots de P-lock (CC# si actif).
    for (int i = 0; i < kAutoNumSlots; ++i) {
        juce::Rectangle<float> chip(L.slotBand.getX() + static_cast<float>(i) * L.slotW,
                                    L.slotBand.getY(), L.slotW, L.slotBand.getHeight());
        const auto inner = chip.reduced(2.0f, 1.0f);
        const bool on    = (i == model_.autoSlot);
        const bool used  = (model_.autoSlotCc[i] >= 0);
        g.setColour(on ? kHeaderText : kCellOff);
        g.fillRoundedRectangle(inner, 3.0f);
        g.setColour(on ? kScreenBg : (used ? kRowLabel : kCellGrid));
        g.setFont(juce::Font(juce::FontOptions().withHeight(10.0f).withStyle(on ? "Bold" : "")));
        g.drawText(used ? ("CC" + juce::String(model_.autoSlotCc[i])) : juce::String("S" + juce::String(i + 1)),
                   inner, juce::Justification::centred);
    }

    // Sélecteur de champ (Valeur / CC#).
    for (int f = 0; f < kAutoNumFields; ++f) {
        juce::Rectangle<float> chip(L.fieldBand.getX() + static_cast<float>(f) * L.fieldW,
                                    L.fieldBand.getY(), L.fieldW, L.fieldBand.getHeight());
        const auto inner = chip.reduced(2.0f, 1.0f);
        const bool on    = (f == model_.autoField);
        g.setColour(on ? kHeaderText : kCellOff);
        g.fillRoundedRectangle(inner, 3.0f);
        g.setColour(on ? kScreenBg : kRowLabel);
        g.setFont(juce::Font(juce::FontOptions().withHeight(11.0f).withStyle(on ? "Bold" : "")));
        g.drawText(autoFieldName(f), inner, juce::Justification::centred);
    }

    // Lane : histogramme des valeurs du slot actif par pas.
    g.setColour(kScreenBorder);
    g.drawLine(L.lane.getX(), L.lane.getBottom(), L.lane.getRight(), L.lane.getBottom(), 0.6f);
    for (int s = 0; s < L.n; ++s) {
        const float x = L.lane.getX() + static_cast<float>(s) * L.cellW;
        juce::Rectangle<float> col(x, L.lane.getY(), L.cellW, L.lane.getHeight());
        if (s == model_.selectedStep) {
            g.setColour(kSelRowBg);
            g.fillRect(col);
        }
        if (s == playhead) {
            g.setColour(kPlayhead.withAlpha(0.22f));
            g.fillRect(col);
        }
        const int v = model_.autoValue[static_cast<size_t>(juce::jlimit(0, 63, s))];
        if (v >= 0) {
            const float h = L.lane.getHeight() * (static_cast<float>(v) / 127.0f);
            juce::Rectangle<float> bar(x + 1.0f, L.lane.getBottom() - h, L.cellW - 2.0f, h);
            g.setColour(kCellOn);
            g.fillRoundedRectangle(bar, 1.5f);
        }
        g.setColour(kScreenBorder);
        g.drawLine(x, L.lane.getY(), x, L.lane.getBottom(), 0.3f);
    }

    // Indicateur de fenêtre de page (16 pas que les touches éditent).
    if (model_.keyPageStart >= 0) {
        const int   start = juce::jlimit(0, L.n - 1, model_.keyPageStart);
        const int   end   = juce::jmin(L.n, start + 16);
        juce::Rectangle<float> box(L.lane.getX() + static_cast<float>(start) * L.cellW, L.lane.getY(),
                                   static_cast<float>(end - start) * L.cellW, L.lane.getHeight());
        g.setColour(kPlayhead.withAlpha(0.10f));
        g.fillRoundedRectangle(box, 2.5f);
        g.setColour(kPlayhead);
        g.drawRoundedRectangle(box.reduced(0.5f), 2.5f, 1.8f);
    }

    // Ligne de détail.
    const int sv = model_.autoValue[static_cast<size_t>(juce::jlimit(0, 63, model_.selectedStep))];
    juce::String detail = "Slot " + juce::String(model_.autoSlot + 1)
                        + "  CC" + juce::String(model_.autoCc)
                        + "   pas " + juce::String(model_.selectedStep + 1) + "/" + juce::String(L.n)
                        + "   val " + (sv >= 0 ? juce::String(sv) : juce::String("-"));
    g.setColour(kRowLabel);
    g.setFont(juce::Font(juce::FontOptions().withHeight(12.0f)));
    g.drawText(detail, L.detail, juce::Justification::centredLeft);
}

void PatternScreen::paintGlobalPage(juce::Graphics& g) {
    if (model_.numGlobalParams <= 0)
        return;

    // Liste de paramètres : nom à gauche, valeur à droite ; curseur = ligne surlignée.
    const float lineH = juce::jlimit(20.0f, 34.0f,
                                     bodyArea_.getHeight() / static_cast<float>(model_.numGlobalParams));
    for (int i = 0; i < model_.numGlobalParams; ++i) {
        const float y = bodyArea_.getY() + static_cast<float>(i) * lineH;
        juce::Rectangle<float> line(bodyArea_.getX(), y, bodyArea_.getWidth(), lineH);
        const bool sel = (i == model_.globalCursor);

        if (sel) {
            g.setColour(kSelRowBg);
            g.fillRoundedRectangle(line.reduced(1.0f), 3.0f);
        }
        g.setColour(sel ? kHeaderText : kRowLabel);
        g.setFont(juce::Font(juce::FontOptions().withHeight(14.0f)
                                 .withStyle(sel ? "Bold" : "")));
        g.drawText(model_.global[static_cast<size_t>(i)].name, line.reduced(8.0f, 0.0f),
                   juce::Justification::centredLeft);
        g.drawText(model_.global[static_cast<size_t>(i)].value, line.reduced(8.0f, 0.0f),
                   juce::Justification::centredRight);
    }
}

void PatternScreen::mouseDown(const juce::MouseEvent& e) {
    const float x = static_cast<float>(e.x);
    const float y = static_cast<float>(e.y);

    // Clic dans la barre d'onglets : bascule de page (émulation du bouton Page / accès direct).
    if (tabBarArea_.contains(x, y)) {
        const int t = tabAtX(x);
        if (t >= 0 && onTabSelected)
            onTabSelected(t);
        return;
    }

    // DRILL-IN (édition d'un subpattern) : court-circuite le hit-test normal des pages.
    // Le rendu a deux variantes : strip de sous-pas (PATTERN-sub) et sub-roll (ROLL-sub).
    if (model_.inSub) {
        if (model_.subEditIdx < 0 || model_.subEditIdx >= 16)
            return;
        const auto& sv = model_.subs[static_cast<size_t>(model_.subEditIdx)];
        const int   sn = juce::jlimit(1, 16, juce::jmax(1, sv.numSteps));

        if (model_.page == PatternScreenModel::Page::PianoRoll) {
            // SUB-ROLL : clic grille = hauteur ; clic lane (bas) = vélo. Géométrie
            // recalculée à l'identique de paintSubRoll (subLaneH / plot / cellW / laneH / topNote).
            const float subLaneH = juce::jlimit(14.0f, 26.0f, bodyArea_.getHeight() * 0.22f);
            auto        bodyTop  = bodyArea_.withTrimmedBottom(subLaneH);
            const juce::Rectangle<float> velLane(bodyArea_.getX(), bodyTop.getBottom(),
                                                 bodyArea_.getWidth(), subLaneH);
            auto plot = bodyTop.withTrimmedLeft(26.0f).reduced(2.0f);
            if (plot.getHeight() < 10.0f || plot.getWidth() < 10.0f)
                return;
            const float cellWlane = plot.getWidth() / static_cast<float>(sn);
            if (velLane.contains(x, y)) {                  // clic lane = vélo du sous-pas
                const int   k    = juce::jlimit(0, sn - 1, static_cast<int>((x - plot.getX()) / cellWlane));
                const float frac = 1.0f - (y - velLane.getY()) / juce::jmax(1.0f, velLane.getHeight());
                const int   v    = juce::jlimit(0, 127, static_cast<int>(frac * 127.0f + 0.5f));
                if (onSubLaneValue) onSubLaneValue(k, v);
                return;
            }
            if (!plot.contains(x, y))
                return;
            const float laneH   = juce::jlimit(7.0f, 16.0f, plot.getHeight() / 24.0f);
            const int   visible = juce::jmax(1, static_cast<int>(plot.getHeight() / laneH));
            auto displayPitch = [&](int k) {
                return sv.relative ? juce::jlimit(0, 127, model_.subHostNote
                                                    + (static_cast<int>(sv.note[static_cast<size_t>(k)]) - 64))
                                   : static_cast<int>(sv.note[static_cast<size_t>(k)]);
            };
            int center = sv.relative ? model_.subHostNote : 60;
            if (!sv.relative) {
                int sum = 0, cnt = 0;
                for (int k = 0; k < sn; ++k)
                    if (sv.enabled[static_cast<size_t>(k)]) { sum += displayPitch(k); ++cnt; }
                if (cnt) center = sum / cnt;
            }
            const int   low     = juce::jlimit(0, juce::jmax(0, 127 - (visible - 1)), center - visible / 2);
            const int   topNote = low + visible - 1;
            const float cellW   = plot.getWidth() / static_cast<float>(sn);
            const int   k    = juce::jlimit(0, sn - 1, static_cast<int>((x - plot.getX()) / cellW));
            const int   lane = juce::jlimit(0, visible - 1, static_cast<int>((y - plot.getY()) / laneH));
            const int   note = juce::jlimit(0, 127, topNote - lane);
            if (onSubNoteSet)
                onSubNoteSet(k, note);
        } else if (model_.page == PatternScreenModel::Page::Pattern) {
            // STRIP : clic = sélectionne + toggle le sous-pas. Géométrie identique au rendu
            // (area = bodyArea_.reduced(4) ; cw = largeur/sn).
            auto area = bodyArea_.reduced(4.0f);
            if (area.getWidth() <= 0.0f || !area.contains(x, y))
                return;
            const float cw = area.getWidth() / static_cast<float>(sn);
            const int   k  = juce::jlimit(0, sn - 1, static_cast<int>((x - area.getX()) / cw));
            if (onSubStepToggled)
                onSubStepToggled(k);
        }
        return;
    }

    // HARMONIE : clic = sélection de slot (bande de slots).
    if (model_.page == PatternScreenModel::Page::Harmony) {
        const HarmLayout L = computeHarmLayout(model_, bodyArea_);
        if (L.slotBand.contains(x, y)) {
            int i = static_cast<int>((x - L.slotBand.getX()) / L.slotW);
            i = juce::jlimit(0, L.slotsToShow - 1, i);
            if (onHarmonySlot)
                onHarmonySlot(i);
        }
        return;
    }

    // AUTO : clic = slot (haut), champ (milieu) ou valeur d'un pas (lane).
    if (model_.page == PatternScreenModel::Page::Auto) {
        const AutoLayout L = computeAutoLayout(model_, bodyArea_);
        if (L.slotBand.contains(x, y)) {
            int i = static_cast<int>((x - L.slotBand.getX()) / L.slotW);
            i = juce::jlimit(0, kAutoNumSlots - 1, i);
            if (onAutoSlot) onAutoSlot(i);
        } else if (L.fieldBand.contains(x, y)) {
            int f = static_cast<int>((x - L.fieldBand.getX()) / L.fieldW);
            f = juce::jlimit(0, kAutoNumFields - 1, f);
            if (onAutoField) onAutoField(f);
        } else if (L.lane.contains(x, y)) {
            int s = static_cast<int>((x - L.lane.getX()) / L.cellW);
            s = juce::jlimit(0, L.n - 1, s);
            const float frac = 1.0f - (y - L.lane.getY()) / juce::jmax(1.0f, L.lane.getHeight());
            const int   v    = juce::jlimit(0, 127, static_cast<int>(frac * 127.0f + 0.5f));
            if (onAutoValueSet) onAutoValueSet(s, v);
        }
        return;
    }

    // PIANO ROLL : grille de notes (haut) + lane vélo (bas).
    if (model_.page == PatternScreenModel::Page::PianoRoll) {
        const RollFrame F = computeRollFrame(bodyArea_);
        const PrLayout  L = computePrLayout(model_, F.grid);
        if (L.valid && L.plot.contains(x, y)) {              // clic grille = pose une hauteur
            int s    = juce::jlimit(0, L.n - 1, static_cast<int>((x - L.plot.getX()) / L.cellW));
            int lane = juce::jlimit(0, L.visibleLanes - 1, static_cast<int>((y - L.plot.getY()) / L.laneH));
            const int note = juce::jlimit(0, 127, L.topNote - lane);
            if (onNoteSet)
                onNoteSet(juce::jlimit(0, model_.numRows - 1, model_.selectedRow), s, note);
            return;
        }
        if (F.lane.contains(x, y) && L.valid) {              // clic lane = vélocité du pas
            const int   s    = juce::jlimit(0, L.n - 1, static_cast<int>((x - L.plot.getX()) / L.cellW));
            const float frac = 1.0f - (y - F.lane.getY()) / juce::jmax(1.0f, F.lane.getHeight());
            const int   v    = juce::jlimit(0, 127, static_cast<int>(frac * 127.0f + 0.5f));
            if (onRollLaneValue) onRollLaneValue(s, v);
        }
        return;
    }

    if (model_.page != PatternScreenModel::Page::Pattern || model_.inSub)
        return;  // autres pages / mode sub : édition par encodeurs+touches, pas à la souris (V1).

    // Clic dans le bandeau de mesures (multi-mesures) : sélection de la mesure éditée.
    {
        const auto band = measureBandArea();
        if (!band.isEmpty() && band.contains(x, y)) {
            const int b = measureAtX(x);
            if (b >= 0 && onMeasureSelected)
                onMeasureSelected(b);
            return;
        }
    }

    if (model_.numRows <= 0 || rowH_ <= 0.0f)
        return;
    const auto grid = gridArea();   // grille décalée sous le bandeau (cf. paint)
    if (!grid.contains(x, y))
        return;

    int r = firstVisibleRow_ + static_cast<int>((y - grid.getY()) / rowH_);   // offset viewport
    r = juce::jlimit(0, model_.numRows - 1, r);
    if (onRowSelected)
        onRowSelected(r);

    const float stripX = grid.getX() + gutterW_;
    const float stripW = juce::jmax(10.0f, grid.getWidth() - gutterW_ - infoW_);
    if (x < stripX || x >= stripX + stripW)
        return;  // gouttière ou zone d'infos = sélection de row uniquement.

    // Inversion de la fenêtre-mesure (zoom horizontal), même calcul qu'au rendu.
    const int   zoom    = juce::jlimit(1, 8, model_.stepZoom);
    const float winLen  = 1.0f / static_cast<float>(zoom);
    float       winStart = 0.0f;
    if (zoom > 1) {
        const int   selRow = juce::jlimit(0, model_.numRows - 1, model_.selectedRow);
        const int   selN   = juce::jlimit(1, 64, model_.rows[static_cast<size_t>(selRow)].numSteps);
        const float selPos = (static_cast<float>(model_.selectedStep) + 0.5f) / static_cast<float>(selN);
        winStart = juce::jlimit(0.0f, 1.0f - winLen, selPos - winLen * 0.5f);
    }
    const int   n      = juce::jlimit(1, 64, model_.rows[static_cast<size_t>(r)].numSteps);
    const float barPos = winStart + (x - stripX) / stripW * winLen;
    int s = juce::jlimit(0, n - 1, static_cast<int>(barPos * static_cast<float>(n)));
    if (onStepToggled)
        onStepToggled(r, s);
}

// --- HardwareButtonLook -----------------------------------------------------

void HardwareButtonLook::drawButtonBackground(juce::Graphics& g, juce::Button& b,
                                              const juce::Colour&,
                                              bool, bool isDown) {
    auto r = b.getLocalBounds().toFloat();
    const float rad = 3.0f;

    if (kind_ == Kind::WhiteKey) {
        const bool on        = b.getToggleState();
        const bool playhead  = static_cast<bool>(b.getProperties().getWithDefault("playhead", false));
        juce::Colour fill;
        juce::Colour border;
        if (playhead) {
            // Pas en cours : ambre vif. Distingue clairement « playhead » de « activé ».
            fill   = on ? juce::Colour(0xfff7a13a) : juce::Colour(0xffffc56e);
            border = juce::Colour(0xffd17a18);
        } else if (on) {
            fill   = juce::Colour(0xff7fd4a0);
            border = juce::Colour(0xff1e8a45);
        } else {
            fill   = juce::Colour(0xfff0f0ee);
            border = juce::Colour(0xff9a9a96);
        }
        g.setColour(fill);
        g.fillRoundedRectangle(r.reduced(1.0f), rad);
        g.setColour(border);
        g.drawRoundedRectangle(r.reduced(1.0f), rad, (on || playhead) ? 1.5f : 1.0f);
        if (isDown) {
            g.setColour(juce::Colours::black.withAlpha(0.12f));
            g.fillRoundedRectangle(r.reduced(1.0f), rad);
        }
    } else if (kind_ == Kind::BlackKey) {
        const bool playhead = static_cast<bool>(b.getProperties().getWithDefault("playhead", false));
        const bool led      = static_cast<bool>(b.getProperties().getWithDefault("led", false));
        // LED d'état (vert) prioritaire sur le highlight transitoire (ambre) ; sinon noir.
        if (led) {
            g.setColour(juce::Colour(0xff1e5a38));   // fond vert sombre = LED allumée
            g.fillRoundedRectangle(r.reduced(0.5f), rad);
            g.setColour(juce::Colour(0xff4fd488));   // liseré vert vif
            g.drawRoundedRectangle(r.reduced(0.5f), rad, 1.5f);
        } else {
            g.setColour(playhead ? juce::Colour(0xff7a4a10) : juce::Colour(0xff1c1c1e));
            g.fillRoundedRectangle(r.reduced(0.5f), rad);
            g.setColour(playhead ? juce::Colour(0xffd17a18) : juce::Colour(0xff4a4a50));
            g.drawRoundedRectangle(r.reduced(0.5f), rad, playhead ? 1.5f : 1.0f);
        }
        if (isDown) {
            g.setColour(juce::Colours::white.withAlpha(0.08f));
            g.fillRoundedRectangle(r.reduced(0.5f), rad);
        }
    } else {
        const bool toggled = b.getToggleState();
        if (toggled) {
            juce::ColourGradient grad(juce::Colour(0xffb33a2a), r.getX(), r.getY(),
                                        juce::Colour(0xff6e1f17), r.getX(), r.getBottom(), false);
            g.setGradientFill(grad);
        } else {
            juce::ColourGradient grad(juce::Colour(0xff3a3d42), r.getX(), r.getY(),
                                        juce::Colour(0xff1e2024), r.getX(), r.getBottom(), false);
            g.setGradientFill(grad);
        }
        g.fillRoundedRectangle(r.reduced(1.0f), 5.0f);
        g.setColour(toggled ? juce::Colour(0xffd57266) : juce::Colour(0xff606060));
        g.drawRoundedRectangle(r.reduced(1.0f), 5.0f, toggled ? 1.5f : 1.0f);
        if (isDown) {
            g.setColour(juce::Colours::black.withAlpha(0.15f));
            g.fillRoundedRectangle(r.reduced(1.0f), 5.0f);
        }
    }
}

void HardwareButtonLook::drawButtonText(juce::Graphics& g, juce::TextButton& button,
                                        bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) {
    if (kind_ != Kind::BlackKey) {
        juce::LookAndFeel_V4::drawButtonText(g, button, shouldDrawButtonAsHighlighted,
                                             shouldDrawButtonAsDown);
        return;
    }

    auto        area = button.getLocalBounds().reduced(4, 3);
    const float fh   = juce::jlimit(8.0f, 13.0f, static_cast<float>(area.getHeight()) * 0.26f);
    g.setFont(juce::Font(juce::FontOptions().withHeight(fh)));
    juce::Colour col = button.findColour(button.getToggleState() ? juce::TextButton::textColourOnId
                                                                 : juce::TextButton::textColourOffId);
    if (!button.isEnabled())
        col = col.withMultipliedAlpha(0.45f);
    g.setColour(col);
    g.drawFittedText(button.getButtonText(), area, juce::Justification::centred, 2, 0.68f);
    juce::ignoreUnused(shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);
}

// --- PianoKeysPanel ---------------------------------------------------------

PianoKeysPanel::PianoKeysPanel() {
    for (int i = 0; i < kNumWhite; ++i) {
        auto bt = std::make_unique<juce::TextButton>(juce::String(i + 1));
        whiteLooks_[static_cast<size_t>(i)].setKind(HardwareButtonLook::Kind::WhiteKey);
        bt->setLookAndFeel(&whiteLooks_[static_cast<size_t>(i)]);
        bt->setColour(juce::TextButton::textColourOffId, juce::Colour(0xff333333));
        bt->setColour(juce::TextButton::textColourOnId, juce::Colour(0xff222222));
        bt->setToggleable(true);
        bt->setClickingTogglesState(false);
        bt->addMouseListener(this, false);  // press/release dispatchés ici, callbacks décident
        addAndMakeVisible(*bt);
        whiteKeys_[static_cast<size_t>(i)] = std::move(bt);
    }
    for (int i = 0; i < kNumBlack; ++i) {
        auto bt = std::make_unique<juce::TextButton>(juce::String("F") + juce::String(i + 1));
        blackLooks_[static_cast<size_t>(i)].setKind(HardwareButtonLook::Kind::BlackKey);
        bt->setLookAndFeel(&blackLooks_[static_cast<size_t>(i)]);
        bt->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffcccccc));
        bt->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        addAndMakeVisible(*bt);
        blackKeys_[static_cast<size_t>(i)] = std::move(bt);
    }
}

void PianoKeysPanel::setWhiteKeyClick(int stepIndex, std::function<void()> fn) {
    if (stepIndex < 0 || stepIndex >= kNumWhite)
        return;
    whiteKeys_[static_cast<size_t>(stepIndex)]->onClick = std::move(fn);
}

void PianoKeysPanel::setWhiteKeyDown(int stepIndex, std::function<void()> fn) {
    if (stepIndex < 0 || stepIndex >= kNumWhite)
        return;
    whiteDown_[static_cast<size_t>(stepIndex)] = std::move(fn);
}

void PianoKeysPanel::setWhiteKeyUp(int stepIndex, std::function<void()> fn) {
    if (stepIndex < 0 || stepIndex >= kNumWhite)
        return;
    whiteUp_[static_cast<size_t>(stepIndex)] = std::move(fn);
}

void PianoKeysPanel::setWhiteKeyLabel(int stepIndex, const juce::String& name) {
    if (stepIndex < 0 || stepIndex >= kNumWhite)
        return;
    whiteKeys_[static_cast<size_t>(stepIndex)]->setButtonText(
        name.isNotEmpty() ? name : juce::String(stepIndex + 1));
}

int PianoKeysPanel::whiteKeyIndexFromComponent(const juce::Component* c) const {
    if (c == nullptr) return -1;
    for (int i = 0; i < kNumWhite; ++i)
        if (whiteKeys_[static_cast<size_t>(i)].get() == c)
            return i;
    return -1;
}

void PianoKeysPanel::mouseDown(const juce::MouseEvent& e) {
    const int idx = whiteKeyIndexFromComponent(e.eventComponent);
    if (idx < 0) return;
    if (auto& fn = whiteDown_[static_cast<size_t>(idx)])
        fn();
}

void PianoKeysPanel::mouseUp(const juce::MouseEvent& e) {
    const int idx = whiteKeyIndexFromComponent(e.eventComponent);
    if (idx < 0) return;
    if (auto& fn = whiteUp_[static_cast<size_t>(idx)])
        fn();
}

void PianoKeysPanel::setBlackKeyClick(int funcIndex, std::function<void()> fn) {
    if (funcIndex < 0 || funcIndex >= kNumBlack)
        return;
    blackKeys_[static_cast<size_t>(funcIndex)]->onClick = std::move(fn);
}

void PianoKeysPanel::setBlackKeyHighlight(int blackIdx) {
    if (blackIdx == blackHighlight_)
        return;
    auto apply = [this](int idx, bool on) {
        if (idx < 0 || idx >= kNumBlack) return;
        auto& bt = *blackKeys_[static_cast<size_t>(idx)];
        if (on) bt.getProperties().set("playhead", true);
        else    bt.getProperties().remove("playhead");
        bt.repaint();
    };
    apply(blackHighlight_, false);
    apply(blackIdx, true);
    blackHighlight_ = blackIdx;
}

void PianoKeysPanel::setBlackKeyLed(int blackIdx, bool on) {
    // LED d'état : une seule noire allumée à la fois (la noire 9 rel/abs ici).
    const int target = on ? blackIdx : -1;
    if (target == blackLed_)
        return;
    auto apply = [this](int idx, bool lit) {
        if (idx < 0 || idx >= kNumBlack) return;
        auto& bt = *blackKeys_[static_cast<size_t>(idx)];
        if (lit) bt.getProperties().set("led", true);
        else     bt.getProperties().remove("led");
        bt.repaint();
    };
    apply(blackLed_, false);
    apply(target, true);
    blackLed_ = target;
}

void PianoKeysPanel::setPlayheadStep(int step) {
    if (step == playheadStep_)
        return;
    auto clearKey = [this](int idx) {
        if (idx < 0 || idx >= kNumWhite) return;
        auto& bt = *whiteKeys_[static_cast<size_t>(idx)];
        bt.getProperties().remove("playhead");
        bt.repaint();
    };
    auto setKey = [this](int idx) {
        if (idx < 0 || idx >= kNumWhite) return;
        auto& bt = *whiteKeys_[static_cast<size_t>(idx)];
        bt.getProperties().set("playhead", true);
        bt.repaint();
    };
    clearKey(playheadStep_);
    setKey(step);
    playheadStep_ = step;
}

void PianoKeysPanel::setBlackKeyLabel(int funcIndex, const juce::String& name) {
    if (funcIndex < 0 || funcIndex >= kNumBlack)
        return;
    blackKeys_[static_cast<size_t>(funcIndex)]->setButtonText(name);
}

void PianoKeysPanel::resized() {
    auto        area    = getLocalBounds().toFloat();
    const float gap     = 6.0f;
    const float rowGap  = 6.0f;
    const float left0   = area.getX() + gap;
    const float usableW = area.getWidth() - gap * 2.0f;

    const float maxByW = usableW / static_cast<float>(kNumWhite);
    const float maxByH =
        (area.getHeight() - gap * 2.0f - rowGap) / (1.0f + kBlackKeyHeightFactor);
    const float whiteCellW = juce::jmin(maxByW, maxByH);
    const float whiteH     = whiteCellW;
    const float blackW     = whiteCellW * kBlackKeyWidthFactor;
    const float blackH     = whiteCellW * kBlackKeyHeightFactor;

    const float whiteY = area.getBottom() - gap - whiteH;
    const float blackY = whiteY - rowGap - blackH;

    // Layout 1 — rangée des pas (cellule fixe, pad plus étroit → vide entre deux blanches).
    for (int i = 0; i < kNumWhite; ++i) {
        const int x = juce::roundToInt(left0 + static_cast<float>(i) * whiteCellW);
        const int w = juce::roundToInt(whiteCellW - kWhiteKeyTrim);
        const int h = juce::roundToInt(whiteH);
        whiteKeys_[static_cast<size_t>(i)]->setBounds(x, juce::roundToInt(whiteY), w, h);
    }

    // Layout 2 — dièses : centre X = milieu du **vide** entre le bord droit de [left] et le bord gauche de
    // [left+1] (bounds réels après arrondi, pas la formule grille seule).
    for (int i = 0; i < kNumBlack; ++i) {
        const int lw = kBlackLeftWhiteIndex[static_cast<size_t>(i)];
        const int r0 = whiteKeys_[static_cast<size_t>(lw)]->getBounds().getRight();
        const int l1 = whiteKeys_[static_cast<size_t>(lw + 1)]->getBounds().getX();
        const float cx = 0.5f * (static_cast<float>(r0) + static_cast<float>(l1));
        const float x  = cx - blackW * 0.5f;
        blackKeys_[static_cast<size_t>(i)]->setBounds(juce::roundToInt(x), juce::roundToInt(blackY),
                                                        juce::roundToInt(blackW), juce::roundToInt(blackH));
        blackKeys_[static_cast<size_t>(i)]->toFront(false);
    }
}
