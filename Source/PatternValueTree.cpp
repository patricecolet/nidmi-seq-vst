#include "PatternValueTree.h"

#include <nidmi_seq/SequencerCommandApi.h>
#include <nidmi_seq/SequencerEngine.h>
#include <nidmi_seq/StepTypes.h>
#include <nidmi_seq/ChainVM.h>

namespace {

juce::String offsetsToString(const HarmonicProgressionBuf& p) {
    juce::String s;
    for (uint8_t i = 0; i < p.len; ++i) {
        if (i) s << ",";
        s << juce::String(static_cast<int>(p.chordSemitoneOffsets[i]));
    }
    return s;
}

bool parseOffsets(const juce::String& s, int8_t* out, uint8_t* outLen) {
    *outLen = 0;
    if (s.isEmpty())
        return true;
    const juce::StringArray arr = juce::StringArray::fromTokens(s, ",", "");
    for (int i = 0; i < arr.size() && *outLen < kMaxProgSteps; ++i) {
        out[*outLen] = static_cast<int8_t>(arr[i].getIntValue());
        ++(*outLen);
    }
    return true;
}

// -------------------------------------------------------------------------
// Construit un noeud "P" portant TOUT le contenu PAR PATTERN (steps, rows,
// harmonie, progressions, sous-patterns). Les champs GLOBAUX (bpm/loop/
// tonalité maître) vivent au niveau racine, pas ici.
// -------------------------------------------------------------------------
juce::ValueTree buildPatternNode(const Pattern& p, int idx) {
    juce::ValueTree node("P");
    node.setProperty("i", idx, nullptr);
    node.setProperty("steps", p.numSteps, nullptr);
    node.setProperty("rows", p.numRows, nullptr);
    node.setProperty("numBars", p.numBars, nullptr);   // multi-mesures (1..kMaxBars ; absent = 1)
    node.setProperty("num", p.numerator, nullptr);
    node.setProperty("den", p.denominator, nullptr);

    juce::ValueTree h("Harmony");
    h.setProperty("en", p.harmony.harmonyEnabled ? 1 : 0, nullptr);
    h.setProperty("scale", p.harmony.scaleId, nullptr);
    h.setProperty("root", p.harmony.rootPc, nullptr);
    h.setProperty("follow", p.harmony.followProgression ? 1 : 0, nullptr);
    h.setProperty("advSub", p.harmony.advanceProgOnSubPatternEnd ? 1 : 0, nullptr);
    // Suivi de la tonalité MAÎTRE : drapeau PAR PATTERN (la tonalité maître
    // elle-même est globale, sérialisée à la racine).
    h.setProperty("fmaster", p.harmony.followMasterTonality ? 1 : 0, nullptr);
    node.appendChild(h, nullptr);

    juce::ValueTree t("Timing");
    t.setProperty("accent", p.timing.accentAmount, nullptr);
    t.setProperty("swing", p.timing.swingEnabled ? 1 : 0, nullptr);
    t.setProperty("swingPct", p.timing.swingPositionPercent, nullptr);
    node.appendChild(t, nullptr);

    juce::ValueTree prog("Progression");
    prog.setProperty("len", p.progression.len, nullptr);
    prog.setProperty("idx", p.progression.idx, nullptr);
    prog.setProperty("offsets", offsetsToString(p.progression), nullptr);
    node.appendChild(prog, nullptr);

    // Progression enrichie V1 (accords : degre + qualite + extensions + inversion).
    juce::ValueTree cprog("ChordProgression");
    cprog.setProperty("len", p.chordProgression.len, nullptr);
    cprog.setProperty("idx", p.chordProgression.idx, nullptr);
    for (uint8_t i = 0; i < p.chordProgression.len; ++i) {
        const ChordSlot& cs = p.chordProgression.slots[i];
        juce::ValueTree cs_vt("Slot");
        cs_vt.setProperty("i",   i,                                     nullptr);
        cs_vt.setProperty("deg", cs.degree,                              nullptr);
        cs_vt.setProperty("qua", static_cast<int>(cs.quality),           nullptr);
        cs_vt.setProperty("ext", cs.extensions,                          nullptr);
        cs_vt.setProperty("bas", static_cast<int>(cs.bassOffset),        nullptr);
        cs_vt.setProperty("dur", cs.durationBeats,                       nullptr);
        cprog.appendChild(cs_vt, nullptr);
    }
    node.appendChild(cprog, nullptr);

    // Lane de TONALITÉ (marqueurs root+gamme+durée en temps).
    juce::ValueTree kprog("KeyProgression");
    kprog.setProperty("len", p.keyProgression.len, nullptr);
    kprog.setProperty("idx", p.keyProgression.idx, nullptr);
    for (uint8_t i = 0; i < p.keyProgression.len; ++i) {
        const KeySlot& ks = p.keyProgression.slots[i];
        juce::ValueTree ks_vt("KSlot");
        ks_vt.setProperty("i",   i,                              nullptr);
        ks_vt.setProperty("rt",  static_cast<int>(ks.rootPc),    nullptr);
        ks_vt.setProperty("sc",  static_cast<int>(ks.scaleId),   nullptr);
        ks_vt.setProperty("dur", static_cast<int>(ks.durationBeats), nullptr);
        kprog.appendChild(ks_vt, nullptr);
    }
    node.appendChild(kprog, nullptr);

    juce::ValueTree rows("Rows");
    for (uint8_t r = 0; r < p.numRows; ++r) {
        juce::ValueTree row("R");
        row.setProperty("i", r, nullptr);
        row.setProperty("ch", p.rows[r].channel, nullptr);
        row.setProperty("ns", p.rows[r].numSteps, nullptr);
        row.setProperty("k", static_cast<int>(p.rows[r].kind), nullptr);
        row.setProperty("cc", p.rows[r].ccNumber, nullptr);
        // Mode harmonique PAR MESURE (hm0..hm3). hm0 sert aussi de marqueur de version :
        // un ancien projet n'a que "hm" (unique) → migré sur toutes les mesures au chargement.
        for (uint8_t bar = 0; bar < kMaxBars; ++bar)
            row.setProperty("hm" + juce::String(static_cast<int>(bar)),
                            static_cast<int>(p.rows[r].harmonyMode[bar]), nullptr);
        row.setProperty("mu", p.rows[r].muted ? 1 : 0, nullptr);
        // Sérialisation PAR MESURE : chaque pas porte sa mesure ("b"). Absent au chargement
        // => mesure 0 (rétrocompat avec les anciens projets mono-mesure).
        for (uint8_t bar = 0; bar < p.numBars && bar < kMaxBars; ++bar) {
            for (uint8_t s = 0; s < p.numSteps; ++s) {
                const StepData& sd = p.rows[r].step(bar, s);
                juce::ValueTree st("S");
                st.setProperty("b", bar, nullptr);
                st.setProperty("j", s, nullptr);
                st.setProperty("n", sd.note, nullptr);
                st.setProperty("v", sd.velocity, nullptr);
                st.setProperty("g", sd.gate, nullptr);
                st.setProperty("on", sd.enabled ? 1 : 0, nullptr);
                st.setProperty("sub", sd.subPatIdx, nullptr);
                st.setProperty("ac", sd.accent ? 1 : 0, nullptr);
                st.setProperty("sw", sd.swingEnable ? 1 : 0, nullptr);
                st.setProperty("span", sd.span, nullptr);   // note longue / sub étalé (1 = normal)
                // P-locks CC : un enfant "CC" par slot actif.
                for (uint8_t k = 0; k < kMaxCCLocksPerStep; ++k) {
                    if (sd.ccLocks[k].ccNumber == kNoCCLock) continue;
                    juce::ValueTree cc("CC");
                    cc.setProperty("k", k, nullptr);
                    cc.setProperty("c", sd.ccLocks[k].ccNumber, nullptr);
                    cc.setProperty("v", sd.ccLocks[k].value, nullptr);
                    st.appendChild(cc, nullptr);
                }
                row.appendChild(st, nullptr);
            }
        }
        rows.appendChild(row, nullptr);
    }
    node.appendChild(rows, nullptr);

    juce::ValueTree subs("Subs");
    for (uint8_t i = 0; i < kMaxSubPatterns; ++i) {
        const SubPattern& sp = p.subPatterns[i];
        if (sp.numSteps == 0)
            continue;
        juce::ValueTree sub("Sub");
        sub.setProperty("orig", i, nullptr);
        sub.setProperty("ns", sp.numSteps, nullptr);
        sub.setProperty("dur", sp.duration, nullptr);
        sub.setProperty("apoe", sp.advanceProgOnEnd ? 1 : 0, nullptr);
        sub.setProperty("rel", sp.relativeToHost ? 1 : 0, nullptr);   // mode REL/ABS du sub
        juce::ValueTree sh("Harmony");
        sh.setProperty("en", sp.harmony.harmonyEnabled ? 1 : 0, nullptr);
        sh.setProperty("scale", sp.harmony.scaleId, nullptr);
        sh.setProperty("root", sp.harmony.rootPc, nullptr);
        sub.appendChild(sh, nullptr);
        juce::ValueTree stt("Timing");
        stt.setProperty("accent", sp.timing.accentAmount, nullptr);
        stt.setProperty("swing", sp.timing.swingEnabled ? 1 : 0, nullptr);
        stt.setProperty("swingPct", sp.timing.swingPositionPercent, nullptr);
        sub.appendChild(stt, nullptr);
        for (uint8_t j = 0; j < sp.numSteps; ++j) {
            const StepData& sd = sp.steps[j];
            juce::ValueTree ss("SS");
            ss.setProperty("j", j, nullptr);
            ss.setProperty("n", sd.note, nullptr);
            ss.setProperty("v", sd.velocity, nullptr);
            ss.setProperty("g", sd.gate, nullptr);
            ss.setProperty("on", sd.enabled ? 1 : 0, nullptr);
            ss.setProperty("ac", sd.accent ? 1 : 0, nullptr);
            ss.setProperty("sw", sd.swingEnable ? 1 : 0, nullptr);
            ss.setProperty("span", sd.span, nullptr);   // longueur du sous-pas (1 = normal)
            for (uint8_t k = 0; k < kMaxCCLocksPerStep; ++k) {
                if (sd.ccLocks[k].ccNumber == kNoCCLock) continue;
                juce::ValueTree cc("CC");
                cc.setProperty("k", k, nullptr);
                cc.setProperty("c", sd.ccLocks[k].ccNumber, nullptr);
                cc.setProperty("v", sd.ccLocks[k].value, nullptr);
                ss.appendChild(cc, nullptr);
            }
            sub.appendChild(ss, nullptr);
        }
        subs.appendChild(sub, nullptr);
    }
    node.appendChild(subs, nullptr);

    return node;
}

// Un pattern « porte du contenu » s'il a au moins un pas activé, ou une
// progression d'accords / de tonalité non vide. (Sert à n'écrire dans la
// banque que les patterns utiles ; les slots vierges restent implicites.)
bool patternHasContent(const Pattern& p) {
    if (p.chordProgression.len > 0 || p.keyProgression.len > 0)
        return true;
    for (uint8_t r = 0; r < p.numRows; ++r)
        for (uint8_t bar = 0; bar < p.numBars && bar < kMaxBars; ++bar)
            for (uint8_t s = 0; s < p.numSteps; ++s)
                if (p.rows[r].step(bar, s).enabled)
                    return true;
    return false;
}

// -------------------------------------------------------------------------
// Applique un noeud "P" (ou la racine d'un ancien projet v1) au pattern ACTIF.
// Les propriétés globales (bpm/loop/tonalité maître) ne sont lues ici que si
// elles sont présentes sur le noeud (cas v1 legacy) ; en v2 elles sont
// appliquées une seule fois au niveau racine par applyToEngine.
// -------------------------------------------------------------------------
void applyPatternNode(SequencerEngine& engine, const juce::ValueTree& node, int64_t nowUs) {
    SequencerCommand c;
    c.id = SequencerCommandId::ClearPattern;
    SequencerCommandApi::dispatch(engine, c, nowUs);

    const uint8_t numSteps = static_cast<uint8_t>(static_cast<int>(node.getProperty("steps", 16)));
    const uint8_t numRows  = static_cast<uint8_t>(static_cast<int>(node.getProperty("rows", 4)));
    // numBars : défaut 1 si absent (rétrocompat des anciens projets mono-mesure).
    const uint8_t numBars  = static_cast<uint8_t>(juce::jlimit(1, static_cast<int>(kMaxBars),
                                                               static_cast<int>(node.getProperty("numBars", 1))));
    const uint8_t num      = static_cast<uint8_t>(static_cast<int>(node.getProperty("num", 4)));
    const uint8_t den      = static_cast<uint8_t>(static_cast<int>(node.getProperty("den", 4)));

    c    = SequencerCommand{};
    c.id = SequencerCommandId::SetSteps;
    c.a  = numSteps;
    SequencerCommandApi::dispatch(engine, c, nowUs);
    c.id = SequencerCommandId::SetNumRows;
    c.a  = numRows;
    SequencerCommandApi::dispatch(engine, c, nowUs);
    c.id = SequencerCommandId::SetTimeSignature;
    c.a  = num;
    c.b  = den;
    SequencerCommandApi::dispatch(engine, c, nowUs);
    // bpm / loop : GLOBAUX ; uniquement présents sur un ancien projet v1.
    if (node.hasProperty("bpm")) {
        c    = SequencerCommand{};
        c.id  = SequencerCommandId::SetBpm;
        c.f32 = static_cast<float>(node.getProperty("bpm", 120.0f));
        SequencerCommandApi::dispatch(engine, c, nowUs);
    }
    if (node.hasProperty("loop")) {
        c    = SequencerCommand{};
        c.id = SequencerCommandId::SetLoop;
        c.x  = static_cast<int>(node.getProperty("loop", 1)) != 0;
        SequencerCommandApi::dispatch(engine, c, nowUs);
    }
    // Nombre de mesures AVANT le chargement des pas (les écritures ciblent une mesure via cmd.f).
    c    = SequencerCommand{};
    c.id = SequencerCommandId::SetPatternNumBars;
    c.a  = numBars;
    SequencerCommandApi::dispatch(engine, c, nowUs);

    juce::ValueTree h = node.getChildWithName("Harmony");
    if (h.isValid()) {
        c    = SequencerCommand{};
        c.id = SequencerCommandId::SetPatternHarmony;
        c.x  = static_cast<int>(h.getProperty("en", 0)) != 0;
        c.a  = static_cast<uint8_t>(static_cast<int>(h.getProperty("scale", 0)));
        c.b  = static_cast<uint8_t>(static_cast<int>(h.getProperty("root", 0)));
        c.y  = static_cast<int>(h.getProperty("follow", 0)) != 0;
        SequencerCommandApi::dispatch(engine, c, nowUs);
        c    = SequencerCommand{};
        c.id = SequencerCommandId::SetAdvanceProgOnSubPatternEnd;
        c.x  = static_cast<int>(h.getProperty("advSub", 0)) != 0;
        SequencerCommandApi::dispatch(engine, c, nowUs);
        // Tonalité MAÎTRE : GLOBALE ; uniquement présente sur un ancien projet v1.
        if (h.hasProperty("mroot")) {
            c    = SequencerCommand{};
            c.id = SequencerCommandId::SetProjectMasterRootPc;
            c.a  = static_cast<uint8_t>(static_cast<int>(h.getProperty("mroot", 0)));
            SequencerCommandApi::dispatch(engine, c, nowUs);
        }
        if (h.hasProperty("mscale")) {
            c    = SequencerCommand{};
            c.id = SequencerCommandId::SetProjectMasterScaleId;
            c.a  = static_cast<uint8_t>(static_cast<int>(h.getProperty("mscale", 0)));
            SequencerCommandApi::dispatch(engine, c, nowUs);
        }
        if (h.hasProperty("fmaster")) {
            c    = SequencerCommand{};
            c.id = SequencerCommandId::SetPatternFollowMasterTonality;
            c.x  = static_cast<int>(h.getProperty("fmaster", 0)) != 0;
            SequencerCommandApi::dispatch(engine, c, nowUs);
        }
    }

    juce::ValueTree t = node.getChildWithName("Timing");
    if (t.isValid()) {
        c    = SequencerCommand{};
        c.id = SequencerCommandId::SetPatternAccentAmount;
        c.a  = static_cast<uint8_t>(static_cast<int>(t.getProperty("accent", 0)));
        SequencerCommandApi::dispatch(engine, c, nowUs);
        c    = SequencerCommand{};
        c.id = SequencerCommandId::SetPatternSwing;
        c.x  = static_cast<int>(t.getProperty("swing", 0)) != 0;
        c.a  = static_cast<uint8_t>(static_cast<int>(t.getProperty("swingPct", 50)));
        SequencerCommandApi::dispatch(engine, c, nowUs);
    }

    juce::ValueTree prog = node.getChildWithName("Progression");
    if (prog.isValid()) {
        juce::String offs = prog.getProperty("offsets").toString();
        int8_t       buf[kMaxProgSteps];
        uint8_t      n = 0;
        if (parseOffsets(offs, buf, &n) && n > 0) {
            c       = SequencerCommand{};
            c.id    = SequencerCommandId::SetProgressionOffsets;
            c.i8ptr = buf;
            c.len   = n;
            SequencerCommandApi::dispatch(engine, c, nowUs);
        }
        (void)prog.getProperty("idx");  // idx runtime; ignore on load
    }

    juce::ValueTree cprog = node.getChildWithName("ChordProgression");
    if (cprog.isValid()) {
        for (int i = 0; i < cprog.getNumChildren(); ++i) {
            juce::ValueTree cs_vt = cprog.getChild(i);
            if (cs_vt.getType().toString() != "Slot") continue;
            const int extFull = static_cast<int>(cs_vt.getProperty("ext", 0));   // uint16
            c    = SequencerCommand{};
            c.id = SequencerCommandId::SetChordSlot;
            c.a  = static_cast<uint8_t>(static_cast<int>(cs_vt.getProperty("i",   0)));
            c.b  = static_cast<uint8_t>(static_cast<int>(cs_vt.getProperty("deg", 1)));
            c.c  = static_cast<uint8_t>(static_cast<int>(cs_vt.getProperty("qua", 0)));
            c.d  = static_cast<uint8_t>(extFull & 0xFF);            // extensions : octet bas
            c.len = static_cast<uint8_t>((extFull >> 8) & 0xFF);    // extensions : octet haut (#9/#11/b13)
            c.e  = static_cast<uint8_t>(static_cast<int>(cs_vt.getProperty("bas", 0)));
            c.f  = static_cast<uint8_t>(static_cast<int>(cs_vt.getProperty("dur", 1)));
            SequencerCommandApi::dispatch(engine, c, nowUs);
        }
        c    = SequencerCommand{};
        c.id = SequencerCommandId::SetChordProgressionLen;
        c.a  = static_cast<uint8_t>(static_cast<int>(cprog.getProperty("len", 0)));
        SequencerCommandApi::dispatch(engine, c, nowUs);
    }

    // Lane de TONALITÉ.
    juce::ValueTree kprog = node.getChildWithName("KeyProgression");
    if (kprog.isValid()) {
        for (int i = 0; i < kprog.getNumChildren(); ++i) {
            juce::ValueTree ks_vt = kprog.getChild(i);
            if (ks_vt.getType().toString() != "KSlot") continue;
            c    = SequencerCommand{};
            c.id = SequencerCommandId::SetKeySlot;
            c.a  = static_cast<uint8_t>(static_cast<int>(ks_vt.getProperty("i",   0)));
            c.b  = static_cast<uint8_t>(static_cast<int>(ks_vt.getProperty("rt",  0)));
            c.c  = static_cast<uint8_t>(static_cast<int>(ks_vt.getProperty("sc",  0)));
            c.f  = static_cast<uint8_t>(static_cast<int>(ks_vt.getProperty("dur", 4)));
            SequencerCommandApi::dispatch(engine, c, nowUs);
        }
        c    = SequencerCommand{};
        c.id = SequencerCommandId::SetKeyProgressionLen;
        c.a  = static_cast<uint8_t>(static_cast<int>(kprog.getProperty("len", 0)));
        SequencerCommandApi::dispatch(engine, c, nowUs);
    }

    uint8_t subMap[kMaxSubPatterns];
    for (auto& m : subMap)
        m = kNoSubPattern;

    juce::ValueTree subs = node.getChildWithName("Subs");
    if (subs.isValid()) {
        for (int i = 0; i < subs.getNumChildren(); ++i) {
            juce::ValueTree sub = subs.getChild(i);
            if (sub.getType().toString() != "Sub")
                continue;
            const uint8_t orig = static_cast<uint8_t>(static_cast<int>(sub.getProperty("orig", 255)));
            const uint8_t ns   = static_cast<uint8_t>(static_cast<int>(sub.getProperty("ns", 0)));
            const uint8_t dur  = static_cast<uint8_t>(static_cast<int>(sub.getProperty("dur", 1)));
            if (ns == 0)
                continue;
            c    = SequencerCommand{};
            c.id = SequencerCommandId::AllocSubPattern;
            c.a  = ns;
            c.b  = dur;
            auto r  = SequencerCommandApi::dispatch(engine, c, nowUs);
            const uint8_t newIdx = r.outU8;
            if (orig < kMaxSubPatterns && newIdx != kNoSubPattern)
                subMap[orig] = newIdx;

            c    = SequencerCommand{};
            c.id = SequencerCommandId::SetSubPatternAdvanceProgOnEnd;
            c.a  = newIdx;
            c.x  = static_cast<int>(sub.getProperty("apoe", 0)) != 0;
            SequencerCommandApi::dispatch(engine, c, nowUs);

            c    = SequencerCommand{};
            c.id = SequencerCommandId::SetSubPatternRelative;
            c.a  = newIdx;
            c.x  = static_cast<int>(sub.getProperty("rel", 0)) != 0;   // mode REL/ABS
            SequencerCommandApi::dispatch(engine, c, nowUs);

            juce::ValueTree sh = sub.getChildWithName("Harmony");
            if (sh.isValid()) {
                c    = SequencerCommand{};
                c.id = SequencerCommandId::SetSubPatternHarmony;
                c.a  = newIdx;
                c.x  = static_cast<int>(sh.getProperty("en", 0)) != 0;
                c.b  = static_cast<uint8_t>(static_cast<int>(sh.getProperty("scale", 0)));
                c.c  = static_cast<uint8_t>(static_cast<int>(sh.getProperty("root", 0)));
                SequencerCommandApi::dispatch(engine, c, nowUs);
            }
            juce::ValueTree stt = sub.getChildWithName("Timing");
            if (stt.isValid()) {
                c    = SequencerCommand{};
                c.id = SequencerCommandId::SetSubPatternTiming;
                c.a  = newIdx;
                c.b  = static_cast<uint8_t>(static_cast<int>(stt.getProperty("accent", 0)));
                c.x  = static_cast<int>(stt.getProperty("swing", 0)) != 0;
                c.c  = static_cast<uint8_t>(static_cast<int>(stt.getProperty("swingPct", 50)));
                SequencerCommandApi::dispatch(engine, c, nowUs);
            }
            for (int j = 0; j < sub.getNumChildren(); ++j) {
                juce::ValueTree ss = sub.getChild(j);
                if (ss.getType().toString() != "SS")
                    continue;
                const uint8_t sj = static_cast<uint8_t>(static_cast<int>(ss.getProperty("j", 0)));
                c    = SequencerCommand{};
                c.id = SequencerCommandId::SetSubStep;
                c.a  = newIdx;
                c.b  = sj;
                c.c  = static_cast<uint8_t>(static_cast<int>(ss.getProperty("n", 60)));
                c.d  = static_cast<uint8_t>(static_cast<int>(ss.getProperty("v", 100)));
                c.e  = static_cast<uint8_t>(static_cast<int>(ss.getProperty("g", 80)));
                SequencerCommandApi::dispatch(engine, c, nowUs);
                // Longueur (span) du sous-pas. Absent => 1 (rétrocompat).
                const int subSpan = juce::jlimit(1, 16, static_cast<int>(ss.getProperty("span", 1)));
                if (subSpan > 1) {
                    c    = SequencerCommand{};
                    c.id = SequencerCommandId::SetSubStepSpan;
                    c.a  = newIdx;
                    c.b  = sj;
                    c.c  = static_cast<uint8_t>(subSpan);
                    SequencerCommandApi::dispatch(engine, c, nowUs);
                }
                if (static_cast<int>(ss.getProperty("on", 0)) == 0) {
                    c    = SequencerCommand{};
                    c.id = SequencerCommandId::ToggleSubStep;
                    c.a  = newIdx;
                    c.b  = sj;
                    SequencerCommandApi::dispatch(engine, c, nowUs);
                }
                c    = SequencerCommand{};
                c.id = SequencerCommandId::SetSubStepAccent;
                c.a  = newIdx;
                c.b  = sj;
                c.x  = static_cast<int>(ss.getProperty("ac", 0)) != 0;
                SequencerCommandApi::dispatch(engine, c, nowUs);
                c    = SequencerCommand{};
                c.id = SequencerCommandId::SetSubStepSwing;
                c.a  = newIdx;
                c.b  = sj;
                c.x  = static_cast<int>(ss.getProperty("sw", 0)) != 0;
                SequencerCommandApi::dispatch(engine, c, nowUs);

                // P-locks CC des sous-pas.
                for (int cci = 0; cci < ss.getNumChildren(); ++cci) {
                    juce::ValueTree ccn = ss.getChild(cci);
                    if (ccn.getType().toString() != "CC") continue;
                    const uint8_t slot = static_cast<uint8_t>(static_cast<int>(ccn.getProperty("k", 0)));
                    const uint8_t ccNb = static_cast<uint8_t>(static_cast<int>(ccn.getProperty("c", 0)));
                    const uint8_t vvv  = static_cast<uint8_t>(static_cast<int>(ccn.getProperty("v", 0)));
                    c    = SequencerCommand{};
                    c.id = SequencerCommandId::SetSubStepCCLock;
                    c.a  = newIdx;
                    c.b  = sj;
                    c.c  = slot;
                    c.d  = ccNb;
                    c.e  = vvv;
                    SequencerCommandApi::dispatch(engine, c, nowUs);
                }
            }
        }
    }

    juce::ValueTree rows = node.getChildWithName("Rows");
    if (rows.isValid()) {
        for (int ri = 0; ri < rows.getNumChildren(); ++ri) {
            juce::ValueTree row = rows.getChild(ri);
            if (row.getType().toString() != "R")
                continue;
            const uint8_t r = static_cast<uint8_t>(static_cast<int>(row.getProperty("i", 0)));
            c    = SequencerCommand{};
            c.id = SequencerCommandId::SetRowChannel;
            c.a  = r;
            c.b  = static_cast<uint8_t>(static_cast<int>(row.getProperty("ch", 0)));
            SequencerCommandApi::dispatch(engine, c, nowUs);

            // Champs per-row ajoutes en V1 (polyrythmie, CC-track, harmony mode, mute).
            if (row.hasProperty("ns")) {
                c    = SequencerCommand{};
                c.id = SequencerCommandId::SetRowSteps;
                c.a  = r;
                c.b  = static_cast<uint8_t>(static_cast<int>(row.getProperty("ns", 16)));
                SequencerCommandApi::dispatch(engine, c, nowUs);
            }
            if (row.hasProperty("k")) {
                c    = SequencerCommand{};
                c.id = SequencerCommandId::SetRowKind;
                c.a  = r;
                c.b  = static_cast<uint8_t>(static_cast<int>(row.getProperty("k", 0)));
                SequencerCommandApi::dispatch(engine, c, nowUs);
            }
            if (row.hasProperty("cc")) {
                c    = SequencerCommand{};
                c.id = SequencerCommandId::SetRowCCNumber;
                c.a  = r;
                c.b  = static_cast<uint8_t>(static_cast<int>(row.getProperty("cc", 74)));
                SequencerCommandApi::dispatch(engine, c, nowUs);
            }
            // Mode harmonique PAR MESURE (hm0..hm3). Ancien projet (clé "hm" unique, sans hm0)
            // => migration : appliqué à toutes les mesures via la sentinelle kRowModeAllBars.
            if (row.hasProperty("hm0")) {
                for (int bar = 0; bar < kMaxBars; ++bar) {
                    c    = SequencerCommand{};
                    c.id = SequencerCommandId::SetRowHarmonyMode;
                    c.a  = r;
                    c.b  = static_cast<uint8_t>(static_cast<int>(row.getProperty("hm" + juce::String(bar), 1)));
                    c.f  = static_cast<uint8_t>(bar);
                    SequencerCommandApi::dispatch(engine, c, nowUs);
                }
            } else if (row.hasProperty("hm")) {
                c    = SequencerCommand{};
                c.id = SequencerCommandId::SetRowHarmonyMode;
                c.a  = r;
                c.b  = static_cast<uint8_t>(static_cast<int>(row.getProperty("hm", 1)));
                c.f  = kRowModeAllBars;   // migration ancien format : toutes les mesures
                SequencerCommandApi::dispatch(engine, c, nowUs);
            }
            if (row.hasProperty("mu")) {
                c    = SequencerCommand{};
                c.id = SequencerCommandId::SetRowMuted;
                c.a  = r;
                c.x  = static_cast<int>(row.getProperty("mu", 0)) != 0;
                SequencerCommandApi::dispatch(engine, c, nowUs);
            }

            for (int sj = 0; sj < row.getNumChildren(); ++sj) {
                juce::ValueTree st = row.getChild(sj);
                if (st.getType().toString() != "S")
                    continue;
                const uint8_t s = static_cast<uint8_t>(static_cast<int>(st.getProperty("j", 0)));
                // Mesure du pas : "b" absent => mesure 0 (rétrocompat mono-mesure).
                const uint8_t bar = static_cast<uint8_t>(juce::jlimit(0, static_cast<int>(numBars) - 1,
                                                                      static_cast<int>(st.getProperty("b", 0))));
                c    = SequencerCommand{};
                c.id = SequencerCommandId::SetStep;
                c.a  = r;
                c.b  = s;
                c.c  = static_cast<uint8_t>(static_cast<int>(st.getProperty("n", 60)));
                c.d  = static_cast<uint8_t>(static_cast<int>(st.getProperty("v", 100)));
                c.e  = static_cast<uint8_t>(static_cast<int>(st.getProperty("g", 80)));
                c.f  = bar;
                SequencerCommandApi::dispatch(engine, c, nowUs);
                if (static_cast<int>(st.getProperty("on", 0)) == 0) {
                    c    = SequencerCommand{};
                    c.id = SequencerCommandId::ToggleStep;
                    c.a  = r;
                    c.b  = s;
                    c.f  = bar;
                    SequencerCommandApi::dispatch(engine, c, nowUs);
                }
                uint8_t subOld = static_cast<uint8_t>(static_cast<int>(st.getProperty("sub", kNoSubPattern)));
                uint8_t subNew = kNoSubPattern;
                if (subOld < kMaxSubPatterns)
                    subNew = subMap[subOld];
                if (subNew != kNoSubPattern) {
                    c    = SequencerCommand{};
                    c.id = SequencerCommandId::SetStepSubPattern;
                    c.a  = r;
                    c.b  = s;
                    c.c  = subNew;
                    c.f  = bar;
                    SequencerCommandApi::dispatch(engine, c, nowUs);
                }
                // Span (note longue / sub étalé). Absent => 1 (rétrocompat ancien projet).
                const int span = juce::jlimit(1, 64, static_cast<int>(st.getProperty("span", 1)));
                if (span > 1) {
                    c    = SequencerCommand{};
                    c.id = SequencerCommandId::SetStepSpan;
                    c.a  = r;
                    c.b  = s;
                    c.c  = static_cast<uint8_t>(span);
                    c.f  = bar;
                    SequencerCommandApi::dispatch(engine, c, nowUs);
                }
                c    = SequencerCommand{};
                c.id = SequencerCommandId::SetStepAccent;
                c.a  = r;
                c.b  = s;
                c.f  = bar;
                c.x  = static_cast<int>(st.getProperty("ac", 0)) != 0;
                SequencerCommandApi::dispatch(engine, c, nowUs);
                c    = SequencerCommand{};
                c.id = SequencerCommandId::SetStepSwing;
                c.a  = r;
                c.b  = s;
                c.f  = bar;
                c.x  = static_cast<int>(st.getProperty("sw", 0)) != 0;
                SequencerCommandApi::dispatch(engine, c, nowUs);

                // P-locks CC : un enfant <CC k="..." c="..." v="..."/> par slot actif.
                for (int cci = 0; cci < st.getNumChildren(); ++cci) {
                    juce::ValueTree ccn = st.getChild(cci);
                    if (ccn.getType().toString() != "CC") continue;
                    const uint8_t slot = static_cast<uint8_t>(static_cast<int>(ccn.getProperty("k", 0)));
                    const uint8_t ccNb = static_cast<uint8_t>(static_cast<int>(ccn.getProperty("c", 0)));
                    const uint8_t vvv  = static_cast<uint8_t>(static_cast<int>(ccn.getProperty("v", 0)));
                    c    = SequencerCommand{};
                    c.id = SequencerCommandId::SetStepCCLock;
                    c.a  = r;
                    c.b  = s;
                    c.c  = slot;
                    c.d  = ccNb;
                    c.e  = vvv;
                    c.f  = bar;
                    SequencerCommandApi::dispatch(engine, c, nowUs);
                }
            }
        }
    }
}

}  // namespace

