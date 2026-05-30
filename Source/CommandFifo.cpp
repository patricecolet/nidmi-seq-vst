#include "CommandFifo.h"

#include <nidmi_seq/SequencerClockDriver.h>
#include <nidmi_seq/SequencerEngine.h>

bool CommandFifo::push(const SequencerCommand& cmd) {
    if (cmd.i8ptr != nullptr)
        return false;

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    fifo.prepareToWrite(1, start1, size1, start2, size2);
    if (size1 + size2 < 1)
        return false;
    const int idx = size1 > 0 ? start1 : start2;
    buf[static_cast<size_t>(idx)] = cmd;
    fifo.finishedWrite(1);
    return true;
}

void CommandFifo::drain(SequencerEngine& engine,
                        SequencerClockDriver& clock,
                        int64_t nowUs,
                        const std::function<void()>& onTransportLikeCommand) {
    for (;;) {
        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        fifo.prepareToRead(1, start1, size1, start2, size2);
        if (size1 + size2 < 1)
            break;
        const int idx = size1 > 0 ? start1 : start2;
        const SequencerCommand cmd = buf[static_cast<size_t>(idx)];
        fifo.finishedRead(1);

        SequencerCommandApi::dispatch(engine, cmd, nowUs);

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wswitch-enum"
#endif
        switch (cmd.id) {
            case SequencerCommandId::Play:
            case SequencerCommandId::Stop:
            case SequencerCommandId::Pause:
            case SequencerCommandId::TogglePlayPause:
            case SequencerCommandId::SetBpm:
            case SequencerCommandId::SetTimeSignature:
            case SequencerCommandId::SetSteps:
                clock.reset(nowUs);
                if (onTransportLikeCommand)
                    onTransportLikeCommand();
                break;
            default:
                break;
        }
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }
}
