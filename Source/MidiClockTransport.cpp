#include "MidiClockTransport.h"

#include <algorithm>

void MidiClockTransport::prepare(double sampleRate) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
}

void MidiClockTransport::reset() {
    playing_               = false;
    anchorGlobalSample_    = 0;
    lastClockGlobalSample_ = -1;
    intervalEmaSamples_    = -1.f;
}

void MidiClockTransport::pushStep(Result& r, const StepKind k, const int64_t nowUs) {
    if (r.numSteps >= kMaxSteps)
        return;
    r.steps[r.numSteps++] = Step{k, nowUs};
}

MidiClockTransport::Result MidiClockTransport::processIncoming(const juce::MidiBuffer& midi,
                                                               const int numSamples,
                                                               const int64_t blockStartGlobalSample) {
    Result r;
    for (const auto metadata : midi) {
        const auto msg = metadata.getMessage();
        const int  pos = metadata.samplePosition;
        if (pos < 0 || pos >= numSamples)
            continue;

        const int64_t absS = blockStartGlobalSample + static_cast<int64_t>(pos);

        if (msg.isMidiClock()) {
            if (playing_ && lastClockGlobalSample_ >= 0) {
                const float ds = static_cast<float>(absS - lastClockGlobalSample_);
                if (ds > 1.f) {
                    if (intervalEmaSamples_ < 0.f)
                        intervalEmaSamples_ = ds;
                    else
                        intervalEmaSamples_ =
                            intervalEmaSamples_ * (1.f - kEmaAlpha) + ds * kEmaAlpha;

                    const double spq = static_cast<double>(intervalEmaSamples_) * 24.0;
                    if (spq > 1.0) {
                        float bpm = static_cast<float>(sampleRate_ * 60.0 / spq);
                        if (bpm >= 20.f && bpm <= 300.f)
                            r.bpmFromClock = bpm;
                    }
                }
            }
            lastClockGlobalSample_ = absS;
        } else if (msg.isMidiStart()) {
            anchorGlobalSample_    = absS;
            playing_               = true;
            lastClockGlobalSample_ = -1;
            intervalEmaSamples_    = -1.f;
            pushStep(r, StepKind::Play, 0);
        } else if (msg.isMidiContinue()) {
            const int64_t tUs = static_cast<int64_t>(
                static_cast<double>(absS - anchorGlobalSample_) * 1.0e6 / sampleRate_);
            playing_               = true;
            lastClockGlobalSample_ = -1;
            pushStep(r, StepKind::Continue, std::max<int64_t>(0, tUs));
        } else if (msg.isMidiStop()) {
            const int64_t tUs = static_cast<int64_t>(
                static_cast<double>(absS - anchorGlobalSample_) * 1.0e6 / sampleRate_);
            pushStep(r, StepKind::Stop, std::max<int64_t>(0, tUs));
            playing_               = false;
            lastClockGlobalSample_ = -1;
        }
    }
    return r;
}

int64_t MidiClockTransport::nowUsAtGlobalSample(const int64_t globalSample) const {
    if (!playing_ || sampleRate_ <= 0.0)
        return 0;
    const double d = static_cast<double>(globalSample - anchorGlobalSample_);
    return static_cast<int64_t>(d * 1.0e6 / sampleRate_);
}
