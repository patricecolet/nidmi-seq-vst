#include "HardwareStyleComponents.h"
#include "HardwareStyleInternal.h"

void PatternScreen::paintHarmonyPage(juce::Graphics& g) {
    // Bandeau de mesures en haut (multi-mesures) : indicateur + chips cliquables.
    // Le layout HARMONIE démarre SOUS le bandeau (gridArea = bodyArea moins le bandeau).
    paintMeasureBand(g);
    const HarmLayout L = computeHarmLayout(model_, gridArea());

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
        juce::Rectangle<float> chip(L.slots.x[i], L.slotBand.getY(),
                                    L.slots.w[i], L.slotBand.getHeight());
        const auto inner = chip.reduced(3.0f, 2.0f);
        const bool used  = (i < model_.progLen);
        const bool cur   = (i == model_.progCurrent);
        const bool sel   = (i == model_.harmonyCursor);

        g.setColour(sel ? kSelRowBg : kCellOff);
        g.fillRoundedRectangle(inner, 3.0f);
        g.setColour(cur ? kPlayhead : kScreenBorder);
        g.drawRoundedRectangle(inner.reduced(0.8f), 3.0f, cur ? 1.5f : 0.6f);
        // Focus ACCORDS : liseré blanc sur la chip sélectionnée (la lane éditée).
        if (sel && model_.harmonyFocus == 0) {
            g.setColour(kSelStep);
            g.drawRoundedRectangle(inner.reduced(0.4f), 3.0f, 1.6f);
        }

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

    // Bande TONALITÉ (marqueurs root+gamme). Préfixe "Ton" à gauche si la place existe.
    {
        const bool focusKey = (model_.harmonyFocus == 1);
        for (int i = 0; i < L.keysToShow; ++i) {
            juce::Rectangle<float> chip(L.keys.x[i], L.keyBand.getY(),
                                        L.keys.w[i], L.keyBand.getHeight());
            const auto inner = chip.reduced(3.0f, 1.5f);
            const bool used  = (i < model_.keyLen);
            const bool cur   = (i == model_.keyCurrent);
            const bool sel   = (i == model_.keyCursor);
            g.setColour(sel ? kSelRowBg : kCellOff);
            g.fillRoundedRectangle(inner, 3.0f);
            g.setColour(cur ? kPlayhead : kScreenBorder);
            g.drawRoundedRectangle(inner.reduced(0.8f), 3.0f, cur ? 1.5f : 0.6f);
            if (sel && focusKey) {   // focus TONALITÉ : liseré blanc sur le marqueur édité
                g.setColour(kSelStep);
                g.drawRoundedRectangle(inner.reduced(0.4f), 3.0f, 1.6f);
            }
            if (used) {
                const auto& k = model_.keyLane[static_cast<size_t>(i)];
                const juce::String name = pitchClassName(k.rootPc) + " " + scaleNameShort(k.scaleId);
                g.setColour(sel ? kHeaderText : kRowLabel);
                g.setFont(juce::Font(juce::FontOptions().withHeight(11.0f).withStyle("Bold")));
                g.drawText(name, inner, juce::Justification::centred);
            } else {
                g.setColour(kCellGrid);
                g.setFont(juce::Font(juce::FontOptions().withHeight(14.0f)));
                g.drawText("+", inner, juce::Justification::centred);
            }
        }
    }

    // Ligne de détail : selon le focus, marqueur de tonalité OU slot d'accord.
    juce::String detail;
    if (model_.harmonyFocus == 1) {
        if (model_.keyCursor < model_.keyLen) {
            const auto& k = model_.keyLane[static_cast<size_t>(model_.keyCursor)];
            detail = "Tonalite " + juce::String(model_.keyCursor + 1) + "/" + juce::String(model_.keyLen)
                   + "   " + pitchClassName(k.rootPc) + " " + scaleNameShort(k.scaleId)
                   + "   duree " + juce::String(k.durationBeats) + " tps";
        } else {
            detail = "Tonalite " + juce::String(model_.keyCursor + 1)
                   + " vide - tourne Enc4 (Tonique/Gamme) pour l'ajouter";
        }
    } else if (model_.harmonyCursor < model_.progLen) {
        const auto&  c      = model_.chord[static_cast<size_t>(model_.harmonyCursor)];
        const juce::String suffix = chordSuffix(c.quality, c.extensions);
        const juce::String ext    = extensionsShort(c.extensions);   // tensions seules (détail)
        const juce::String name   = pitchClassName(c.rootPc) + suffix;
        detail = "Slot " + juce::String(model_.harmonyCursor + 1) + "/" + juce::String(model_.progLen)
               + "   " + romanNumeral(c.degree) + suffix + " " + name
               + "   ext " + (ext.isEmpty() ? juce::String("-") : ext)
               + "   bass " + juce::String(c.bassOffset)
               + "   duree " + juce::String(c.durationBeats) + " tps";
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
        // Le geste ⇧+blanche N regle le mode de la row POUR LA MESURE EDITEE, pas
        // pour la row entiere. La ligne disait « Rows R1:A R2:A… » sans jamais le
        // dire : on changeait de mesure, les lettres changeaient, et rien a l'ecran
        // n'expliquait pourquoi. Le numero de mesure porte cette portee.
        juce::String rowsLine = (model_.numBars > 1)
            ? ("Mes " + juce::String(model_.editBar + 1) + " " + juce::String(juce::CharPointer_UTF8("\xc2\xb7")) + " rows ")
            : juce::String("Rows ");
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
    g.drawText((model_.harmonyFocus == 1
                    ? juce::String("TONALITE  Enc2=Tonique Enc3=Duree Enc4=Gamme  push=Suppr   ")
                    : juce::String(juce::CharPointer_UTF8("\xe2\x87\xa7"))
                          + "+blanche N : cycle mode row   ")
                   + "re-appui HARMONIE : accords <-> tonalite",
               L.hint, juce::Justification::centredLeft);
}
