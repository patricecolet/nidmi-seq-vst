#pragma once

#include <JuceHeader.h>

// Profil d'appareil : donne un nom aux numéros de CC.
//
// Le séquenceur émet des CC bruts ; un profil sert uniquement à l'affichage,
// pour lire « VCF Cutoff » plutôt que « CC 74 » sur les rows CC, les P-locks
// et les destinations de macro. Aucun effet sur le MIDI émis.
//
// Le profil actif est une propriété du processeur, sérialisée avec le projet.

struct DeviceParam {
    int         cc;
    const char* shortName;   // ≤ 7 caractères : les pastilles de la page AUTO
                             // font ~28 px de large en police 10
    const char* name;
    const char* group;       // VCF, VCA, VCO, LFO, Mod
    bool        wired;       // le firmware sort-il réellement ce paramètre ?
};

class DeviceProfile {
public:
    DeviceProfile(juce::String name, const DeviceParam* params, int count)
        : name_(std::move(name)), params_(params), count_(count) {}

    const juce::String& name() const noexcept { return name_; }
    bool  isEmpty()            const noexcept { return count_ == 0; }

    // nullptr si ce CC n'est pas décrit par le profil.
    const DeviceParam* find(int cc) const noexcept;

    // « VCF Cutoff » si connu, « CC 74 » sinon.
    juce::String label(int cc) const;

    // « Cutoff » si connu, « 74 » sinon. Pour les zones étroites.
    juce::String shortLabel(int cc) const;

    // Registre. L'index 0 est toujours le profil vide (« Aucun ») : le
    // comportement historique, numéros de CC bruts.
    static int                  count() noexcept;
    static const DeviceProfile& byIndex(int index) noexcept;
    static int                  indexOfName(const juce::String& name) noexcept;

private:
    juce::String       name_;
    const DeviceParam* params_;
    int                count_;
};
