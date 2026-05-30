#pragma once

#include <cstdint>

#include <juce_audio_basics/juce_audio_basics.h>

/// Interprète Start / Continue / Stop / Timing Clock (24 ppqn) pour le mode « transport MIDI ».
/// Le temps moteur suit l’horloge audio entre deux tops MIDI ; le BPM est estimé à partir des intervalles.
class MidiClockTransport {
public:
    void prepare(double sampleRate);
    void reset();

    enum class StepKind : uint8_t { Stop = 0, Play = 1, Continue = 2 };

    struct Step {
        StepKind kind = StepKind::Stop;
        int64_t  nowUs = 0;
    };

    static constexpr int kMaxSteps = 24;

    struct Result {
        Step    steps[kMaxSteps];
        uint8_t numSteps = 0;
        float   bpmFromClock = -1.f;  ///< ≥ 0 si une nouvelle estimation BPM est disponible
    };

    /// À appeler avant de vider le MidiBuffer ; `blockStartGlobalSample` = compteur d’échantillons absolu.
    Result processIncoming(const juce::MidiBuffer& midi,
                           int                   numSamples,
                           int64_t               blockStartGlobalSample);

    bool    isPlaying() const { return playing_; }
    int64_t nowUsAtGlobalSample(int64_t globalSample) const;

private:
    void pushStep(Result& r, StepKind k, int64_t nowUs);

    double  sampleRate_            = 44100.0;
    bool    playing_               = false;
    int64_t anchorGlobalSample_    = 0;
    int64_t lastClockGlobalSample_ = -1;
    float   intervalEmaSamples_    = -1.f;

    static constexpr float kEmaAlpha = 0.12f;
};
