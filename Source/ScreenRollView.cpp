#include "HardwareStyleComponents.h"
#include "HardwareStyleInternal.h"

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
    // Sous-pas RECOUVERTS par la longueur (span) d'un sous-pas antérieur : masqués (même
    // règle que le moteur / le ROLL principal). Le pas sélectionné reste éditable (colonne).
    bool subCovered[16] = {};
    for (int k = 0; k < sn; ++k)
        for (int o = k - 1; o >= 0; --o) {
            if (!sv.enabled[static_cast<size_t>(o)]) continue;
            const int osp = juce::jlimit(1, sn - o, static_cast<int>(sv.span[static_cast<size_t>(o)]));
            if (o + osp > k) { subCovered[k] = true; break; }
        }

    // Colonnes (sous-pas) + curseur + blocs de notes (largeur ∝ longueur = span × gate).
    for (int k = 0; k < sn; ++k) {
        const float x = plot.getX() + static_cast<float>(k) * cellW;
        if (k == model_.subStep) {
            g.setColour(kSelStep.withAlpha(0.16f));   // colonne du sous-pas sélectionné : voile blanc
            g.fillRect(juce::Rectangle<float>(x, plot.getY(), cellW, plot.getHeight()));
        }
        g.setColour(kScreenBorder);
        g.drawLine(x, plot.getY(), x, plot.getBottom(), 0.4f);
        if (sv.enabled[static_cast<size_t>(k)] && !subCovered[k]) {
            const int lane = topNote - displayPitch(k);
            if (lane >= 0 && lane < visible) {
                // Longueur jouée : span sous-pas × gate% → largeur du bloc (min 1 cellule visible).
                const int   span     = juce::jlimit(1, sn - k, static_cast<int>(sv.span[static_cast<size_t>(k)]));
                const float gateFrac = juce::jlimit(0.1f, 1.0f, static_cast<float>(sv.gate[static_cast<size_t>(k)]) / 100.0f);
                const float w        = juce::jmax(2.0f, cellW * static_cast<float>(span) * gateFrac);
                juce::Rectangle<float> blk(x, plot.getY() + static_cast<float>(lane) * laneH, w, laneH);
                g.setColour(kCellOn);
                g.fillRoundedRectangle(blk.reduced(1.0f, 1.0f), 2.0f);
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
    paintMeasureBand(g);                              // bandeau de mesures (multi-mesures), cliquable
    const RollFrame F = computeRollFrame(gridArea()); // le roll démarre sous le bandeau
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
        // (Plus de marqueur ghost/partagé : copies pleines → subs indépendants.)
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
