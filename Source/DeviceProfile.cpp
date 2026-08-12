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
  "schema": 2,
  "name": "Kobol Expander",
  "manufacturer": "RSF",
  "panel": {
    "image": "kobol-expander.jpg",
    "aspect": 3.18,
    "_comment": "Ratio 3,18:1. Sur un ecran 320x240 la photo ne fait que 320x101 : 58 % de hauteur perdue, potards a 12 px, labels a 1 px. L'image sert a l'editeur bureau ; le materiel dessine les sections a partir de 'section' et 'pos'.",
    "sections": [
      {
        "id": "lfo",
        "name": "LFO",
        "rect": [
          0.02,
          0.05,
          0.16,
          0.9
        ]
      },
      {
        "id": "vco",
        "name": "VCO",
        "rect": [
          0.2,
          0.05,
          0.3,
          0.9
        ]
      },
      {
        "id": "vcf",
        "name": "VCF",
        "rect": [
          0.51,
          0.05,
          0.15,
          0.9
        ]
      },
      {
        "id": "env",
        "name": "ADS",
        "rect": [
          0.66,
          0.05,
          0.25,
          0.9
        ]
      },
      {
        "id": "mod",
        "name": "Mod",
        "rect": [
          0.0,
          0.0,
          0.0,
          0.0
        ]
      }
    ]
  },
  "pitch": {
    "name": "Pitch",
    "driven_by": "note",
    "output": "MCP4822 canal A",
    "p1_pin": 11,
    "base_note": 0,
    "mv_per_octave": 400,
    "calibrated": true,
    "bend_cents_max": 200,
    "_comment": "Reprend v1-first-release/kobolDAC.ino, qui tourne dans le Teensy et sonne juste : 4000 unités DAC pour 120 demi-tons. Ce n'est pas du 1 V/oct malgré ce qu'annonce le README de la v1.",
    "section": "vco",
    "type": "knob",
    "pos": [
      0.3,
      0.705
    ],
    "size": 0.045
  },
  "parameters": [
    {
      "cc": 74,
      "short": "Cutoff",
      "name": "VCF Cutoff",
      "group": "VCF",
      "section": "vcf",
      "type": "knob",
      "pos": [
        0.539,
        0.285
      ],
      "size": 0.045,
      "wired": true,
      "default": 90,
      "learn": null
    },
    {
      "cc": 71,
      "short": "Reso",
      "name": "VCF Resonance",
      "group": "VCF",
      "section": "vcf",
      "type": "knob",
      "pos": [
        0.618,
        0.285
      ],
      "size": 0.045,
      "wired": false,
      "default": 0,
      "learn": null
    },
    {
      "cc": 73,
      "short": "VcfAtk",
      "name": "VCF Attack",
      "group": "VCF",
      "section": "env",
      "type": "knob",
      "pos": [
        0.697,
        0.705
      ],
      "size": 0.045,
      "wired": false,
      "default": 0,
      "learn": null
    },
    {
      "cc": 75,
      "short": "VcfDec",
      "name": "VCF Decay",
      "group": "VCF",
      "section": "env",
      "type": "knob",
      "pos": [
        0.776,
        0.705
      ],
      "size": 0.045,
      "wired": false,
      "default": 40,
      "learn": null
    },
    {
      "cc": 102,
      "short": "VcfSus",
      "name": "VCF Sustain",
      "group": "VCF",
      "section": "env",
      "type": "knob",
      "pos": [
        0.855,
        0.705
      ],
      "size": 0.045,
      "wired": false,
      "default": 90,
      "learn": null
    },
    {
      "cc": 103,
      "short": "AdsCtl",
      "name": "VCF ADS Ctrl",
      "group": "VCF",
      "section": "vcf",
      "type": "knob",
      "pos": [
        0.618,
        0.705
      ],
      "size": 0.045,
      "wired": false,
      "default": 64,
      "learn": null
    },
    {
      "cc": 105,
      "short": "VcaAtk",
      "name": "VCA Attack",
      "group": "VCA",
      "section": "env",
      "type": "knob",
      "pos": [
        0.697,
        0.285
      ],
      "size": 0.045,
      "wired": false,
      "default": 0,
      "learn": null
    },
    {
      "cc": 106,
      "short": "VcaDec",
      "name": "VCA Decay",
      "group": "VCA",
      "section": "env",
      "type": "knob",
      "pos": [
        0.776,
        0.285
      ],
      "size": 0.045,
      "wired": false,
      "default": 40,
      "learn": null
    },
    {
      "cc": 107,
      "short": "VcaSus",
      "name": "VCA Sustain",
      "group": "VCA",
      "section": "env",
      "type": "knob",
      "pos": [
        0.855,
        0.285
      ],
      "size": 0.045,
      "wired": false,
      "default": 100,
      "learn": null
    },
    {
      "cc": 109,
      "short": "Vol2",
      "name": "VCO2 Volume",
      "group": "VCO",
      "section": "vco",
      "type": "knob",
      "pos": [
        0.457,
        0.705
      ],
      "size": 0.045,
      "wired": false,
      "default": 100,
      "learn": null
    },
    {
      "cc": 108,
      "short": "Vol1",
      "name": "VCO1 Volume",
      "group": "VCO",
      "section": "vco",
      "type": "knob",
      "pos": [
        0.457,
        0.285
      ],
      "size": 0.045,
      "wired": false,
      "default": 100,
      "learn": null
    },
    {
      "cc": 112,
      "short": "Wave1",
      "name": "VCO1 Waveform",
      "group": "VCO",
      "section": "vco",
      "type": "knob",
      "pos": [
        0.379,
        0.285
      ],
      "size": 0.045,
      "wired": false,
      "default": 0,
      "learn": null
    },
    {
      "cc": 113,
      "short": "Wave2",
      "name": "VCO2 Waveform",
      "group": "VCO",
      "section": "vco",
      "type": "knob",
      "pos": [
        0.379,
        0.705
      ],
      "size": 0.045,
      "wired": false,
      "default": 0,
      "learn": null
    },
    {
      "cc": 76,
      "short": "LfoRate",
      "name": "LFO Rate",
      "group": "LFO",
      "section": "lfo",
      "type": "knob",
      "pos": [
        0.068,
        0.285
      ],
      "size": 0.045,
      "wired": true,
      "default": 0,
      "learn": null
    },
    {
      "cc": 1,
      "short": "Mod",
      "name": "Mod Wheel",
      "group": "Mod",
      "section": "mod",
      "type": "knob",
      "pos": null,
      "size": 0.045,
      "wired": false,
      "default": 0,
      "learn": null
    },
    {
      "cc": 5,
      "short": "Porta",
      "name": "Portamento Time",
      "group": "Mod",
      "section": "mod",
      "type": "knob",
      "pos": null,
      "size": 0.045,
      "wired": true,
      "default": 0,
      "learn": null
    },
    {
      "cc": 65,
      "short": "PortSw",
      "name": "Portamento On/Off",
      "group": "Mod",
      "section": "mod",
      "type": "knob",
      "pos": null,
      "size": 0.045,
      "wired": true,
      "default": 127,
      "learn": null
    },
    {
      "cc": 114,
      "short": "VelCutN",
      "name": "Velocite > Cutoff (on)",
      "group": "Mod",
      "section": "mod",
      "type": "knob",
      "pos": null,
      "size": 0.045,
      "wired": true,
      "default": 0,
      "learn": null
    },
    {
      "cc": 115,
      "short": "VelCutF",
      "name": "Velocite > Cutoff (off)",
      "group": "Mod",
      "section": "mod",
      "type": "knob",
      "pos": null,
      "size": 0.045,
      "wired": true,
      "default": 0,
      "learn": null
    },
    {
      "cc": 116,
      "short": "VelVca",
      "name": "Velocite > VCA",
      "group": "Mod",
      "section": "mod",
      "type": "knob",
      "pos": null,
      "size": 0.045,
      "wired": false,
      "default": 0,
      "learn": null
    },
    {
      "cc": 118,
      "short": "Gate",
      "name": "Gate force",
      "group": "Mod",
      "section": "mod",
      "type": "knob",
      "pos": null,
      "size": 0.045,
      "wired": true,
      "default": 0,
      "learn": null
    }
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

        // v2 : geometrie du panneau. Absente en v1 -> le parametre existe mais
        // n'a pas de place a l'ecran, ce qui est aussi le cas des modulations
        // purement logicielles.
        p.section = e.getProperty("section", {}).toString();
        const juce::String t = e.getProperty("type", {}).toString();
        if (t.isNotEmpty()) p.type = t;
        if (const auto* xy = e.getProperty("pos", {}).getArray(); xy != nullptr && xy->size() == 2) {
            p.hasPos = true;
            p.x = static_cast<float>(static_cast<double>((*xy)[0]));
            p.y = static_cast<float>(static_cast<double>((*xy)[1]));
        }
        const juce::var sz = e.getProperty("size", {});
        if (!sz.isVoid()) p.size = static_cast<float>(static_cast<double>(sz));
        const juce::var lr = e.getProperty("learn", {});
        p.learn = lr.isVoid() ? -1 : static_cast<int>(lr);   // null JSON -> void
        if (p.learn < 0 || p.learn > 127) p.learn = -1;
        p.defaultValue = juce::jlimit(0, 127, static_cast<int>(e.getProperty("default", 0)));

        if (p.name.isEmpty()) continue;
        if (p.shortName.isEmpty()) p.shortName = p.name.substring(0, 7);
        list.push_back(std::move(p));
    }
    if (list.empty()) { why = "aucun parametre exploitable"; return false; }

    out = DeviceProfile(name, std::move(list));

    // Panneau : facultatif. Sans lui le profil reste valide, il ne sert
    // simplement qu'au nommage des CC.
    if (const juce::var panel = root.getProperty("panel", {}); panel.isObject()) {
        std::vector<DeviceSection> secs;
        if (const auto* arr = panel.getProperty("sections", {}).getArray())
            for (const auto& sv : *arr) {
                if (!sv.isObject()) continue;
                DeviceSection sec;
                sec.id   = sv.getProperty("id", {}).toString();
                sec.name = sv.getProperty("name", {}).toString();
                if (const auto* r = sv.getProperty("rect", {}).getArray(); r && r->size() == 4) {
                    sec.x = static_cast<float>(static_cast<double>((*r)[0]));
                    sec.y = static_cast<float>(static_cast<double>((*r)[1]));
                    sec.w = static_cast<float>(static_cast<double>((*r)[2]));
                    sec.h = static_cast<float>(static_cast<double>((*r)[3]));
                }
                if (sec.id.isNotEmpty()) secs.push_back(sec);
            }
        out.setPanel(panel.getProperty("image", {}).toString(),
                     static_cast<float>(static_cast<double>(panel.getProperty("aspect", 0.0))),
                     std::move(secs));
    }
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

const DeviceParam* DeviceProfile::findByLearn(int incomingCc) const noexcept {
    for (const auto& p : params_)
        if (p.learn == incomingCc)
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
