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

    // ---- format v2 : panneau ----
    juce::String section;     // section du panneau = une page sur le materiel
    juce::String type { "knob" };   // knob | switch | selector
    bool  hasPos = false;     // false = pas de controle physique (modulation)
    float x = 0.0f, y = 0.0f; // centre, normalise 0..1 sur le panneau
    float size = 0.045f;      // diametre, fraction de la LARGEUR du panneau

    // CC entrant remappe vers ce parametre. -1 = aucun.
    // Seul moyen de « MIDI learn » sur un synthe sans sortie MIDI : on ne peut
    // pas decouvrir sa carte, seulement lier un potard de notre controleur.
    int learn = -1;

    // Valeur appliquee par « Reset valeurs ». Meme table que le cc_default du
    // firmware au demarrage : le synthe retrouve son etat de mise sous tension.
    int defaultValue = 0;

    // Parametre -64..0..+63 remappe en 0..127 en ajoutant 64. L'editeur doit
    // l'afficher signe : sans cela « 64 » s'affiche pour un reglage centre.
    // Cas frequent chez Waldorf, marque d'un asterisque dans leurs manuels.
    bool bipolar = false;
};

// Ce que l'appareil sait dire de son propre etat. Determine entierement la
// strategie de synchronisation quand l'utilisateur charge un preset SUR la
// machine : les potards de l'editeur ne correspondent alors plus a rien.
struct DeviceSync {
    // L'appareil emet-il un CC quand on bouge un controle de facade ?
    // Si oui, l'editeur reste synchrone APRES un premier accord, et le vrai
    // MIDI learn est possible.
    bool ccOnEdit = false;

    // "none"    : rien a lire, l'etat de l'editeur EST la verite (Kobol)
    // "manual"  : l'appareil sait dumper, mais seulement depuis SON menu —
    //             l'editeur ne peut pas l'interroger, il doit demander a
    //             l'utilisateur de declencher l'envoi (Waldorf M)
    // "request" : l'editeur demande et recoit, synchronisation invisible
    juce::String dump { "none" };

    juce::String dumpFormat;   // p. ex. "waldorf-microwave-1"

    // DANGER. Sur le M, recevoir un sysex de son ecrase immediatement le
    // programme SELECTIONNE sur la machine, pas seulement le tampon d'edition.
    // Un editeur ne doit jamais envoyer de dump sans confirmation explicite.
    bool dumpOverwritesStoredProgram = false;
};

// Zone du panneau. Sur le materiel, une section = une page d'encodeurs.
struct DeviceSection {
    juce::String id, name;
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;   // normalises 0..1
};

class DeviceProfile {
public:
    DeviceProfile() = default;
    DeviceProfile(juce::String name, std::vector<DeviceParam> params)
        : name_(std::move(name)), params_(std::move(params)) {}

    void setSync(DeviceSync s) { sync_ = std::move(s); }

    // POLYPHONIE de l'appareil. C'est elle qui borne la densite du voicing : sur un
    // mono, densite et ancrage n'ont pas d'objet et ne s'affichent pas. Le moteur ne
    // peut pas la deviner — il ne connait que sa propre borne, kMaxVoicing.
    // Defaut 1 : un profil qui ne le declare pas est traite comme monophonique, ce
    // qui est le comportement historique (une note par pas).
    void setVoices(int v) { voices_ = v < 1 ? 1 : v; }
    int  voices() const noexcept { return voices_; }

    void setPanel(juce::String image, float aspect, std::vector<DeviceSection> sections) {
        image_ = std::move(image); aspect_ = aspect; sections_ = std::move(sections);
    }

    const juce::String& name() const noexcept { return name_; }
    bool  isEmpty()            const noexcept { return params_.empty(); }

    const DeviceParam* find(int cc) const noexcept;

    // Parametre dont le champ learn vaut ce CC entrant. nullptr si aucun.
    const DeviceParam* findByLearn(int incomingCc) const noexcept;

    const juce::String&               imageFile() const noexcept { return image_; }
    float                             aspect()    const noexcept { return aspect_; }
    const std::vector<DeviceSection>& sections()  const noexcept { return sections_; }
    const DeviceSync&                 sync()      const noexcept { return sync_; }
    const std::vector<DeviceParam>&   params()    const noexcept { return params_; }

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
    juce::String               name_ { "Aucun" };
    std::vector<DeviceParam>   params_;
    juce::String               image_;
    float                      aspect_ = 0.0f;
    std::vector<DeviceSection> sections_;
    DeviceSync                 sync_;
    int                        voices_ = 1;   // polyphonie ; 1 = mono
};
