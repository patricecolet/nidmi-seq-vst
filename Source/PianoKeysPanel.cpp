#include "HardwareStyleComponents.h"
#include "HardwareStyleInternal.h"

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
