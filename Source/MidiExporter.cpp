#include "MidiExporter.h"

#include "PatternValueTree.h"

#include <nidmi_seq/SeqEvent.h>
#include <nidmi_seq/SequencerEngine.h>
#include <nidmi_seq/StepTypes.h>

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <cmath>

namespace MidiExporter {

namespace {

constexpr int     kPpq             = 480;
constexpr int64_t kTickStepUs      = 250;     // 0,25 ms — au-dessus de la résolution du driver live
constexpr int     kSafetyBarsCap   = 64;      // garde-fou : on n'exporte jamais plus que ça
constexpr int     kMinBars         = 1;

constexpr juce::uint8 kMetaSequencerSpecific = 0x7F;

void appendVarLen(juce::MemoryBlock& dest, juce::uint32 value) {
    juce::uint8 buf[5];
    int         n      = 0;
    buf[n++]           = static_cast<juce::uint8>(value & 0x7F);
    value >>= 7;
    while (value > 0 && n < 5) {
        buf[n++] = static_cast<juce::uint8>((value & 0x7F) | 0x80);
        value >>= 7;
    }
    // buf est en ordre inverse (LSB en premier), on l'écrit dans le bon ordre.
    for (int i = n - 1; i >= 0; --i)
        dest.append(&buf[i], 1);
}

juce::MidiMessage makeSequencerSpecificMeta(const void* data, size_t size) {
    juce::MemoryBlock raw;
    const juce::uint8 prefix[2] = {0xFF, kMetaSequencerSpecific};
    raw.append(prefix, 2);
    appendVarLen(raw, static_cast<juce::uint32>(size));
    if (size > 0 && data != nullptr)
        raw.append(data, size);
    return juce::MidiMessage(raw.getData(), static_cast<int>(raw.getSize()));
}

void copyMasterSettings(const SequencerEngine& src, SequencerEngine& dst) {
    const auto& ps = src.projectSettings();
    dst.setProjectMasterBpm(ps.masterBpm);
    dst.setProjectMasterRootPc(ps.masterRootPc);
    dst.setProjectMasterScaleId(ps.masterScaleId);
}

int computeNumBars(const SequencerEngine& engine) {
    const auto& prog = engine.pattern().chordProgression;
    if (prog.len == 0)
        return kMinBars;
    int sum = 0;
    for (juce::uint8 i = 0; i < prog.len; ++i)
        sum += juce::jmax<int>(1, prog.slots[i].durationSlots);
    return juce::jlimit(kMinBars, kSafetyBarsCap, sum);
}

double ticksFromUs(int64_t us, double usPerQuarter) {
    return (static_cast<double>(us) / usPerQuarter) * static_cast<double>(kPpq);
}

void collectEvent(const SeqEvent& e, int64_t nowUs, double usPerQuarter,
                  juce::Array<juce::MidiMessageSequence>& trackByRow,
                  const juce::uint8 channelByRow[kMaxRows],
                  juce::uint8 rowCount) {
    // On retrouve la row par appariement canal (les rows n'apparaissent pas dans SeqEvent).
    int rowIdx = -1;
    for (juce::uint8 r = 0; r < rowCount; ++r) {
        if (channelByRow[r] == e.channel) { rowIdx = r; break; }
    }
    if (rowIdx < 0)
        rowIdx = 0;  // events sans row identifiable (macros, etc.) → track 0

    const double tick = ticksFromUs(nowUs, usPerQuarter);
    // SeqEvent.channel : déjà 1..16 (engine émet `row.channel + 1`).
    const int    juceChannel = juce::jlimit(1, 16, static_cast<int>(e.channel));

    juce::MidiMessage msg;
    switch (e.type) {
        case SeqEventType::NoteOn:
            msg = juce::MidiMessage::noteOn(juceChannel, e.data1,
                                            static_cast<juce::uint8>(e.data2));
            break;
        case SeqEventType::NoteOff:
            msg = juce::MidiMessage::noteOff(juceChannel, e.data1);
            break;
        case SeqEventType::CC:
            msg = juce::MidiMessage::controllerEvent(juceChannel, e.data1, e.data2);
            break;
        case SeqEventType::ProgramChange:
            msg = juce::MidiMessage::programChange(juceChannel, e.data1);
            break;
        case SeqEventType::PitchBend: {
            const int value = (e.data2 << 7) | e.data1;  // LSB|MSB → 14-bit
            msg = juce::MidiMessage::pitchWheel(juceChannel, value);
            break;
        }
        case SeqEventType::PolyPressure:
            msg = juce::MidiMessage::aftertouchChange(juceChannel, e.data1, e.data2);
            break;
        case SeqEventType::PatternLooped:
        case SeqEventType::SubPatternEnded:
        case SeqEventType::Stopped:
        default:
            return;  // évènements de contrôle non MIDI
    }
    msg.setTimeStamp(tick);
    trackByRow.getReference(rowIdx).addEvent(msg);
}

juce::MidiMessageSequence buildMetaTrack(const SequencerEngine& live, int numBars, Mode mode,
                                          double usPerQuarter) {
    juce::MidiMessageSequence meta;

    // Tempo
    meta.addEvent(juce::MidiMessage::tempoMetaEvent(static_cast<int>(std::lround(usPerQuarter)))
                      .withTimeStamp(0.0));

    // Time signature : la lib JUCE attend un dénominateur en puissance de 2.
    const auto& pat = live.pattern();
    meta.addEvent(
        juce::MidiMessage::timeSignatureMetaEvent(pat.numerator, pat.denominator).withTimeStamp(0.0));

    // Signature NiDMI (Track Name 0x03)
    meta.addEvent(juce::MidiMessage::textMetaEvent(0x03, "NiDMI Seq V1").withTimeStamp(0.0));

    if (mode == Mode::Full) {
        // Sérialise le ValueTree en binaire et l'embarque en Sequencer-Specific Meta.
        const auto tree = PatternValueTree::buildFromEngine(live);
        juce::MemoryOutputStream out;
        tree.writeToStream(out);
        const auto blob = out.getMemoryBlock();
        if (blob.getSize() > 0) {
            auto msg = makeSequencerSpecificMeta(blob.getData(), blob.getSize());
            msg.setTimeStamp(0.0);
            meta.addEvent(msg);
        }
    }

    // End-of-track implicite : la fin "musicale" est à numBars * ppq * numerator/denominator.
    // JUCE ajoute lui-même un End-of-track lors de l'écriture si nécessaire — pas besoin ici.
    juce::ignoreUnused(numBars);
    return meta;
}

void renderOffline(SequencerEngine& engine, int numBars, double usPerQuarter,
                   juce::Array<juce::MidiMessageSequence>& trackByRow,
                   const juce::uint8 channelByRow[kMaxRows], juce::uint8 rowCount) {
    // Durée totale en µs : bar duration × numBars.
    engine.play(0);
    const int64_t barUs = engine.barDurationUs();
    const int64_t endUs = barUs * numBars;

    // Le premier tick a déjà été appelé par play(0) ; on draine et on avance.
    auto drain = [&](int64_t nowUs) {
        const auto& q = engine.events();
        for (juce::uint8 i = 0; i < q.count; ++i)
            collectEvent(q.buf[i], nowUs, usPerQuarter, trackByRow, channelByRow, rowCount);
        engine.clearEvents();
    };

    drain(0);

    for (int64_t now = kTickStepUs; now <= endUs; now += kTickStepUs) {
        engine.tick(now);
        engine.pollNoteOffs(now);
        drain(now);
    }

    // Flush : stop() émet les NoteOff résiduels.
    engine.stop();
    drain(endUs);
}

}  // namespace

Result exportToFile(const SequencerEngine& live, const juce::File& destFile, Mode mode) {
    // 1. Clone l'état dans un engine offline.
    SequencerEngine offline;
    {
        const auto tree = PatternValueTree::buildFromEngine(live);
        PatternValueTree::applyToEngine(offline, tree, 0);
    }
    copyMasterSettings(live, offline);

    const auto& pat = offline.pattern();
    const juce::uint8 rowCount = juce::jlimit<juce::uint8>(1, kMaxRows, pat.numRows);

    juce::uint8 channelByRow[kMaxRows] = {0};
    juce::Array<juce::MidiMessageSequence> trackByRow;
    trackByRow.ensureStorageAllocated(rowCount);
    for (juce::uint8 r = 0; r < rowCount; ++r) {
        channelByRow[r] = pat.rows[r].channel;
        trackByRow.add(juce::MidiMessageSequence{});
    }

    const int numBars = computeNumBars(offline);
    const double usPerQuarter = (offline.bpm() > 0.0f) ? (60'000'000.0 / offline.bpm())
                                                       : 500'000.0;

    renderOffline(offline, numBars, usPerQuarter, trackByRow, channelByRow, rowCount);

    // Tri + résolution des paires NoteOn/NoteOff
    for (auto& seq : trackByRow) {
        seq.sort();
        seq.updateMatchedPairs();
    }

    juce::MidiFile mf;
    mf.setTicksPerQuarterNote(kPpq);

    mf.addTrack(buildMetaTrack(live, numBars, mode, usPerQuarter));
    for (auto& seq : trackByRow)
        mf.addTrack(seq);

    if (!destFile.getParentDirectory().exists())
        destFile.getParentDirectory().createDirectory();

    juce::FileOutputStream fos(destFile);
    if (!fos.openedOk())
        return {false, "Impossible d'ouvrir " + destFile.getFullPathName()};
    fos.setPosition(0);
    fos.truncate();
    if (!mf.writeTo(fos))
        return {false, "Échec d'écriture MidiFile"};

    return {true, destFile.getFullPathName()};
}

}  // namespace MidiExporter
