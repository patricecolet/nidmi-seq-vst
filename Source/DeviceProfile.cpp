#include "DeviceProfile.h"

// ─────────────────────────────────────────────────────────────────────
// Profil d'exemple ecrit au premier lancement
//
// Genere depuis la carte MIDI du firmware Kobol :
//   Documents/Arduino/synth/kobol-expander/midi-cv/midi-map.json
//   python3 tools/gen_device_profile.py --write
//
// Il sert de graine ET de documentation du format. Une fois sur disque
// c'est le FICHIER qui fait foi : editer le JSON, pas cette chaine.
// ─────────────────────────────────────────────────────────────────────

// >>> GENERE DEPUIS midi-map.json — NE PAS EDITER A LA MAIN
static const char* kKobolSeedJson = R"JSON({
  "name": "Kobol Expander",
  "parameters": [
    { "cc":  74, "short": "Cutoff",  "name": "VCF Cutoff",              "group": "VCF", "wired": true  },
    { "cc":  71, "short": "Reso",    "name": "VCF Resonance",           "group": "VCF", "wired": false },
    { "cc":  73, "short": "VcfAtk",  "name": "VCF Attack",              "group": "VCF", "wired": false },
    { "cc":  75, "short": "VcfDec",  "name": "VCF Decay",               "group": "VCF", "wired": false },
    { "cc": 102, "short": "VcfSus",  "name": "VCF Sustain",             "group": "VCF", "wired": false },
    { "cc": 103, "short": "AdsCtl",  "name": "VCF ADS Ctrl",            "group": "VCF", "wired": false },
    { "cc": 105, "short": "VcaAtk",  "name": "VCA Attack",              "group": "VCA", "wired": false },
    { "cc": 106, "short": "VcaDec",  "name": "VCA Decay",               "group": "VCA", "wired": false },
    { "cc": 107, "short": "VcaSus",  "name": "VCA Sustain",             "group": "VCA", "wired": false },
    { "cc": 109, "short": "Vol2",    "name": "VCO2 Volume",             "group": "VCO", "wired": false },
    { "cc": 108, "short": "Vol1",    "name": "VCO1 Volume",             "group": "VCO", "wired": false },
    { "cc": 112, "short": "Wave1",   "name": "VCO1 Waveform",           "group": "VCO", "wired": false },
    { "cc": 113, "short": "Wave2",   "name": "VCO2 Waveform",           "group": "VCO", "wired": false },
    { "cc":  76, "short": "LfoRate", "name": "LFO Rate",                "group": "LFO", "wired": true  },
    { "cc":   1, "short": "Mod",     "name": "Mod Wheel",               "group": "Mod", "wired": false },
    { "cc":   5, "short": "Porta",   "name": "Portamento Time",         "group": "Mod", "wired": true  },
    { "cc":  65, "short": "PortSw",  "name": "Portamento On/Off",       "group": "Mod", "wired": true  },
    { "cc": 114, "short": "VelCutN", "name": "Velocite > Cutoff (on)",  "group": "Mod", "wired": true  },
    { "cc": 115, "short": "VelCutF", "name": "Velocite > Cutoff (off)", "group": "Mod", "wired": true  },
    { "cc": 116, "short": "VelVca",  "name": "Velocite > VCA",          "group": "Mod", "wired": false },
    { "cc": 118, "short": "Gate",    "name": "Gate force",              "group": "Mod", "wired": true  }
  ]
})JSON";
// <<< FIN DU BLOC GENERE

// ─────────────────────────────────────────────────────────────────────

static std::vector<DeviceProfile> s_profiles;
static juce::String               s_summary;
static bool                       s_loaded = false;

juce::File DeviceProfile::profilesDirectory() {
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
               .getChildFile("NiDMI")
               .getChildFile("Profiles");
}

