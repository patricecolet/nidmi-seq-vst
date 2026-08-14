#include "HardwareStyleComponents.h"
#include "HardwareStyleInternal.h"
#include "EditorHelpers.h"   // ccInterpName : la LANE nomme son mode de lissage

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

void PatternScreen::paintAutoPage(juce::Graphics& g) {
    const AutoLayout L = computeAutoLayout(model_, bodyArea_);
    const int r = (model_.numRows > 0) ? juce::jlimit(0, model_.numRows - 1, model_.selectedRow) : 0;
    const int playhead = (model_.numRows > 0) ? model_.rows[static_cast<size_t>(r)].playhead : -1;

    // Cellule LANE, en TETE : la row elle-meme comme automation. Une row kind==CC
    // n'apparaissait nulle part sur cette page — il fallait aller sur GLOB pour
    // seulement savoir qu'elle en etait une.
    {
        const auto inner = L.slotCell(0).reduced(2.0f, 1.0f);
        const bool on    = (model_.autoSlot == PatternScreenModel::kAutoLaneSlot);
        g.setColour(on ? kHeaderText : kCellOff);
        g.fillRoundedRectangle(inner, 3.0f);
        // Contour ambre quand la lane enregistre : le geste part la, pas ailleurs.
        if (model_.laneIsCC && model_.recArmed) {
            g.setColour(kPlayhead);
            g.drawRoundedRectangle(inner.reduced(0.5f), 3.0f, 1.6f);
        }
        g.setColour(on ? kScreenBg : (model_.laneIsCC ? kRowLabel : kCellGrid));
        g.setFont(juce::Font(juce::FontOptions().withHeight(10.0f).withStyle(on ? "Bold" : "")));
        juce::String t = model_.laneIsCC
                           ? (model_.laneLabel.isNotEmpty() ? model_.laneLabel
                                                            : ("CC" + juce::String(model_.laneCc)))
                           : juce::String("Lane");
        g.drawText(t, inner, juce::Justification::centred);
    }

    // Bande des 8 slots de P-lock (CC# si actif), decalee d'une cellule.
    for (int i = 0; i < kAutoNumSlots; ++i) {
        const auto chip  = L.slotCell(i + 1);
        const auto inner = chip.reduced(2.0f, 1.0f);
        const bool on    = (i == model_.autoSlot);
        const bool used  = (model_.autoSlotCc[i] >= 0);
        g.setColour(on ? kHeaderText : kCellOff);
        g.fillRoundedRectangle(inner, 3.0f);
        g.setColour(on ? kScreenBg : (used ? kRowLabel : kCellGrid));
        g.setFont(juce::Font(juce::FontOptions().withHeight(10.0f).withStyle(on ? "Bold" : "")));
        // Nom court du profil s'il en donne un, sinon « CC74 ».
        juce::String slotText = juce::String("S") + juce::String(i + 1);
        if (used) {
            slotText = model_.ccSlotLabel[i].isNotEmpty()
                         ? model_.ccSlotLabel[i]
                         : ("CC" + juce::String(model_.autoSlotCc[i]));
        }
        g.drawText(slotText, inner, juce::Justification::centred);
    }

    // Sélecteur de champ (Valeur / CC# / Interp sur la LANE).
    for (int f = 0; f < L.fieldsToShow; ++f) {
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

    // COURBE d'interpolation — uniquement sur la LANE, et seulement si elle lisse.
    //
    // C'etait le trou : le mode se reglait sans qu'on voie jamais son effet. La
    // courbe tracee est celle que le moteur emet vraiment (smoothstep pour Smooth,
    // droite pour Linear). En mode Step il n'y a rien a montrer — les barres SONT
    // deja la reponse, ajouter des paliers ne dirait rien de plus.
    if (model_.autoSlot == PatternScreenModel::kAutoLaneSlot && model_.laneIsCC && model_.laneInterp != 0) {
        juce::Path path;
        bool  started = false;
        int   prev    = -1;
        auto  yOf = [&](int v) { return L.lane.getBottom() - L.lane.getHeight() * (v / 127.0f); };
        for (int s = 0; s < L.n; ++s) {
            const int v = model_.autoValue[static_cast<size_t>(juce::jlimit(0, 63, s))];
            if (v < 0) continue;
            const float xc = L.lane.getX() + (static_cast<float>(s) + 0.5f) * L.cellW;
            const float yc = yOf(v);
            if (!started) { path.startNewSubPath(xc, yc); started = true; prev = s; continue; }
            const float x0 = L.lane.getX() + (static_cast<float>(prev) + 0.5f) * L.cellW;
            const float y0 = yOf(model_.autoValue[static_cast<size_t>(juce::jlimit(0, 63, prev))]);
            if (model_.laneInterp == 1) {
                path.lineTo(xc, yc);
            } else {
                constexpr int kSeg = 12;
                for (int i = 1; i <= kSeg; ++i) {
                    const float t = static_cast<float>(i) / static_cast<float>(kSeg);
                    const float e = t * t * (3.0f - 2.0f * t);   // smoothstep, comme le moteur
                    path.lineTo(x0 + (xc - x0) * t, y0 + (yc - y0) * e);
                }
            }
            prev = s;
        }
        if (started) {
            g.setColour(kPlayhead.withAlpha(0.9f));
            g.strokePath(path, juce::PathStrokeType(1.8f));
        }

        // Segment de BOUCLAGE : le moteur cherche le pas suivant en (step + k) % N,
        // donc il interpole du dernier pas actif vers le premier, par-dessus la
        // barre de mesure. S'arreter au dernier pas laissait croire a un palier
        // final qui n'existe pas. On dessine ce segment deux fois — sa sortie a
        // droite et son entree a gauche — en clippant sur la lane.
        int firstIdx = -1, lastIdx = -1;
        for (int s = 0; s < L.n; ++s)
            if (model_.autoValue[static_cast<size_t>(juce::jlimit(0, 63, s))] >= 0) {
                if (firstIdx < 0) firstIdx = s;
                lastIdx = s;
            }
        if (firstIdx >= 0 && lastIdx > firstIdx) {
            const float y0 = yOf(model_.autoValue[static_cast<size_t>(juce::jlimit(0, 63, lastIdx))]);
            const float y1 = yOf(model_.autoValue[static_cast<size_t>(juce::jlimit(0, 63, firstIdx))]);
            const float xa = L.lane.getX() + (static_cast<float>(lastIdx) + 0.5f) * L.cellW;
            const float xb = L.lane.getX() + (static_cast<float>(firstIdx + L.n) + 0.5f) * L.cellW;
            juce::Path wrap;
            wrap.startNewSubPath(xa, y0);
            constexpr int kSeg = 16;
            for (int i = 1; i <= kSeg; ++i) {
                const float t = static_cast<float>(i) / static_cast<float>(kSeg);
                const float e = (model_.laneInterp == 1) ? t : t * t * (3.0f - 2.0f * t);
                wrap.lineTo(xa + (xb - xa) * t, y0 + (y1 - y0) * e);
            }
            juce::Graphics::ScopedSaveState clip(g);
            g.reduceClipRegion(L.lane.toNearestInt());
            g.setColour(kPlayhead.withAlpha(0.45f));   // plus discret : il enjambe la mesure
            g.strokePath(wrap, juce::PathStrokeType(1.4f));
            wrap.applyTransform(juce::AffineTransform::translation(
                -static_cast<float>(L.n) * L.cellW, 0.0f));   // son entree, au tour suivant
            g.strokePath(wrap, juce::PathStrokeType(1.4f));
        }
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
    const bool onLane = (model_.autoSlot == PatternScreenModel::kAutoLaneSlot);
    juce::String detail = (onLane ? juce::String("Lane")
                                  : ("Slot " + juce::String(model_.autoSlot + 1)))
                        + (onLane && !model_.laneIsCC
                             ? juce::String("  row en Note - push Enc1 pour en faire une lane")
                             : ("  CC" + juce::String(model_.autoCc)
                                + (onLane ? ("  " + juce::String(ccInterpName(model_.laneInterp)))
                                          : juce::String())))
                        + "   pas " + juce::String(model_.selectedStep + 1) + "/" + juce::String(L.n)
                        + "   val " + (sv >= 0 ? juce::String(sv) : juce::String("-"));
    g.setColour(kRowLabel);
    g.setFont(juce::Font(juce::FontOptions().withHeight(12.0f)));
    g.drawText(detail, L.detail, juce::Justification::centredLeft);
}

// Libellé d'un slot de chaîne (notation « à l'italienne » du ChainVM).
static juce::String songOpLabel(int op, int p1, int p2) {
    switch (op) {
        case 0:  return "Play  P" + juce::String(p1 + 1) + (p2 > 1 ? "  x" + juce::String(p2) : juce::String());
        case 1:  return "Repeat  x" + juce::String(juce::jmax(1, p1));
        case 2:  return "End Repeat";
        case 3:  return "Segno";
        case 4:  return "D.S. (al Segno)";
        case 5:  return "D.S. al Coda";
        case 6:  return "Coda";
        case 7:  return "To Coda";
        case 8:  return "D.C. (al Capo)";
        case 9:  return "D.C. al Coda";
        case 10: return "Fine";
        case 11: return "End";
        default: return "?";
    }
}

void PatternScreen::paintSongPage(juce::Graphics& g) {
    auto area = bodyArea_;

    // Ligne d'état : mode chaîne/boucle + pattern actif de la banque.
    auto status = area.removeFromTop(20.0f);
    g.setColour(model_.songMode ? kCellOn : kRowLabel);
    g.setFont(juce::Font(juce::FontOptions().withHeight(13.0f).withStyle("Bold")));
    g.drawText(model_.songMode ? juce::String(juce::CharPointer_UTF8("\xe2\x96\xb6 CHA\xc3\x8eNE"))
                               : juce::String(juce::CharPointer_UTF8("\xe2\x97\x8b BOUCLE")),
               status.reduced(8.0f, 0.0f), juce::Justification::centredLeft);
    g.setColour(kRowLabel);
    g.setFont(juce::Font(juce::FontOptions().withHeight(12.0f)));
    g.drawText("Actif: P" + juce::String(model_.songActive),
               status.reduced(8.0f, 0.0f), juce::Justification::centredRight);

    // Liste des slots + ligne « + » d'ajout, avec défilement pour suivre le curseur.
    const int total   = juce::jmax(1, model_.songLen + 1);
    const float lineH = 20.0f;
    const int   visN  = juce::jmax(1, static_cast<int>(area.getHeight() / lineH));
    int win = 0;
    if (total > visN) {
        win = juce::jlimit(0, total - visN, model_.songCursor - visN / 2);
    }

    for (int vis = 0; vis < visN; ++vis) {
        const int i = win + vis;
        if (i >= total) break;
        juce::Rectangle<float> line(area.getX(), area.getY() + static_cast<float>(vis) * lineH,
                                    area.getWidth(), lineH);
        const bool sel    = (i == model_.songCursor);
        const bool addRow = (i >= model_.songLen);

        if (sel) {
            g.setColour(kSelRowBg);
            g.fillRoundedRectangle(line.reduced(2.0f, 1.0f), 3.0f);
        }
        // Numéro de slot.
        g.setColour(sel ? kHeaderText : kCellGrid);
        g.setFont(juce::Font(juce::FontOptions().withHeight(11.0f)));
        g.drawText(addRow ? juce::String("  +") : juce::String(i + 1).paddedLeft('0', 2),
                   line.reduced(8.0f, 0.0f).withWidth(26.0f), juce::Justification::centredLeft);
        // Contenu.
        g.setColour(sel ? kHeaderText : kRowLabel);
        g.setFont(juce::Font(juce::FontOptions().withHeight(13.0f).withStyle(sel ? "Bold" : "")));
        juce::Rectangle<float> txt = line.reduced(8.0f, 0.0f).withTrimmedLeft(30.0f);
        if (addRow) {
            g.setColour(kCellGrid);
            g.drawText("ajouter un slot", txt, juce::Justification::centredLeft);
        } else {
            const auto& s = model_.songSlots[static_cast<size_t>(juce::jlimit(0, 63, i))];
            g.drawText(songOpLabel(s.op, s.p1, s.p2), txt, juce::Justification::centredLeft);
        }
    }

    if (model_.songLen == 0) {
        g.setColour(kCellGrid);
        g.setFont(juce::Font(juce::FontOptions().withHeight(11.0f)));
        g.drawText("Enc1=type  Enc3=P#  Enc4=x  |  noires: +ajout  -suppr  Song",
                   area.removeFromBottom(16.0f).reduced(8.0f, 0.0f), juce::Justification::centredLeft);
    }
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
