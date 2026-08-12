#pragma once

#include <juce_core/juce_core.h>

#include <cmath>

// Helpers FILE-LOCAL partagés entre les unités de compilation de l'éditeur
// (PluginEditor*.cpp). Déplacés ici depuis l'anonymous namespace de
// PluginEditor.cpp pour rester accessibles à tous les .cpp découpés.
// Gardés au scope GLOBAL en `inline` (C++17) pour ne pas toucher les sites d'appel.

// NB : « Pas/mesure » (numSteps global) RETIRE — le nb de pas par mesure n'est plus un
// reglage global : la MESURE est la signature (num/den), et la subdivision est le N PAR ROW
// (polyrythmie, vue PATTERN). Son defaut derive de la signature (engine.defaultRowSteps()).
inline constexpr const char* kOledParamIds[] = {"bpm",        "numRows",    "tsNum",
                                                "tsDen",      "loop",       "useMidiClock", "followHost",
                                                "useHostBpm"};
inline constexpr const char* kOledTitles[]   = {"BPM",        "Rangees",    "Mes. num.",
                                              "Mes. den.",  "Boucle",     "Clk MIDI",   "Suiv. hote",
                                              "BPM hote"};
static_assert(std::size(kOledParamIds) == std::size(kOledTitles));
inline constexpr int kNumOledParams = static_cast<int>(std::size(kOledParamIds));

// Entrees VIRTUELLES de la page GLOBAL, a la suite des params APVTS. Nommees
// plutot que « kNumOledParams + 3 » dissemine dans trois fichiers : inserer une
// ligne au milieu decalait silencieusement toutes les suivantes.
enum GlobalRow : int {
    kGlobalRowChannel = kNumOledParams,   // canal MIDI de la row selectionnee
    kGlobalRowPattern,                    // pattern actif de la banque
    kGlobalRowBars,                       // nombre de mesures du pattern
    kGlobalRowMode,                       // Pattern <-> Song
    kGlobalRowProfile,                    // profil d'appareil
    kGlobalRowResetMappings,              // action, au push
    kGlobalRowResetValues,                // action, au push
    kGlobalRowCount                       // nombre total de lignes de la page
};

inline constexpr const char* kRoman[] = {"I", "II", "III", "IV", "V", "VI", "VII"};

// « Longueur » unifiée d'une note en QUARTS de (sous-)pas : 1=¼, 4=1 pas, etc.
// Conversion bidirectionnelle avec le stockage moteur (span entier + gate %).
//   durée = span × gate% = q/4 pas.
inline juce::String lengthQuartersLabel(int q) {
    const int whole = q / 4, rem = q % 4;
    static const char* kFrac[4] = {"", juce::CharPointer_UTF8("\xc2\xbc"),
                                       juce::CharPointer_UTF8("\xc2\xbd"),
                                       juce::CharPointer_UTF8("\xc2\xbe")};   // ¼ ½ ¾
    if (whole == 0 && rem == 0) return "0";
    return (whole > 0 ? juce::String(whole) : juce::String()) + kFrac[rem] + " pas";
}
// (span, gate%) -> quarts arrondis.
inline int lengthToQuarters(int span, int gate) {
    return juce::jmax(1, static_cast<int>(std::lround(span * gate / 25.0)));
}
// quarts -> span entier (ceil) borné à maxSpan.
inline int quartersToSpan(int q, int maxSpan) {
    return juce::jlimit(1, juce::jmax(1, maxSpan), (q + 3) / 4);
}
// quarts + span -> gate% (1..100).
inline int quartersToGate(int q, int span) {
    return juce::jlimit(1, 100, static_cast<int>(std::lround(q * 25.0 / juce::jmax(1, span))));
}

// Type de division musicale donné par N pas dans une mesure tsNum/tsDen.
// Ex. 4/4 : N=16 -> "1/16", N=12 -> "1/8T", N=20 -> "5:tps". Vide si non interprétable.
inline juce::String divisionLabel(int n, int tsNum, int tsDen) {
    if (n <= 0 || tsNum <= 0 || tsDen <= 0 || (n % tsNum) != 0)
        return {};
    const int spb = n / tsNum;                       // pas par temps
    auto isPow2 = [](int x) { return x > 0 && (x & (x - 1)) == 0; };
    if (isPow2(spb))
        return "1/" + juce::String(tsDen * spb);     // division binaire droite
    if ((spb % 3) == 0 && isPow2((spb / 3) * 2))     // spb = 1.5 × puissance de 2 -> triolet
        return "1/" + juce::String(tsDen * (spb / 3) * 2) + "T";
    return juce::String(spb) + ":tps";               // tuplet irrégulier (quintolet, septolet…)
}
