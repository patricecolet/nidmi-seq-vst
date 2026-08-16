#include <nidmi_seq/EncoderModel.h>
#include <nidmi_seq/SequencerEngine.h>

#include "DeviceProfile.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"

// LA GRAMMAIRE À SIX MOLETTES, côté plugin.
//
// Tout ce qui décide — ce que porte chaque molette, ce que fait son appui, quelles
// commandes en sortent — vit dans `encoders::` au cœur du moteur, sans JUCE. Ce
// fichier ne fait que trois choses : lire l'état, dessiner ce que le modèle décrit,
// et poster les commandes qu'il produit.
//
// C'est délibéré. Un affichage qui recalculerait de son côté ce que la molette édite
// finirait par mentir sur ce que le moteur fait — et le firmware, qui partage ce
// modèle, montrera exactement la même chose sous les mêmes doigts.

namespace {

/// Les encodeurs sont INFINIS : un EC11 n'a pas de butée. On travaille donc en
/// relatif — la plage est large, on lit le déplacement, et on recale au centre.
constexpr double kEncCenter = 5000.0;
constexpr double kEncSpan   = 10000.0;

juce::Colour roleColour(encoders::Role r) {
    using R = encoders::Role;
    switch (r) {
        case R::Pushed: return juce::Colour(0xffd8f3e2);   // la molette poussée
        case R::Lent:   return juce::Colour(0xffe0a044);   // prêtée au contexte
        case R::Free:   return juce::Colour(0xff6b3c38);   // rien à éditer ici
        case R::Absent: return juce::Colour(0xff5a3330);   // sans objet sur cet objet
        case R::Own:    break;
    }
    return juce::Colour(0xff8fd6a6);
}

}  // namespace

int NidmiSeqAudioProcessorEditor::voicesOfCurrentRow() const {
    // La polyphonie n'est pas dans le moteur : c'est le profil d'appareil qui la
    // porte. À une voix, densité et ancrage n'apparaissent pas — pas de refus, le
    // contexte n'existe simplement pas.
    // Un profil par projet aujourd'hui ; quand une piste pourra viser son propre
    // appareil (§5.11), c'est ici que le choix se fera, par row.
    const int idx = proc_.deviceProfileIndex();
    if (idx <= 0 || idx >= DeviceProfile::count()) return 1;
    return DeviceProfile::byIndex(idx).voices();
}

void NidmiSeqAudioProcessorEditor::sendAction(const encoders::Action& a) {
    for (uint8_t i = 0; i < a.count; ++i) proc_.pushCommand(a.cmd[i]);
}

void NidmiSeqAudioProcessorEditor::setupGrammarEncoders() {
    for (int i = 0; i < encoders::kCount; ++i) {
        auto& k = gEnc_[i];
        k.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        k.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        k.setRange(0.0, kEncSpan, 1.0);
        k.setMouseDragSensitivity(900);
        k.setValue(kEncCenter, juce::dontSendNotification);
        gLast_[i] = kEncCenter;
        k.onValueChange = [this, i] { onGrammarTurn(i); };

        gLab_[i].setJustificationType(juce::Justification::centred);
        gLab_[i].setFont(juce::FontOptions(11.0f));

        gPush_[i].setLookAndFeel(&transportLook_);
        gPush_[i].setToggleable(true);
        gPush_[i].setClickingTogglesState(false);
        gPush_[i].onClick = [this, i] { onGrammarPush(i); };

        addAndMakeVisible(gEnc_[i]);
        addAndMakeVisible(gLab_[i]);
        addAndMakeVisible(gPush_[i]);
    }
}

void NidmiSeqAudioProcessorEditor::onGrammarTurn(int slot) {
    auto& k = gEnc_[slot];
    const double now   = k.getValue();
    const int    delta = static_cast<int>(now - gLast_[slot]);
    gLast_[slot] = now;
    if (delta == 0) return;

    // On recale au centre pour ne jamais buter : l'encodeur physique n'a pas de fin.
    if (now < kEncCenter * 0.2 || now > kEncCenter * 1.8) {
        k.setValue(kEncCenter, juce::dontSendNotification);
        gLast_[slot] = kEncCenter;
    }

    encState_.voices = static_cast<uint8_t>(voicesOfCurrentRow());
    sendAction(encoders::turn(proc_.engine(), encState_,
                              static_cast<encoders::Slot>(slot), delta));
    refreshGrammarEncoders();
}

void NidmiSeqAudioProcessorEditor::onGrammarPush(int slot) {
    encState_.voices = static_cast<uint8_t>(voicesOfCurrentRow());
    sendAction(encoders::push(proc_.engine(), encState_,
                              static_cast<encoders::Slot>(slot)));
    refreshGrammarEncoders();
}

void NidmiSeqAudioProcessorEditor::refreshGrammarEncoders() {
    encState_.voices = static_cast<uint8_t>(voicesOfCurrentRow());

    encoders::View v[encoders::kCount];
    encoders::describe(proc_.engine(), encState_, v);

    for (int i = 0; i < encoders::kCount; ++i) {
        // Deux lignes : l'attribut, puis sa valeur. Sur le boîtier les EC11 n'ont
        // aucun afficheur — c'est l'écran qui le dira, en face de chaque molette.
        gLab_[i].setText(juce::String(v[i].name) + "\n" + juce::String(v[i].value),
                         juce::dontSendNotification);
        gLab_[i].setColour(juce::Label::textColourId, roleColour(v[i].role));
        gEnc_[i].setTooltip(juce::String(v[i].src));   // le champ édité, pour l'audit

        // La marque permanente de la règle 11 : on ne pousse jamais pour voir.
        const bool depth = v[i].hasDepth;
        const bool open  = (encState_.ctx == static_cast<uint8_t>(i));
        gPush_[i].setButtonText(open ? "sortir" : (depth ? "\xe2\x97\x8f" : "\xe2\x80\x94"));
        gPush_[i].setToggleState(open, juce::dontSendNotification);
        gPush_[i].setEnabled(depth || open);
    }
}

void NidmiSeqAudioProcessorEditor::layoutGrammarEncoders(juce::Rectangle<int> left,
                                                         juce::Rectangle<int> right) {
    // Trois à gauche — Row, Pas, Valeur — trois à droite — Vélo, Durée, Master.
    auto place = [](juce::Rectangle<int> col, juce::Label& lab, juce::Slider& knob,
                    juce::TextButton& push) {
        lab.setBounds(col.removeFromTop(26));
        push.setBounds(col.removeFromBottom(20).reduced(6, 2));
        auto k = col.reduced(2, 1);
        const int d = juce::jmin(k.getWidth(), k.getHeight());
        knob.setBounds(k.withSizeKeepingCentre(d, d));
    };
    const int hL = left.getHeight() / 3, hR = right.getHeight() / 3;
    for (int i = 0; i < 3; ++i)
        place(left.removeFromTop(i == 2 ? left.getHeight() : hL), gLab_[i], gEnc_[i], gPush_[i]);
    for (int i = 0; i < 3; ++i)
        place(right.removeFromTop(i == 2 ? right.getHeight() : hR),
              gLab_[3 + i], gEnc_[3 + i], gPush_[3 + i]);
}
