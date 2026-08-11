#include "HardwareStyleComponents.h"
#include "HardwareStyleInternal.h"

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

    paintMeasureBand(g);

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
                // (Plus de marqueur ghost/partagé : les copies sont pleines → tout sub est
                //  indépendant ; le contenu du sub est déjà rendu par les mini-blocs.)
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
        info += "  c" + juce::String(row.channel);
        // Une row CC ne suit pas l'harmonie : on montre sa destination plutot
        // que son mode harmonique, qui n'a aucun effet.
        info += " " + (row.isCC ? (row.ccLabel.isNotEmpty() ? row.ccLabel
                                                            : ("CC" + juce::String(row.ccNumber)))
                                : juce::String(harmonyModeShort(row.harmonyMode)));
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
