#include "PluginProcessor.h"
#include "DeviceProfile.h"
#include "PatternValueTree.h"
#include "PluginEditor.h"

#include <nidmi_seq/SeqEvent.h>
#include <nidmi_seq/SequencerCommandApi.h>

#include <cmath>
#include <vector>

namespace {
constexpr const char* kIdFollowHost     = "followHost";
constexpr const char* kIdUseHostBpm     = "useHostBpm";
constexpr const char* kIdUseMidiClock   = "useMidiClock";
constexpr const char* kIdBpm            = "bpm";
constexpr const char* kIdLoop           = "loop";
constexpr const char* kIdNumSteps       = "numSteps";
constexpr const char* kIdNumRows        = "numRows";
constexpr const char* kIdTsNum          = "tsNum";
constexpr const char* kIdTsDen          = "tsDen";
constexpr const char* kIdMasterRoot     = "masterRoot";       // 0..11 : C, C#, ..., B
constexpr const char* kIdMasterScaleIdx = "masterScale";      // index dans la liste des gammes

constexpr uint8_t kTsDenFromChoiceIndex[] = {1, 2, 4, 8, 16};

// Noms des pitches pour le choix de tonalite (aligne sur pitch-class 0..11).
static const juce::StringArray kRootNames{"C", "C#", "D", "D#", "E", "F",
                                          "F#", "G", "G#", "A", "A#", "B"};

// Noms alignes sur scalebank::ScaleId (cf. ScaleBank.h).
static const juce::StringArray kScaleNames{
    "Major", "Natural Minor", "Harmonic Minor", "Melodic Minor",
    "Dorian", "Phrygian", "Lydian", "Mixolydian",
    "Pentatonic Major", "Pentatonic Minor", "Blues", "Chromatic"
};
}  // namespace

juce::AudioProcessorValueTreeState::ParameterLayout NidmiSeqAudioProcessor::createParameterLayout() {
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{kIdFollowHost, 1}, "Follow host transport", true));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{kIdUseHostBpm, 1}, "Use host BPM", true));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{kIdUseMidiClock, 1}, "MIDI clock transport", false));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{kIdBpm, 1}, "BPM",
        juce::NormalisableRange<float>{20.0f, 300.0f, 0.1f, 0.3f}, 120.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{kIdLoop, 1}, "Loop", true));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{kIdNumSteps, 1}, "Steps", 1, 64, 16));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{kIdNumRows, 1}, "Rows", 1, 16, 4));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{kIdTsNum, 1}, "Time sig num", 1, 16, 4));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{kIdTsDen, 1}, "Time sig den",
        juce::StringArray{"1", "2", "4", "8", "16"}, 2));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{kIdMasterRoot, 1}, "Master root", kRootNames, 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{kIdMasterScaleIdx, 1}, "Master scale", kScaleNames, 0));
    return {params.begin(), params.end()};
}

NidmiSeqAudioProcessor::NidmiSeqAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    , apvts_(*this, nullptr, "PARAMS", createParameterLayout())
    , controller_(*this) {
    // Indispensable : un tableau de std::atomic est INDETERMINE a la
    // construction. Sans cet appel, processBlock lirait des cibles au hasard et
    // remapperait des CC des le premier bloc.
    rebuildLearnMap();
}

NidmiSeqAudioProcessor::~NidmiSeqAudioProcessor() = default;

void NidmiSeqAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    juce::ignoreUnused(samplesPerBlock);
    sampleRate_        = sampleRate;
    internalTimeUs_    = 0;
    wasHostPlaying_    = false;
    lastFollowHost_    = true;
    lastUseHostBpm_    = true;
    lastUseMidiClock_  = false;
    lastBpmParam_      = -1.0f;
    lastLoop_          = -1;
    lastNumSteps_      = -1;
    lastNumRows_       = -1;
    lastTsNum_         = -1;
    lastTsDenIdx_      = -1;
    lastMasterRootIdx_   = -1;
    lastMasterScaleIdx_  = -1;
    lastHostBpmSynced_ = -1.0f;
    globalSamples_     = 0;
    lastMidiTickUs_    = 0;
    hostTime_.resetTransportEdges();
    midiClock_.prepare(sampleRate);
    midiClock_.reset();
}