namespace PatternValueTree {

juce::Identifier rootId() {
    static const juce::Identifier id("NidmiSeq");
    return id;
}

juce::ValueTree buildFromEngine(const SequencerEngine& engine) {
    juce::ValueTree root(rootId());
    root.setProperty("v", 2, nullptr);   // v2 = banque multi-pattern + chaîne (song mode)

    // --- Champs GLOBAUX (transport + tonalité maître + état de la banque) ---
    root.setProperty("bpm", engine.bpm(), nullptr);
    root.setProperty("loop", engine.isLooping() ? 1 : 0, nullptr);
    root.setProperty("mroot", engine.projectSettings().masterRootPc, nullptr);
    root.setProperty("mscale", engine.projectSettings().masterScaleId, nullptr);
    root.setProperty("active", engine.activePatternIndex(), nullptr);
    root.setProperty("song", engine.songMode() ? 1 : 0, nullptr);

    // --- Banque : on n'écrit que les patterns « utiles » (contenu, actif, ou
    //     cité par la chaîne) ; les slots vierges restent implicites. ---------
    const ChainProgram& chain = engine.chainProgram();
    bool referencedByChain[kMaxPatterns] = { false };
    for (uint8_t i = 0; i < chain.len && i < kMaxChainSlots; ++i)
        if (chain.slots[i].op == ChainOp::PlayPattern && chain.slots[i].param1 < kMaxPatterns)
            referencedByChain[chain.slots[i].param1] = true;

    juce::ValueTree bank("Bank");
    for (uint8_t i = 0; i < kMaxPatterns; ++i) {
        const Pattern& p = engine.bankPattern(i);
        if (i == engine.activePatternIndex() || referencedByChain[i] || patternHasContent(p))
            bank.appendChild(buildPatternNode(p, i), nullptr);
    }
    root.appendChild(bank, nullptr);

    // --- Chaîne (song mode) -------------------------------------------------
    juce::ValueTree chainVt("Chain");
    chainVt.setProperty("len", chain.len, nullptr);
    for (uint8_t i = 0; i < chain.len && i < kMaxChainSlots; ++i) {
        juce::ValueTree cs("C");
        cs.setProperty("i",  i, nullptr);
        cs.setProperty("op", static_cast<int>(chain.slots[i].op), nullptr);
        cs.setProperty("p1", chain.slots[i].param1, nullptr);
        cs.setProperty("p2", chain.slots[i].param2, nullptr);
        chainVt.appendChild(cs, nullptr);
    }
    root.appendChild(chainVt, nullptr);

    return root;
}

void applyToEngine(SequencerEngine& engine, const juce::ValueTree& rootIn, int64_t nowUs) {
    if (!rootIn.isValid() || rootIn.getType() != rootId())
        return;

    SequencerCommand c;
    c.id = SequencerCommandId::Stop;
    SequencerCommandApi::dispatch(engine, c, nowUs);

    juce::ValueTree bank = rootIn.getChildWithName("Bank");

    // --- Ancien format v1 : la racine EST le pattern unique (chargé en slot 0). ---
    if (!bank.isValid()) {
        c    = SequencerCommand{};
        c.id = SequencerCommandId::SelectPattern;
        c.a  = 0;
        SequencerCommandApi::dispatch(engine, c, nowUs);
        applyPatternNode(engine, rootIn, nowUs);
        // Pas de chaîne ni de song mode dans l'ancien format.
        c    = SequencerCommand{};
        c.id = SequencerCommandId::SetSongMode;
        c.x  = false;
        SequencerCommandApi::dispatch(engine, c, nowUs);
        c    = SequencerCommand{};
        c.id = SequencerCommandId::ChainProgramReset;
        SequencerCommandApi::dispatch(engine, c, nowUs);
        return;
    }

    // --- Format v2 : champs globaux, banque, chaîne. ---
    if (rootIn.hasProperty("bpm")) {
        c    = SequencerCommand{};
        c.id  = SequencerCommandId::SetBpm;
        c.f32 = static_cast<float>(rootIn.getProperty("bpm", 120.0f));
        SequencerCommandApi::dispatch(engine, c, nowUs);
    }
    if (rootIn.hasProperty("loop")) {
        c    = SequencerCommand{};
        c.id = SequencerCommandId::SetLoop;
        c.x  = static_cast<int>(rootIn.getProperty("loop", 1)) != 0;
        SequencerCommandApi::dispatch(engine, c, nowUs);
    }
    if (rootIn.hasProperty("mroot")) {
        c    = SequencerCommand{};
        c.id = SequencerCommandId::SetProjectMasterRootPc;
        c.a  = static_cast<uint8_t>(static_cast<int>(rootIn.getProperty("mroot", 0)));
        SequencerCommandApi::dispatch(engine, c, nowUs);
    }
    if (rootIn.hasProperty("mscale")) {
        c    = SequencerCommand{};
        c.id = SequencerCommandId::SetProjectMasterScaleId;
        c.a  = static_cast<uint8_t>(static_cast<int>(rootIn.getProperty("mscale", 0)));
        SequencerCommandApi::dispatch(engine, c, nowUs);
    }

    // Chaque pattern de la banque : on bascule l'actif dessus, puis on le remplit.
    // selectPattern recopie l'actif courant dans son slot avant de charger le suivant,
    // ce qui distribue correctement les contenus dans bank_[].
    for (int i = 0; i < bank.getNumChildren(); ++i) {
        juce::ValueTree pnode = bank.getChild(i);
        if (pnode.getType().toString() != "P")
            continue;
        const int idx = juce::jlimit(0, static_cast<int>(kMaxPatterns) - 1,
                                     static_cast<int>(pnode.getProperty("i", 0)));
        c    = SequencerCommand{};
        c.id = SequencerCommandId::SelectPattern;
        c.a  = static_cast<uint8_t>(idx);
        SequencerCommandApi::dispatch(engine, c, nowUs);
        applyPatternNode(engine, pnode, nowUs);
    }

    // Chaîne (song mode) : reset puis réécriture des slots.
    c    = SequencerCommand{};
    c.id = SequencerCommandId::ChainProgramReset;
    SequencerCommandApi::dispatch(engine, c, nowUs);
    juce::ValueTree chainVt = rootIn.getChildWithName("Chain");
    if (chainVt.isValid()) {
        for (int i = 0; i < chainVt.getNumChildren(); ++i) {
            juce::ValueTree cs = chainVt.getChild(i);
            if (cs.getType().toString() != "C") continue;
            c    = SequencerCommand{};
            c.id = SequencerCommandId::SetChainSlot;
            c.a  = static_cast<uint8_t>(static_cast<int>(cs.getProperty("i",  0)));
            c.b  = static_cast<uint8_t>(static_cast<int>(cs.getProperty("op", 0)));
            c.c  = static_cast<uint8_t>(static_cast<int>(cs.getProperty("p1", 0)));
            c.d  = static_cast<uint8_t>(static_cast<int>(cs.getProperty("p2", 0)));
            SequencerCommandApi::dispatch(engine, c, nowUs);
        }
        c    = SequencerCommand{};
        c.id = SequencerCommandId::SetChainLen;
        c.a  = static_cast<uint8_t>(static_cast<int>(chainVt.getProperty("len", 0)));
        SequencerCommandApi::dispatch(engine, c, nowUs);
    }

    c    = SequencerCommand{};
    c.id = SequencerCommandId::SetSongMode;
    c.x  = static_cast<int>(rootIn.getProperty("song", 0)) != 0;
    SequencerCommandApi::dispatch(engine, c, nowUs);

    // Restaure le pattern ACTIF en dernier (selectPattern stashe le dernier rempli).
    c    = SequencerCommand{};
    c.id = SequencerCommandId::SelectPattern;
    c.a  = static_cast<uint8_t>(juce::jlimit(0, static_cast<int>(kMaxPatterns) - 1,
                                             static_cast<int>(rootIn.getProperty("active", 0))));
    SequencerCommandApi::dispatch(engine, c, nowUs);
}

}  // namespace PatternValueTree
