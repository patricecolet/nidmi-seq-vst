#include "HardwareStyleComponents.h"
#include "HardwareStyleInternal.h"

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