void NidmiSeqAudioProcessor::releaseResources() {}

bool NidmiSeqAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    return true;
}

int64_t NidmiSeqAudioProcessor::approximateNowUs() const {
    return static_cast<int64_t>(juce::Time::getMillisecondCounterHiRes() * 1000.0);
}

bool NidmiSeqAudioProcessor::pushCommand(const SequencerCommand& cmd) {
    return commandFifo_.push(cmd);
}

void NidmiSeqAudioProcessor::applyPatternTree(const juce::ValueTree& tree) {
    suspendProcessing(true);
    PatternValueTree::applyToEngine(engine_, tree, approximateNowUs());
    clock_.reset(approximateNowUs());
    suspendProcessing(false);
}

void NidmiSeqAudioProcessor::syncParametersToEngine(int64_t nowUs) {
    const bool useMidiClock =
        apvts_.getRawParameterValue(kIdUseMidiClock) != nullptr &&
        apvts_.getRawParameterValue(kIdUseMidiClock)->load() > 0.5f;
    const bool followHost =
        apvts_.getRawParameterValue(kIdFollowHost) != nullptr &&
        apvts_.getRawParameterValue(kIdFollowHost)->load() > 0.5f;
    const bool useHostBpm =
        apvts_.getRawParameterValue(kIdUseHostBpm) != nullptr &&
        apvts_.getRawParameterValue(kIdUseHostBpm)->load() > 0.5f;
    // En standalone il n'y a pas d'hôte : le BPM hôte est inopérant, donc le BPM manuel
    // (GLOBAL) doit piloter le moteur. (Hors standalone, followHost gère déjà le suivi.)
    const bool hostBpmActive = useHostBpm && !isStandaloneApp();
    const float bpm = apvts_.getRawParameterValue(kIdBpm) != nullptr
                          ? apvts_.getRawParameterValue(kIdBpm)->load()
                          : 120.0f;
    const bool loop =
        apvts_.getRawParameterValue(kIdLoop) != nullptr &&
        apvts_.getRawParameterValue(kIdLoop)->load() > 0.5f;
    const int steps = apvts_.getRawParameterValue(kIdNumSteps) != nullptr
                          ? static_cast<int>(apvts_.getRawParameterValue(kIdNumSteps)->load())
                          : 16;
    const int rows = apvts_.getRawParameterValue(kIdNumRows) != nullptr
                         ? static_cast<int>(apvts_.getRawParameterValue(kIdNumRows)->load())
                         : 4;
    const int tsNum = apvts_.getRawParameterValue(kIdTsNum) != nullptr
                          ? static_cast<int>(apvts_.getRawParameterValue(kIdTsNum)->load())
                          : 4;
    int tsDenIdx = 2;
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(apvts_.getParameter(kIdTsDen)))
        tsDenIdx = p->getIndex();

    if (followHost != lastFollowHost_) {
        lastFollowHost_ = followHost;
        hostTime_.resetTransportEdges();
        wasHostPlaying_  = false;
        internalTimeUs_  = 0;
        clock_.reset(nowUs);
    }

    SequencerCommand c;

    // « numSteps » global RETIRE du modele d'usage : le nb de pas par mesure est le N PAR ROW
    // (polyrythmie) ou son defaut derive de la signature. On ne pousse plus SetSteps (qui
    // ecraserait toutes les rows) depuis ce parametre legacy. (void) pour eviter un warning.
    (void) steps;
    if (rows != lastNumRows_) {
        lastNumRows_ = rows;
        c.id         = SequencerCommandId::SetNumRows;
        c.a          = static_cast<uint8_t>(juce::jlimit(1, 16, rows));
        SequencerCommandApi::dispatch(engine_, c, nowUs);
        clock_.reset(nowUs);
    }
    if ((loop ? 1 : 0) != lastLoop_) {
        lastLoop_ = loop ? 1 : 0;
        c.id      = SequencerCommandId::SetLoop;
        c.x       = loop;
        SequencerCommandApi::dispatch(engine_, c, nowUs);
    }

    if (!hostBpmActive && !useMidiClock && std::abs(bpm - lastBpmParam_) > 0.01f) {
        lastBpmParam_ = bpm;
        c.id          = SequencerCommandId::SetBpm;
        c.f32         = bpm;
        SequencerCommandApi::dispatch(engine_, c, nowUs);
        clock_.reset(nowUs);
    }

    if (tsNum != lastTsNum_ || tsDenIdx != lastTsDenIdx_) {
        lastTsNum_    = tsNum;
        lastTsDenIdx_ = tsDenIdx;
        const int di  = juce::jlimit(0, 4, tsDenIdx);
        c.id          = SequencerCommandId::SetTimeSignature;
        c.a           = static_cast<uint8_t>(juce::jlimit(1, 16, tsNum));
        c.b           = kTsDenFromChoiceIndex[static_cast<size_t>(di)];
        SequencerCommandApi::dispatch(engine_, c, nowUs);
        clock_.reset(nowUs);
    }

    // Master tonality (ProjectSettings).
    int masterRootIdx = 0;
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(apvts_.getParameter(kIdMasterRoot)))
        masterRootIdx = p->getIndex();
    int masterScaleIdx = 0;
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(apvts_.getParameter(kIdMasterScaleIdx)))
        masterScaleIdx = p->getIndex();

    if (masterRootIdx != lastMasterRootIdx_) {
        lastMasterRootIdx_ = masterRootIdx;
        c.id = SequencerCommandId::SetProjectMasterRootPc;
        c.a  = static_cast<uint8_t>(juce::jlimit(0, 11, masterRootIdx));
        SequencerCommandApi::dispatch(engine_, c, nowUs);
    }
    if (masterScaleIdx != lastMasterScaleIdx_) {
        lastMasterScaleIdx_ = masterScaleIdx;
        c.id = SequencerCommandId::SetProjectMasterScaleId;
        c.a  = static_cast<uint8_t>(juce::jlimit(0, 11, masterScaleIdx));
        SequencerCommandApi::dispatch(engine_, c, nowUs);
    }
}

