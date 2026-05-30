#pragma once

#include <cstdint>

namespace juce {
class AudioPlayHead;
}

struct HostTimeSnapshot {
    int64_t nowUs     = 0;
    bool    isPlaying = false;
    bool    hasTime   = false;
    float   hostBpm   = 0.0f;
    bool    hasBpm    = false;
};

class HostTimeMapper {
public:
    void resetTransportEdges();

    HostTimeSnapshot snapshotFromPlayHead(juce::AudioPlayHead* head, double sampleRate) const;

    bool wasPlaying() const { return wasPlaying_; }
    void setWasPlaying(bool v) { wasPlaying_ = v; }

private:
    bool wasPlaying_ = false;
};
