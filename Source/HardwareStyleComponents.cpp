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

juce::String extensionsShort(int bits) {
    struct { int bit; const char* name; } kE[] = {
        {1 << 0, "9"}, {1 << 1, "11"}, {1 << 2, "13"},
        {1 << 3, "b9"}, {1 << 4, "#9"}, {1 << 5, "#11"}, {1 << 6, "b13"}};
    juce::StringArray parts;
    for (auto& e : kE)
        if (bits & e.bit) parts.add(e.name);
    return parts.isEmpty() ? juce::String() : ("+" + parts.joinIntoString(","));
}

struct HarmLayout {
    juce::Rectangle<float> slotBand, detail;
    float slotW = 10.0f;
    int   slotsToShow = 1;
};

HarmLayout computeHarmLayout(const PatternScreenModel& m, juce::Rectangle<float> body) {
    HarmLayout L;
    auto b      = body;
    L.slotBand  = b.removeFromTop(juce::jmin(90.0f, b.getHeight() * 0.6f));
    b.removeFromTop(8.0f);
    L.detail    = b.removeFromTop(22.0f);
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
    static const char* kQ[12] = {"", "m", "dim", "aug", "7", "maj7",
                                 "m7", "mM7", "m7b5", "dim7", "sus2", "sus4"};
    return kQ[juce::jlimit(0, 11, quality)];
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
    constexpr float kMinRowH = 22.0f;
    const int nr = juce::jmax(1, model_.numRows);
    const int autoVis = juce::jlimit(1, nr, static_cast<int>(bodyArea_.getHeight() / kMinRowH));
    visibleRows_ = (model_.rowZoom > 0) ? juce::jlimit(1, nr, model_.rowZoom) : autoVis;
    rowH_        = bodyArea_.getHeight() / static_cast<float>(visibleRows_);
    const int sel = juce::jlimit(0, nr - 1, model_.selectedRow);
    firstVisibleRow_ = juce::jlimit(0, juce::jmax(0, nr - visibleRows_), sel - visibleRows_ / 2);
}