// Un profil valide a un nom non vide et au moins un parametre. On refuse
// silencieusement le reste plutot que d'afficher des noms fantaisistes sur
// des CC : mieux vaut « CC 74 » qu'un mauvais libelle.
static bool parseProfile(const juce::var& root, DeviceProfile& out, juce::String& why) {
    if (!root.isObject()) { why = "racine JSON non objet"; return false; }

    const juce::String name = root.getProperty("name", {}).toString().trim();
    if (name.isEmpty()) { why = "champ \"name\" absent ou vide"; return false; }

    const juce::var params = root.getProperty("parameters", {});
    const juce::Array<juce::var>* arr = params.getArray();
    if (arr == nullptr) { why = "champ \"parameters\" absent ou non tableau"; return false; }

    std::vector<DeviceParam> list;
    list.reserve(static_cast<size_t>(arr->size()));
    for (const auto& e : *arr) {
        if (!e.isObject()) continue;
        DeviceParam p;
        p.cc = static_cast<int>(e.getProperty("cc", -1));
        if (p.cc < 0 || p.cc > 127) continue;      // hors plage MIDI : ignore
        p.name      = e.getProperty("name", {}).toString();
        p.shortName = e.getProperty("short", {}).toString();
        p.group     = e.getProperty("group", {}).toString();
        p.wired     = static_cast<bool>(e.getProperty("wired", true));
        if (p.name.isEmpty()) continue;
        if (p.shortName.isEmpty()) p.shortName = p.name.substring(0, 7);
        list.push_back(std::move(p));
    }
    if (list.empty()) { why = "aucun parametre exploitable"; return false; }

    out = DeviceProfile(name, std::move(list));
    return true;
}

void DeviceProfile::reload() {
    s_profiles.clear();
    s_profiles.emplace_back();          // index 0 : « Aucun », toujours present

    const juce::File dir = profilesDirectory();
    int ok = 0, bad = 0;
    juce::StringArray problems;

    if (!dir.exists()) {
        // Premier lancement : on cree le dossier et on y depose le Kobol, qui
        // sert d'exemple du format. Ensuite c'est le fichier qui fait foi.
        if (dir.createDirectory().wasOk())
            dir.getChildFile("kobol-expander.json").replaceWithText(kKobolSeedJson);
    }

    for (const auto& f : dir.findChildFiles(juce::File::findFiles, false, "*.json")) {
        juce::var root;
        const auto res = juce::JSON::parse(f.loadFileAsString(), root);
        DeviceProfile p;
        juce::String why;
        if (!res.wasOk())               why = res.getErrorMessage();
        else if (!parseProfile(root, p, why)) {}
        else {
            // Un fichier plus recent portant le meme nom remplace le precedent.
            auto it = std::find_if(s_profiles.begin(), s_profiles.end(),
                                   [&](const DeviceProfile& e) { return e.name() == p.name(); });
            if (it != s_profiles.end()) *it = p;
            else                        s_profiles.push_back(p);
            ++ok;
            continue;
        }
        ++bad;
        problems.add(f.getFileName() + " (" + why + ")");
    }

    // Tri alphabetique a partir de l'index 1 : « Aucun » reste en tete.
    if (s_profiles.size() > 2)
        std::sort(s_profiles.begin() + 1, s_profiles.end(),
                  [](const DeviceProfile& a, const DeviceProfile& b) { return a.name() < b.name(); });

    s_summary = juce::String(ok) + " profil(s) charge(s) depuis " + dir.getFullPathName();
    if (bad > 0)
        s_summary += "  |  " + juce::String(bad) + " rejete(s) : " + problems.joinIntoString(", ");
    s_loaded = true;
}

static void ensureLoaded() {
    if (!s_loaded) DeviceProfile::reload();
}

const DeviceParam* DeviceProfile::find(int cc) const noexcept {
    for (const auto& p : params_)
        if (p.cc == cc)
            return &p;
    return nullptr;
}

juce::String DeviceProfile::label(int cc) const {
    if (const auto* p = find(cc))
        return p->name;
    return "CC " + juce::String(cc);
}

juce::String DeviceProfile::shortLabel(int cc) const {
    if (const auto* p = find(cc))
        return p->shortName;
    return juce::String(cc);
}

int DeviceProfile::count() noexcept {
    ensureLoaded();
    return static_cast<int>(s_profiles.size());
}

const DeviceProfile& DeviceProfile::byIndex(int index) noexcept {
    ensureLoaded();
    return s_profiles[static_cast<size_t>(
        juce::jlimit(0, static_cast<int>(s_profiles.size()) - 1, index))];
}

int DeviceProfile::indexOfName(const juce::String& name) noexcept {
    ensureLoaded();
    for (size_t i = 0; i < s_profiles.size(); ++i)
        if (s_profiles[i].name() == name)
            return static_cast<int>(i);
    return 0;   // inconnu -> « Aucun » : on n'invente pas de noms de CC
}

juce::String DeviceProfile::lastScanSummary() {
    ensureLoaded();
    return s_summary;
}