void NidmiSeqAudioProcessor::drainSeqEventsToMidi(juce::MidiBuffer& midi, int sampleOffset) {
    const auto& q = engine_.events();
    for (uint8_t i = 0; i < q.count; ++i) {
        const auto& e = q.buf[i];
        // SeqEvent.channel : déjà 1..16 (l'engine émet `row.channel + 1`).
        const int ch = juce::jlimit(1, 16, static_cast<int>(e.channel));
        if (e.type == SeqEventType::NoteOn) {
            midi.addEvent(juce::MidiMessage::noteOn(ch, static_cast<int>(e.data1),
                                                    static_cast<juce::uint8>(e.data2)),
                          sampleOffset);
        } else if (e.type == SeqEventType::NoteOff) {
            midi.addEvent(juce::MidiMessage::noteOff(ch, static_cast<int>(e.data1)), sampleOffset);
        } else if (e.type == SeqEventType::CC) {
            midi.addEvent(juce::MidiMessage::controllerEvent(ch, static_cast<int>(e.data1),
                                                             static_cast<int>(e.data2)),
                          sampleOffset);
        }
    }
    engine_.clearEvents();
}

MidiExporter::Result NidmiSeqAudioProcessor::exportMidi(const juce::File& destFile,
                                                       MidiExporter::Mode mode) const {
    return MidiExporter::exportToFile(engine_, destFile, mode);
}