int PatternScreen::tabAtX(float x) const {
    if (tabBarArea_.getWidth() <= 0.0f)
        return -1;
    const float tabW = tabBarArea_.getWidth() / static_cast<float>(PatternScreenModel::kNumPages);
    const int   i    = static_cast<int>((x - tabBarArea_.getX()) / tabW);
    return juce::jlimit(0, PatternScreenModel::kNumPages - 1, i);
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
            const bool rel = (model_.subEditIdx >= 0 && model_.subEditIdx < 16)
                                 && model_.subs[static_cast<size_t>(model_.subEditIdx)].relative;
            crumb = "R" + juce::String(model_.subHostRow + 1) + arrow
                  + "P" + juce::String(model_.subHostStep + 1) + arrow
                  + "SUB " + (rel ? "rel" : "abs");
        } else if (model_.page == PatternScreenModel::Page::Pattern
            || model_.page == PatternScreenModel::Page::PianoRoll
            || model_.page == PatternScreenModel::Page::Auto) {
            crumb = "R" + juce::String(model_.selectedRow + 1) + "/" + juce::String(juce::jmax(1, model_.numRows));
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
        g.drawText(crumb, header.removeFromLeft(120.0f), juce::Justification::centredLeft);
        juce::String right = glyph + "  " + juce::String(model_.bpm, 1) + " BPM   "
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
                g.setColour(kHeaderText);
                g.drawRoundedRectangle(inner.reduced(0.4f), 3.0f, 1.8f);
            }
        }
        return;
    }

    const float stripX = bodyArea_.getX() + gutterW_;
    const float stripW = juce::jmax(10.0f, bodyArea_.getWidth() - gutterW_ - infoW_);

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
    const juce::Rectangle<int> stripClip(juce::roundToInt(stripX), juce::roundToInt(bodyArea_.getY()),
                                         juce::roundToInt(stripW), juce::roundToInt(bodyArea_.getHeight()));

    const int rEnd = juce::jmin(model_.numRows, firstVisibleRow_ + visibleRows_);
    for (int r = firstVisibleRow_; r < rEnd; ++r) {
        const auto& row = model_.rows[static_cast<size_t>(r)];
        const float y   = bodyArea_.getY() + static_cast<float>(r - firstVisibleRow_) * rowH_;
        juce::Rectangle<float> rowRect(bodyArea_.getX(), y, bodyArea_.getWidth(), rowH_);

        if (r == model_.selectedRow) {
            g.setColour(kSelRowBg);
            g.fillRoundedRectangle(rowRect.reduced(1.0f), 3.0f);
        }

        // Libellé row (gauche).
        g.setColour(r == model_.selectedRow ? kHeaderText : kRowLabel);
        g.setFont(juce::Font(juce::FontOptions().withHeight(12.0f)
                                 .withStyle(r == model_.selectedRow ? "Bold" : "")));
        g.drawText("R" + juce::String(r + 1),
                   juce::Rectangle<float>(bodyArea_.getX(), y, gutterW_, rowH_).reduced(2.0f),
                   juce::Justification::centredLeft);

        // Strip de pas : N cellules réparties sur la fenêtre-mesure (zoom horizontal), clippées au strip.
        const int   n        = juce::jlimit(1, 64, row.numSteps);
        const float cellWz   = (1.0f / static_cast<float>(n)) / winLen * stripW;
        const float cellPad  = cellWz > 10.0f ? 1.5f : 0.5f;
        const bool  showNote = cellWz >= 22.0f;
        {
            juce::Graphics::ScopedSaveState clip(g);
            g.reduceClipRegion(stripClip);
            for (int s = 0; s < n; ++s) {
                const float cx = barToX(static_cast<float>(s) / static_cast<float>(n));
                if (cx + cellWz <= stripX || cx >= stripX + stripW)
                    continue;  // hors fenêtre
                juce::Rectangle<float> cell(cx, y + 2.0f, cellWz, rowH_ - 4.0f);
                const auto inner = cell.reduced(cellPad, cellPad);
                const bool on    = row.enabled[static_cast<size_t>(s)];
                const bool ph    = (s == row.playhead);

                // Luminosité de la cellule active ∝ vélocité (feedback d'un coup d'œil).
                const float velA = 0.45f + 0.55f * (static_cast<float>(row.velocity[static_cast<size_t>(s)]) / 127.0f);
                g.setColour(on ? (row.muted ? kCellOn.withAlpha(0.30f) : kCellOn.withAlpha(velA)) : kCellOff);
                g.fillRoundedRectangle(inner, 2.0f);
                if (ph) {
                    g.setColour(kPlayhead);
                    g.drawRoundedRectangle(inner.reduced(0.5f), 2.0f, 1.5f);
                } else {
                    g.setColour(kCellGrid);
                    g.drawRoundedRectangle(inner.reduced(0.5f), 2.0f, 0.6f);
                }
                // Subpattern niché : la cellule affiche les sous-pas du sub (tuplet imbriqué visible).
                const int subI = row.subIdx[static_cast<size_t>(s)];
                if (subI >= 0 && subI < 16 && model_.subs[static_cast<size_t>(subI)].numSteps > 0) {
                    const auto& sv  = model_.subs[static_cast<size_t>(subI)];
                    const int   sn  = juce::jlimit(1, 16, sv.numSteps);
                    const float mw  = inner.getWidth() / static_cast<float>(sn);
                    for (int k = 0; k < sn; ++k) {
                        juce::Rectangle<float> mc(inner.getX() + static_cast<float>(k) * mw,
                                                  inner.getY(), mw, inner.getHeight());
                        g.setColour(sv.enabled[static_cast<size_t>(k)] ? kScreenBg : kCellOn.withAlpha(0.25f));
                        g.fillRect(mc.reduced(0.6f, 1.0f));
                    }
                    g.setColour(kPlayhead);   // liseré ambre = « ce pas a un sub »
                    g.drawRoundedRectangle(inner.reduced(0.4f), 2.0f, 1.2f);
                } else if (showNote && on) {
                    g.setColour(kScreenBg);
                    g.setFont(juce::Font(juce::FontOptions().withHeight(juce::jmin(11.0f, rowH_ - 6.0f))));
                    g.drawText(midiNoteShort(row.note[static_cast<size_t>(s)]), inner,
                               juce::Justification::centred);
                }
                // Pas sélectionné (curseur Enc2) sur la row sélectionnée : liseré clair.
                if (r == model_.selectedRow && s == model_.selectedStep) {
                    g.setColour(kHeaderText);
                    g.drawRoundedRectangle(inner.reduced(0.3f), 2.0f, 1.6f);
                }
            }
        }

        // Infos row (droite) : N, canal, mode harmonique, mute.
        juce::String info = "N" + juce::String(n) + " c" + juce::String(row.channel) + " "
                            + harmonyModeShort(row.harmonyMode);
        g.setColour(row.muted ? kMutedText : kRowLabel);
        g.setFont(juce::Font(juce::FontOptions().withHeight(11.0f)));
        g.drawText(row.muted ? (info + " M") : info,
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
        const float y     = bodyArea_.getY() + static_cast<float>(model_.selectedRow - firstVisibleRow_) * rowH_;
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
                   juce::Rectangle<float>(bodyArea_.getRight() - 12.0f, bodyArea_.getY(), 12.0f, 10.0f),
                   juce::Justification::centred);
    if (firstVisibleRow_ + visibleRows_ < model_.numRows)
        g.drawText(juce::CharPointer_UTF8("\xe2\x96\xbc"),  // ▼
                   juce::Rectangle<float>(bodyArea_.getRight() - 12.0f, bodyArea_.getBottom() - 10.0f, 12.0f, 10.0f),
                   juce::Justification::centred);
}

