#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <nidmi_seq/SequencerCommandApi.h>

#include <juce_audio_basics/juce_audio_basics.h>

class SequencerEngine;
class SequencerClockDriver;

class CommandFifo {
public:
    static constexpr int kCapacity = 128;

    bool push(const SequencerCommand& cmd);

    void drain(SequencerEngine& engine,
               SequencerClockDriver& clock,
               int64_t nowUs,
               const std::function<void()>& onTransportLikeCommand);

private:
    juce::AbstractFifo fifo { kCapacity };
    std::array<SequencerCommand, static_cast<size_t>(kCapacity)> buf {};
};
