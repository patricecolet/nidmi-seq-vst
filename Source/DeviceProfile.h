#pragma once

#include <JuceHeader.h>
#include <vector>

// Profil d'appareil : donne un nom aux numéros de CC.
//
// Les profils sont des FICHIERS JSON lus au démarrage, pas une table
// compilée : ajouter un synthé n'exige aucune recompilation. Le même format
// servira à l'éditeur de panneau, qui lira les mêmes fichiers.
//
// Emplacement : ~/Documents/NiDMI/Profiles/*.json
// Le dossier est créé au premier lancement, avec le profil Kobol dedans en
// guise d'exemple.
//
// Le profil ne modifie AUCUN message MIDI : il ne sert qu'à l'affichage.

struct DeviceParam {
    int          cc      = 0;
    juce::String shortName;   // ≤ 7 caractères : zones étroites (pastilles 10 px)
    juce::String name;
    juce::String group;       // VCF, VCA, VCO, LFO, Mod…
    bool         wired = true;  // a un effet audible aujourd'hui
};

class DeviceProfile {
public:
    DeviceProfile() = default;
    DeviceProfile(juce::String name, std::vector<DeviceParam> params)
        : name_(std::move(name)), params_(std::move(params)) {}

    const juce::String& name() const noexcept { return name_; }
    bool  isEmpty()            const noexcept { return params_.empty(); }

    const DeviceParam* find(int cc) const noexcept;

    juce::String label(int cc) const;        // « VCF Cutoff », sinon « CC 74 »
    juce::String shortLabel(int cc) const;   // « Cutoff »,     sinon « 74 »

    // Registre. L'index 0 est toujours « Aucun » : numéros de CC bruts.
    static int                  count() noexcept;
    static const DeviceProfile& byIndex(int index) noexcept;
    static int                  indexOfName(const juce::String& name) noexcept;

    // Relit le dossier. Appelé une fois au premier accès ; rappeler pour
    // prendre en compte un fichier ajouté sans redémarrer.
    static void reload();

    static juce::File profilesDirectory();

    // Ce que la dernière lecture a trouvé ou rejeté, pour l'affichage.
    static juce::String lastScanSummary();

private:
    juce::String             name_ { "Aucun" };
    std::vector<DeviceParam> params_;
};
