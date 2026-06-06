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