// Plafond de CC remappes par bloc. 32 couvre tout usage reel : a 64 echantillons
// de bloc, cela ferait plus de 20 000 CC par seconde sur un seul canal.
static constexpr int kMaxRemapPerBlock = 32;

void NidmiSeqAudioProcessor::setDeviceProfileIndex(int i) {
    deviceProfileIndex_ = i;
    rebuildLearnMap();
}

void NidmiSeqAudioProcessor::rebuildLearnMap() {
    int8_t tmp[128];
    std::fill(std::begin(tmp), std::end(tmp), static_cast<int8_t>(-1));

    const DeviceProfile& prof = DeviceProfile::byIndex(deviceProfileIndex_);

    // 1) IDENTITE par defaut : un CC que le profil connait traverse sans changer
    //    de numero. Sans cela, un controleur deja regle sur le bon CC serait
    //    ignore, puisque processBlock vide tout l'entrant.
    //    Seuls les CC declares passent : le plugin ne devient pas un MIDI thru.
    for (const auto& p : prof.params())
        if (p.cc >= 0 && p.cc < 128)
            tmp[p.cc] = static_cast<int8_t>(p.cc);

    // 2) Remappages explicites, appliques APRES : un learn l'emporte sur
    //    l'identite. Si un learn vise un numero qui est deja le CC propre d'un
    //    autre parametre, il le detourne — c'est voulu, l'explicite gagne.
    for (const auto& p : prof.params())
        if (p.learn >= 0 && p.learn < 128 && p.cc >= 0 && p.cc < 128)
            tmp[p.learn] = static_cast<int8_t>(p.cc);

    for (int i = 0; i < 128; ++i)
        learnMap_[i].store(tmp[i], std::memory_order_relaxed);
}

void NidmiSeqAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                         juce::MidiBuffer& midiMessages) {
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();

    const bool useMidiClock =
        apvts_.getRawParameterValue(kIdUseMidiClock) != nullptr &&
        apvts_.getRawParameterValue(kIdUseMidiClock)->load() > 0.5f;
    const bool followHostParam =
        apvts_.getRawParameterValue(kIdFollowHost) != nullptr &&
        apvts_.getRawParameterValue(kIdFollowHost)->load() > 0.5f;
    const bool followHost =
        followHostParam && !isStandaloneApp() && !useMidiClock;
    const bool useHostBpm =
        apvts_.getRawParameterValue(kIdUseHostBpm) != nullptr &&
        apvts_.getRawParameterValue(kIdUseHostBpm)->load() > 0.5f;

    if (useMidiClock != lastUseMidiClock_) {
        lastUseMidiClock_ = useMidiClock;
        hostTime_.resetTransportEdges();
        wasHostPlaying_  = false;
        internalTimeUs_  = 0;
        globalSamples_     = 0;
        lastMidiTickUs_  = 0;
        midiClock_.reset();
        clock_.reset(0);
    }

    const int64_t blockStartGlobal = globalSamples_;

    MidiClockTransport::Result midiR;
    if (useMidiClock)
        midiR = midiClock_.processIncoming(midiMessages, numSamples, blockStartGlobal);

    // « Learn » : un CC entrant est remappe vers le CC que le profil associe au
    // parametre. Sur un synthe sans sortie MIDI — le Kobol — c'est le seul sens
    // possible du MIDI learn : lier un potard de notre controleur.
    //
    // Capture AVANT le clear, qui jette tout l'entrant, et reinjection apres.
    // Tableau sur la pile : aucune allocation sur le thread audio. Au-dela de
    // kMaxRemapPerBlock le surplus est ignore plutot que de deborder — un bloc
    // qui porterait autant de CC est deja pathologique.
    struct Remap { int sample; uint8_t channel, cc, value; };
    Remap     remaps[kMaxRemapPerBlock];
    int       numRemaps = 0;

    // Lecture des OCTETS BRUTS, sans construire de juce::MidiMessage :
    // getMessage() alloue pour un SysEx, et le thread audio ne doit jamais
    // allouer. Un Control Change fait exactement 3 octets, statut 0xBn.
    for (const auto meta : midiMessages) {
        if (meta.numBytes != 3)
            continue;
        const auto* d = meta.data;
        if ((d[0] & 0xF0) != 0xB0)
            continue;
        const int src = d[1] & 0x7F;
        const int dst = learnMap_[static_cast<size_t>(src)].load(std::memory_order_relaxed);
        if (dst < 0)
            continue;
        if (numRemaps >= kMaxRemapPerBlock)
            break;
        remaps[numRemaps++] = { meta.samplePosition,
                                static_cast<uint8_t>((d[0] & 0x0F) + 1),   // canal 1..16
                                static_cast<uint8_t>(dst),
                                static_cast<uint8_t>(d[2] & 0x7F) };
    }

    midiMessages.clear();
    buffer.clear();

    for (int i = 0; i < numRemaps; ++i)
        midiMessages.addEvent(juce::MidiMessage::controllerEvent(remaps[i].channel,
                                                                 remaps[i].cc,
                                                                 remaps[i].value),
                              remaps[i].sample);

    const int64_t blockUs =
        sampleRate_ > 0.0 ? static_cast<int64_t>(static_cast<double>(numSamples) * 1.0e6 / sampleRate_)
                          : 0;

    HostTimeSnapshot snap;
    if (followHost)
        snap = hostTime_.snapshotFromPlayHead(getPlayHead(), sampleRate_);

    int64_t cmdTimeUs  = 0;
    int64_t tickTimeUs = 0;

    if (useMidiClock) {
        SequencerCommand c;
        for (uint8_t i = 0; i < midiR.numSteps; ++i) {
            const auto& st = midiR.steps[i];
            if (st.kind == MidiClockTransport::StepKind::Stop) {
                c.id = SequencerCommandId::Stop;
                SequencerCommandApi::dispatch(engine_, c, st.nowUs);
                clock_.reset(st.nowUs);
            } else if (st.kind == MidiClockTransport::StepKind::Play) {
                c.id = SequencerCommandId::Play;
                SequencerCommandApi::dispatch(engine_, c, st.nowUs);
                clock_.reset(st.nowUs);
            } else if (st.kind == MidiClockTransport::StepKind::Continue) {
                c.id = SequencerCommandId::Play;
                SequencerCommandApi::dispatch(engine_, c, st.nowUs);
                clock_.reset(st.nowUs);
            }
        }

        const int64_t blockEndGlobal = blockStartGlobal + static_cast<int64_t>(numSamples);
        if (midiClock_.isPlaying()) {
            tickTimeUs       = midiClock_.nowUsAtGlobalSample(blockEndGlobal);
            lastMidiTickUs_  = tickTimeUs;
        } else {
            tickTimeUs = lastMidiTickUs_;
        }

        cmdTimeUs = tickTimeUs - blockUs;
        if (cmdTimeUs < 0)
            cmdTimeUs = 0;

        if (midiR.bpmFromClock >= 0.f) {
            c.id  = SequencerCommandId::SetBpm;
            c.f32 = midiR.bpmFromClock;
            SequencerCommandApi::dispatch(engine_, c, tickTimeUs);
            clock_.reset(tickTimeUs);
        }
    } else if (followHost && snap.hasTime) {
        const double startSec = static_cast<double>(snap.nowUs) * 1.0e-6;
        const double endSec   = startSec + static_cast<double>(numSamples) / sampleRate_;
        tickTimeUs            = static_cast<int64_t>(endSec * 1.0e6);
        internalTimeUs_       = tickTimeUs;
        cmdTimeUs             = tickTimeUs - blockUs;
    } else if (followHost) {
        cmdTimeUs  = internalTimeUs_;
        internalTimeUs_ += blockUs;
        tickTimeUs = internalTimeUs_;
    } else {
        // Base de temps interne MONOTONE : ne jamais remettre internalTimeUs_ à 0
        // sur une transition STOPPED->PLAYING. La commande Play est drainée plus bas
        // à cmdTimeUs et ancre barStartUs_ = cmdTimeUs ; un reset ici rendrait
        // barElapsed = nowUs - barStartUs_ négatif au bloc suivant (BUG : dernier pas figé un tour).
        cmdTimeUs = internalTimeUs_;
        if (engine_.state() == SequencerEngine::State::PLAYING)
            internalTimeUs_ += blockUs;
        tickTimeUs = internalTimeUs_;
    }

    if (cmdTimeUs < 0)
        cmdTimeUs = 0;

    globalSamples_ += static_cast<int64_t>(numSamples);

    commandFifo_.drain(engine_, clock_, cmdTimeUs, [] {});

    syncParametersToEngine(tickTimeUs);

    if (!useMidiClock && followHostParam && !isStandaloneApp()) {
        if (snap.hasTime && useHostBpm && snap.hasBpm &&
            std::abs(snap.hostBpm - lastHostBpmSynced_) > 0.01f) {
            lastHostBpmSynced_ = snap.hostBpm;
            SequencerCommand c;
            c.id  = SequencerCommandId::SetBpm;
            c.f32 = snap.hostBpm;
            SequencerCommandApi::dispatch(engine_, c, tickTimeUs);
            clock_.reset(tickTimeUs);
        }

        const bool hostPlaying = snap.isPlaying;
        if (!wasHostPlaying_ && hostPlaying) {
            SequencerCommand c;
            c.id = SequencerCommandId::Play;
            SequencerCommandApi::dispatch(engine_, c, tickTimeUs);
            clock_.reset(tickTimeUs);
        } else if (wasHostPlaying_ && !hostPlaying) {
            SequencerCommand c;
            c.id = SequencerCommandId::Stop;
            SequencerCommandApi::dispatch(engine_, c, tickTimeUs);
            clock_.reset(tickTimeUs);
        }
        wasHostPlaying_ = hostPlaying;
    }

    clock_.tick(engine_, tickTimeUs);
    drainSeqEventsToMidi(midiMessages, 0);
}

