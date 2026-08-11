#include "DeviceProfile.h"

// ─────────────────────────────────────────────────────────────────────
// RSF Kobol Expander
//
// Repris de la carte MIDI du firmware KobolMidiCV :
//   Documents/Arduino/synth/kobol-expander/midi-cv/midi-map.json
//
// Numérotation fondée sur les Sound Controllers MIDI (CC 70-79) là où un
// équivalent standard existe — CC 74 Brightness pour le cutoff, CC 71
// Harmonic Content pour la résonance — et sur la zone undefined 102-119
// pour le reste.
//
// `wired` dit si le paramètre ressort vraiment en CV aujourd'hui. Le
// matériel actuel est un Teensy 2.0 + un seul MCP4822, soit deux canaux :
// le pitch (piloté par les notes) et le cutoff. Les autres CC sont émis
// par le séquenceur mais ignorés par le firmware tant qu'un second DAC
// n'est pas monté.
// ─────────────────────────────────────────────────────────────────────

static const DeviceParam kKobolParams[] = {
    //  cc   court      nom complet             groupe  câblé
    {  74, "Cutoff",  "VCF Cutoff",             "VCF",  true  },
    {  71, "Reso",    "VCF Resonance",          "VCF",  false },
    {  73, "VcfAtk",  "VCF Attack",             "VCF",  false },
    {  75, "VcfDec",  "VCF Decay",              "VCF",  false },
    { 102, "VcfSus",  "VCF Sustain",            "VCF",  false },
    { 103, "AdsCtl",  "VCF ADS Ctrl",           "VCF",  false },
    { 105, "VcaAtk",  "VCA Attack",             "VCA",  false },
    { 106, "VcaDec",  "VCA Decay",              "VCA",  false },
    { 107, "VcaSus",  "VCA Sustain",            "VCA",  false },
    { 108, "Vol1",    "VCO1 Volume",            "VCO",  false },
    { 109, "Vol2",    "VCO2 Volume",            "VCO",  false },
    { 112, "Wave1",   "VCO1 Waveform",          "VCO",  false },
    { 113, "Wave2",   "VCO2 Waveform",          "VCO",  false },
    {  76, "LfoRate", "LFO Rate",               "LFO",  false },

    // Modulations calculées par le firmware : pas de sortie CV propre.
    {   1, "Mod",     "Mod Wheel",              "Mod",  true  },
    {   5, "Porta",   "Portamento Time",        "Mod",  true  },
    {  65, "PortSw",  "Portamento On/Off",      "Mod",  true  },
    { 114, "VelCutN", "Velocite > Cutoff (on)", "Mod",  true  },
    { 115, "VelCutF", "Velocite > Cutoff (off)","Mod",  true  },
    { 116, "VelVca",  "Velocite > VCA",         "Mod",  true  },
    { 118, "Gate",    "Gate force",             "Mod",  true  },
};

// ─────────────────────────────────────────────────────────────────────

static const DeviceProfile kProfiles[] = {
    DeviceProfile("Aucun",          nullptr,       0),
    DeviceProfile("Kobol Expander", kKobolParams,
                  static_cast<int>(sizeof(kKobolParams) / sizeof(kKobolParams[0]))),
};

static constexpr int kProfileCount =
    static_cast<int>(sizeof(kProfiles) / sizeof(kProfiles[0]));

const DeviceParam* DeviceProfile::find(int cc) const noexcept {
    if (params_ == nullptr)
        return nullptr;
    for (int i = 0; i < count_; ++i)
        if (params_[i].cc == cc)
            return &params_[i];
    return nullptr;
}

juce::String DeviceProfile::label(int cc) const {
    if (const auto* p = find(cc))
        return juce::String(p->name);
    return "CC " + juce::String(cc);
}

juce::String DeviceProfile::shortLabel(int cc) const {
    if (const auto* p = find(cc))
        return juce::String(p->shortName);
    return juce::String(cc);
}

int DeviceProfile::count() noexcept { return kProfileCount; }

const DeviceProfile& DeviceProfile::byIndex(int index) noexcept {
    return kProfiles[juce::jlimit(0, kProfileCount - 1, index)];
}

int DeviceProfile::indexOfName(const juce::String& name) noexcept {
    for (int i = 0; i < kProfileCount; ++i)
        if (kProfiles[i].name() == name)
            return i;
    return 0;   // inconnu -> « Aucun », on n'invente pas de noms de CC
}
