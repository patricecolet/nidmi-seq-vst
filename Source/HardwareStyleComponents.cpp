#include "HardwareStyleComponents.h"
#include "HardwareStyleInternal.h"

// --- PatternScreen ----------------------------------------------------------

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

// Bandeau de mesures : en PATTERN et HARMONIE, hors-sub, et si numBars>1. Hauteur fixe ~15px,
// rogné en haut de bodyArea_. Sinon rect vide (les autres vues ignorent ce bandeau).
juce::Rectangle<float> PatternScreen::measureBandArea() const {
    const bool pageOk = (model_.page == PatternScreenModel::Page::Pattern
                         || model_.page == PatternScreenModel::Page::Harmony
                         || model_.page == PatternScreenModel::Page::PianoRoll);
    if (!pageOk || model_.inSub || model_.numBars <= 1)
        return {};
    constexpr float kBandH = 15.0f;
    return bodyArea_.withHeight(juce::jmin(kBandH, bodyArea_.getHeight() * 0.4f));
}

// Bandeau de mesures (multi-mesures) : chips cliquables au-dessus du contenu de la vue.
// Mesure éditée = surlignée ; mesure jouée = liseré ambre (playhead). Partagé PATTERN/HARMONIE.
void PatternScreen::paintMeasureBand(juce::Graphics& g) {
    const auto band = measureBandArea();
    if (band.isEmpty())
        return;
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
        } else if (model_.page == PatternScreenModel::Page::Harmony) {
            // Indicateur de mesure éditée (le mode harmonique est par row+mesure).
            crumb = "Mes " + juce::String(model_.editBar + 1) + "/" + juce::String(juce::jmax(1, model_.numBars));
            if (model_.playing && model_.numBars > 1 && model_.playBar != model_.editBar) {
                const juce::String play(juce::CharPointer_UTF8("\xe2\x96\xb8"));   // ▸
                crumb += play + "joue" + juce::String(model_.playBar + 1);
            }
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
        // Clic bandeau de mesures (au-dessus de la grille) : sélection de la mesure éditée.
        {
            const auto band = measureBandArea();
            if (!band.isEmpty() && band.contains(x, y)) {
                const int b = measureAtX(x);
                if (b >= 0 && onMeasureSelected)
                    onMeasureSelected(b);
                return;
            }
        }
        const RollFrame F = computeRollFrame(gridArea());
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

    // Clic dans le bandeau de mesures (PATTERN et HARMONIE, hors-sub) : sélection de la mesure éditée.
    if (!model_.inSub) {
        const auto band = measureBandArea();
        if (!band.isEmpty() && band.contains(x, y)) {
            const int b = measureAtX(x);
            if (b >= 0 && onMeasureSelected)
                onMeasureSelected(b);
            return;
        }
    }

    if (model_.page != PatternScreenModel::Page::Pattern || model_.inSub)
        return;  // édition grille/rows à la souris = PATTERN seulement (V1).

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
