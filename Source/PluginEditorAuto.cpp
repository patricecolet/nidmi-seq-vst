#include "PluginEditor.h"
#include "PluginProcessor.h"

#include "EditorHelpers.h"
#include "MidiExporter.h"

#include <nidmi_seq/ScaleBank.h>
#include <nidmi_seq/HarmonyEngine.h>

#include <cmath>

int NidmiSeqAudioProcessorEditor::numOledParams() {
    return kNumOledParams;
}

const char* NidmiSeqAudioProcessorEditor::oledParamId(int index) {
    return kOledParamIds[juce::jlimit(0, kNumOledParams - 1, index)];
}

const char* NidmiSeqAudioProcessorEditor::oledParamTitle(int index) {
    return kOledTitles[juce::jlimit(0, kNumOledParams - 1, index)];
}

int NidmiSeqAudioProcessorEditor::stepPageCount() const {
    const auto& pat = proc_.engine().pattern();
    if (pat.numRows == 0)
        return 1;
    const int sr = juce::jlimit(0, static_cast<int>(pat.numRows) - 1, selectedRow_);
    const int n  = juce::jmax(1, static_cast<int>(pat.rows[static_cast<size_t>(sr)].numSteps));
    return juce::jlimit(1, 4, (n + 15) / 16);   // pages de 16 pas (P1..P4)
}

void NidmiSeqAudioProcessorEditor::setStepField(int step, int field, int value) {
    const auto& pat = proc_.engine().pattern();
    if (pat.numRows == 0)
        return;
    const int sr = juce::jlimit(0, static_cast<int>(pat.numRows) - 1, selectedRow_);
    const int n  = juce::jlimit(1, 64, static_cast<int>(pat.rows[static_cast<size_t>(sr)].numSteps));
    if (step < 0 || step >= n)
        return;
    const auto& sd = pat.rows[static_cast<size_t>(sr)].step(static_cast<uint8_t>(editBar_), static_cast<uint8_t>(step));
    int note = sd.note, vel = sd.velocity, gate = sd.gate;
    switch (field) {
        case 0:  note = juce::jlimit(0, 127, value); break;   // Note
        case 1:  vel  = juce::jlimit(0, 127, value); break;   // Vélo
        default: gate = juce::jlimit(1, 100, value); break;   // Gate (%)
    }
    SequencerCommand c;
    c.id = SequencerCommandId::SetStep;
    c.a  = static_cast<uint8_t>(sr);
    c.b  = static_cast<uint8_t>(step);
    c.c  = static_cast<uint8_t>(note);
    c.d  = static_cast<uint8_t>(vel);
    c.e  = static_cast<uint8_t>(gate);
    c.f  = static_cast<uint8_t>(editBar_);
    proc_.controller().postCommand(c);
    buildScreenModel();
}

int NidmiSeqAudioProcessorEditor::effectiveAutoCc() const {
    const auto& pat = proc_.engine().pattern();
    if (pat.numRows == 0)
        return autoCcDefault_;
    const int   ar   = juce::jlimit(0, static_cast<int>(pat.numRows) - 1, selectedRow_);
    const auto& arow = pat.rows[static_cast<size_t>(ar)];
    const int   an   = juce::jlimit(1, 64, static_cast<int>(arow.numSteps));
    const int   slot = juce::jlimit(0, 7, autoSlot_);
    for (int s = 0; s < an; ++s) {
        const auto& lk = arow.step(static_cast<uint8_t>(editBar_), static_cast<uint8_t>(s)).ccLocks[static_cast<size_t>(slot)];
        if (lk.ccNumber != 0xFF)
            return lk.ccNumber;
    }
    return autoCcDefault_;
}

