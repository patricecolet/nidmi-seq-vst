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

/// Ces couleurs sont posées sur le PANNEAU du plugin, pas sur l'écran simulé. Les
/// tons sombres de la simulation — pensés pour un fond quasi noir — y disparaissent :
/// c'est le gotcha déjà payé une fois (« ne pas encoder une information vers le bas
/// depuis un fond quasi noir »). Ici on distingue par la TEINTE, à luminance tenue.
juce::Colour roleColour(encoders::Role r) {
    using R = encoders::Role;
    switch (r) {
        case R::Pushed: return juce::Colour(0xffffffff);   // la molette poussée
        case R::Lent:   return juce::Colour(0xfff0a94e);   // prêtée au contexte
        case R::Free:   return juce::Colour(0xff8b97a2);   // rien à éditer ici
        case R::Absent: return juce::Colour(0xff8b97a2);   // sans objet sur cet objet
        case R::Own:    break;
    }
    return juce::Colour(0xffcfd8e0);
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

// UNE SEULE SOURCE DE VÉRITÉ pour la sélection. L'éditeur la tient déjà —
// `selectedRow_`, `selectedStep_`, `inSub_`, `subStep_` — et tout l'écran la lit.
// `encState_` n'est donc PAS un second état : c'est un véhicule, rempli avant chaque
// appel au modèle et relu après. Deux curseurs parallèles, c'est le curseur qui bouge
// d'un côté et la vue qui reste de l'autre.
void NidmiSeqAudioProcessorEditor::syncStateFromEditor() {
    const auto& pat = proc_.engine().pattern();
    encState_.row     = static_cast<uint8_t>(juce::jlimit(0, static_cast<int>(pat.numRows) - 1,
                                                          selectedRow_));
    encState_.step    = static_cast<uint8_t>(juce::jmax(0, selectedStep_));
    encState_.bar     = static_cast<uint8_t>(juce::jmax(0, editBar_));
    encState_.inSub   = inSub_;
    encState_.subStep = static_cast<uint8_t>(juce::jmax(0, subStep_));
    encState_.voices  = static_cast<uint8_t>(voicesOfCurrentRow());
}

void NidmiSeqAudioProcessorEditor::syncEditorFromState() {
    if (selectedRow_ != encState_.row) {
        selectedRow_ = encState_.row;
        inSub_       = false;              // changer de piste sort du sous-pattern
    }
    selectedStep_ = encState_.step;
    subStep_      = encState_.subStep;
    if (encState_.inSub && !inSub_) {      // on vient d'entrer : mémoriser l'hôte
        subHostRow_  = encState_.row;
        subHostStep_ = encState_.step;
    }
    inSub_ = encState_.inSub;
    buildScreenModel();                    // l'écran suit le curseur, toujours
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
        gLab_[i].setFont(juce::FontOptions(10.0f));
        gLab_[i].setMinimumHorizontalScale(0.6f);   // « Mode harmo. » doit tenir

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

    syncStateFromEditor();
    sendAction(encoders::turn(proc_.engine(), encState_,
                              static_cast<encoders::Slot>(slot), delta));
    syncEditorFromState();
    refreshGrammarEncoders();
}

void NidmiSeqAudioProcessorEditor::onGrammarPush(int slot) {
    syncStateFromEditor();
    sendAction(encoders::push(proc_.engine(), encState_,
                              static_cast<encoders::Slot>(slot)));
    syncEditorFromState();
    refreshGrammarEncoders();
}

void NidmiSeqAudioProcessorEditor::refreshGrammarEncoders() {
    const uint8_t ctx = encState_.ctx;   // le contexte ouvert n'appartient qu'à la grammaire
    syncStateFromEditor();
    encState_.ctx = ctx;

    encoders::View v[encoders::kCount];
    encoders::describe(proc_.engine(), encState_, v);

    for (int i = 0; i < encoders::kCount; ++i) {
        // Deux lignes : l'attribut, puis sa valeur. Sur le boîtier les EC11 n'ont
        // aucun afficheur — c'est l'écran qui le dira, en face de chaque molette.
        // fromUTF8 obligatoire : le modèle produit de l'UTF-8 (« Gamme mère », « ¼ »),
        // et juce::String(const char*) le lirait en Latin-1 — d'où les « Ã¨ » à l'écran.
        gLab_[i].setText(juce::String::fromUTF8(v[i].name) + "\n"
                             + juce::String::fromUTF8(v[i].value),
                         juce::dontSendNotification);
        gLab_[i].setColour(juce::Label::textColourId, roleColour(v[i].role));
        gEnc_[i].setTooltip(juce::String::fromUTF8(v[i].src));   // le champ édité

        // La marque permanente de la règle 11 : on ne pousse jamais pour voir.
        const bool depth = v[i].hasDepth;
        const bool open  = (encState_.ctx == static_cast<uint8_t>(i));
        gPush_[i].setButtonText(open ? juce::String("sortir")
                                     : juce::String(depth ? "+" : "-"));
        gPush_[i].setToggleState(open, juce::dontSendNotification);
        gPush_[i].setEnabled(depth || open);
    }
}

void NidmiSeqAudioProcessorEditor::layoutGrammarEncoders(juce::Rectangle<int> left,
                                                         juce::Rectangle<int> right) {
    // Trois à gauche — Row, Pas, Valeur — trois à droite — Vélo, Durée, Master.
    auto place = [](juce::Rectangle<int> col, juce::Label& lab, juce::Slider& knob,
                    juce::TextButton& push) {
        lab.setBounds(col.removeFromTop(30));   // deux lignes de 10 px + interligne
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