juce::AudioProcessorEditor* NidmiSeqAudioProcessor::createEditor() {
    return new NidmiSeqAudioProcessorEditor(*this);
}

void NidmiSeqAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    juce::ValueTree root("NidmiRoot");
    // Le profil est stocke par NOM et non par index : ajouter un profil au
    // registre ne doit pas renommer les CC d'un projet deja enregistre.
    root.setProperty("deviceProfile",
                     DeviceProfile::byIndex(deviceProfileIndex_).name(), nullptr);
    root.appendChild(apvts_.copyState(), nullptr);
    // Le pattern actif fait foi (bank_[active] peut être périmé entre deux switches) :
    // on le resynchronise dans la banque avant de tout sérialiser.
    engine_.syncActiveToBank();
    root.appendChild(PatternValueTree::buildFromEngine(engine_), nullptr);
    if (auto xml = root.createXml())
        copyXmlToBinary(*xml, destData);
}

void NidmiSeqAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr)
        return;
    juce::ValueTree root = juce::ValueTree::fromXml(*xml);
    if (!root.isValid())
        return;

    if (root.hasProperty("deviceProfile")) {
        deviceProfileIndex_ =
            DeviceProfile::indexOfName(root.getProperty("deviceProfile").toString());
        rebuildLearnMap();
    }

    for (int i = 0; i < root.getNumChildren(); ++i) {
        juce::ValueTree c = root.getChild(i);
        if (c.getType() == juce::Identifier("PARAMETERS"))
            apvts_.replaceState(c);
        else if (c.getType() == PatternValueTree::rootId())
            PatternValueTree::applyToEngine(engine_, c, approximateNowUs());
    }
    clock_.reset(approximateNowUs());
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new NidmiSeqAudioProcessor();
}