void PatternScreen::paintSubRoll(juce::Graphics& g) {
    if (model_.subEditIdx < 0 || model_.subEditIdx >= 16) {
        paintStubPage(g, "SUB", "—");
        return;
    }
    const auto& sv = model_.subs[static_cast<size_t>(model_.subEditIdx)];
    const int   sn = juce::jlimit(1, 16, juce::jmax(1, sv.numSteps));
    auto        plot = bodyArea_.withTrimmedLeft(26.0f).reduced(2.0f);
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
            g.setColour(kSelRowBg);
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

    // Colonnes (cases du tuplet) : curseur de pas + playhead + séparateurs.
    for (int s = 0; s < L.n; ++s) {
        const float x = L.plot.getX() + static_cast<float>(s) * L.cellW;
        if (s == model_.selectedStep) {
            g.setColour(kSelRowBg);
            g.fillRect(juce::Rectangle<float>(x, L.plot.getY(), L.cellW, L.plot.getHeight()));
        }
        if (s == row.playhead) {
            g.setColour(kPlayhead.withAlpha(0.22f));
            g.fillRect(juce::Rectangle<float>(x, L.plot.getY(), L.cellW, L.plot.getHeight()));
        }
        g.setColour(kScreenBorder);
        g.drawLine(x, L.plot.getY(), x, L.plot.getBottom(), 0.4f);
    }

    // Notes posées : un bloc par pas actif dont la hauteur tombe dans la fenêtre.
    for (int s = 0; s < L.n; ++s) {
        if (!row.enabled[static_cast<size_t>(s)])
            continue;
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

    // Bande de slots d'accords : chiffrage romain + qualité, extensions/bass dessous.
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
            const auto&  c     = model_.chord[static_cast<size_t>(i)];
            juce::String label = juce::String(romanNumeral(c.degree)) + chordQualityShort(c.quality);
            auto         top   = inner;
            auto         lower = top.removeFromBottom(inner.getHeight() * 0.38f);
            g.setColour(sel ? kHeaderText : kRowLabel);
            g.setFont(juce::Font(juce::FontOptions().withHeight(16.0f).withStyle("Bold")));
            g.drawText(label, top, juce::Justification::centred);
            juce::String sub = extensionsShort(c.extensions);
            if (c.bassOffset != 0)
                sub += (sub.isEmpty() ? juce::String() : juce::String(" ")) + "/" + juce::String(c.bassOffset);
            g.setColour(kRowLabel);
            g.setFont(juce::Font(juce::FontOptions().withHeight(10.0f)));
            g.drawText(sub, lower, juce::Justification::centred);
        } else {
            g.setColour(kCellGrid);
            g.setFont(juce::Font(juce::FontOptions().withHeight(18.0f)));
            g.drawText("+", inner, juce::Justification::centred);
        }
    }

    // Ligne de détail du slot sélectionné.
    juce::String detail;
    if (model_.harmonyCursor < model_.progLen) {
        const auto&  c  = model_.chord[static_cast<size_t>(model_.harmonyCursor)];
        const juce::String ext = extensionsShort(c.extensions);
        detail = "Slot " + juce::String(model_.harmonyCursor + 1) + "/" + juce::String(model_.progLen)
               + "   " + romanNumeral(c.degree) + " " + chordQualityShort(c.quality)
               + "   ext " + (ext.isEmpty() ? juce::String("-") : ext)
               + "   bass " + juce::String(c.bassOffset) + "   duree " + juce::String(c.durationSlots);
    } else {
        detail = "Slot " + juce::String(model_.harmonyCursor + 1) + " vide - edite un champ pour l'ajouter";
    }
    g.setColour(kRowLabel);
    g.setFont(juce::Font(juce::FontOptions().withHeight(12.0f)));
    g.drawText(detail, L.detail, juce::Justification::centredLeft);
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
    if (model_.numRows <= 0 || rowH_ <= 0.0f)
        return;
    if (!bodyArea_.contains(x, y))
        return;

    int r = firstVisibleRow_ + static_cast<int>((y - bodyArea_.getY()) / rowH_);   // offset viewport
    r = juce::jlimit(0, model_.numRows - 1, r);
    if (onRowSelected)
        onRowSelected(r);

    const float stripX = bodyArea_.getX() + gutterW_;
    const float stripW = juce::jmax(10.0f, bodyArea_.getWidth() - gutterW_ - infoW_);
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
        g.setColour(playhead ? juce::Colour(0xff7a4a10) : juce::Colour(0xff1c1c1e));
        g.fillRoundedRectangle(r.reduced(0.5f), rad);
        g.setColour(playhead ? juce::Colour(0xffd17a18) : juce::Colour(0xff4a4a50));
        g.drawRoundedRectangle(r.reduced(0.5f), rad, playhead ? 1.5f : 1.0f);
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
