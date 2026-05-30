#include "HostTimeMapper.h"

#include <juce_audio_basics/juce_audio_basics.h>

void HostTimeMapper::resetTransportEdges() {
    wasPlaying_ = false;
}

HostTimeSnapshot HostTimeMapper::snapshotFromPlayHead(juce::AudioPlayHead* head,
                                                      double sampleRate) const {
    HostTimeSnapshot out;
    juce::ignoreUnused(sampleRate);

    if (head == nullptr)
        return out;

    const auto pos = head->getPosition();
    if (!pos.hasValue())
        return out;

    out.isPlaying = pos->getIsPlaying();

    if (auto t = pos->getTimeInSeconds(); t.hasValue()) {
        out.hasTime = true;
        out.nowUs   = static_cast<int64_t>(*t * 1.0e6);
    }

    if (auto bpm = pos->getBpm(); bpm.hasValue()) {
        out.hasBpm  = true;
        out.hostBpm = static_cast<float>(*bpm);
    }

    return out;
}