void NidmiSeqAudioProcessorEditor::postAutoValueAt(int step, int value) {
    const auto& pat = proc_.engine().pattern();
    if (pat.numRows == 0)
        return;
    const int ar = juce::jlimit(0, static_cast<int>(pat.numRows) - 1, selectedRow_);
    const int an = juce::jlimit(1, 64, static_cast<int>(pat.rows[static_cast<size_t>(ar)].numSteps));
    if (step < 0 || step >= an)
        return;
    SequencerCommand c;
    c.id = SequencerCommandId::SetStepCCLock;
    c.a  = static_cast<uint8_t>(ar);
    c.b  = static_cast<uint8_t>(step);
    c.c  = static_cast<uint8_t>(juce::jlimit(0, 7, autoSlot_));
    c.d  = static_cast<uint8_t>(juce::jlimit(0, 127, effectiveAutoCc()));
    c.e  = static_cast<uint8_t>(juce::jlimit(0, 127, value));
    c.f  = static_cast<uint8_t>(editBar_);
    proc_.controller().postCommand(c);
    buildScreenModel();
}

void NidmiSeqAudioProcessorEditor::postAutoCcNumber(int ccNumber) {
    autoCcDefault_ = juce::jlimit(0, 127, ccNumber);
    const auto& pat = proc_.engine().pattern();
    if (pat.numRows > 0) {
        // Réémet les P-locks existants du slot actif avec le nouveau numéro de CC.
        const int   ar   = juce::jlimit(0, static_cast<int>(pat.numRows) - 1, selectedRow_);
        const auto& arow = pat.rows[static_cast<size_t>(ar)];
        const int   an   = juce::jlimit(1, 64, static_cast<int>(arow.numSteps));
        const int   slot = juce::jlimit(0, 7, autoSlot_);
        for (int s = 0; s < an; ++s) {
            const auto& lk = arow.step(static_cast<uint8_t>(editBar_), static_cast<uint8_t>(s)).ccLocks[static_cast<size_t>(slot)];
            if (lk.ccNumber == 0xFF)
                continue;
            SequencerCommand c;
            c.id = SequencerCommandId::SetStepCCLock;
            c.a  = static_cast<uint8_t>(ar);
            c.b  = static_cast<uint8_t>(s);
            c.c  = static_cast<uint8_t>(slot);
            c.d  = static_cast<uint8_t>(autoCcDefault_);
            c.e  = lk.value;
            c.f  = static_cast<uint8_t>(editBar_);
            proc_.controller().postCommand(c);
        }
    }
    applyEncoderConfigForState();
    buildScreenModel();
}

void NidmiSeqAudioProcessorEditor::launchExportFlow() {
    // 1. Choix du mode via popup, 2. file chooser, 3. export + retour utilisateur.
    juce::PopupMenu menu;
    menu.addItem(1, "Bake (NoteOn/CC absolus)");
    menu.addItem(2, "Full (Bake + blob NiDMI)");

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&exportBtn_),
                       [this](int choice) {
        if (choice == 0)
            return;
        const auto mode = (choice == 2) ? MidiExporter::Mode::Full : MidiExporter::Mode::Bake;

        const auto defaultDir =
            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
        const juce::String suggested =
            (mode == MidiExporter::Mode::Full ? "NidmiSeq_full" : "NidmiSeq_bake")
            + juce::String(".mid");

        fileChooser_ = std::make_unique<juce::FileChooser>(
            "Exporter MIDI", defaultDir.getChildFile(suggested), "*.mid");

        const int flags =
            juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting;

        fileChooser_->launchAsync(flags, [this, mode](const juce::FileChooser& fc) {
            const auto dest = fc.getResult();
            if (dest == juce::File{})
                return;
            const auto withExt =
                dest.hasFileExtension(".mid") ? dest : dest.withFileExtension(".mid");
            const auto result = proc_.exportMidi(withExt, mode);

            juce::MessageBoxOptions opts =
                juce::MessageBoxOptions()
                    .withIconType(result.success ? juce::MessageBoxIconType::InfoIcon
                                                  : juce::MessageBoxIconType::WarningIcon)
                    .withTitle(result.success ? "Export OK" : juce::String(juce::CharPointer_UTF8("Export rat\xc3\xa9")))
                    .withMessage(result.message)
                    .withButton("OK");
            juce::AlertWindow::showAsync(opts, nullptr);
        });
    });
}
