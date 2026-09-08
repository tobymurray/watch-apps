#ifndef HAPTICLAB_COMMANDS_HPP
#define HAPTICLAB_COMMANDS_HPP

#include <cstdint>

#include "SDK/Messages/MessageBase.hpp"
#include "SDK/Messages/MessageTypes.hpp"

#pragma pack(push, 4)

/// The two halves' own messages. Only Phase 1 needs them: a GUI process cannot
/// reach a sensor, so the effect sweep runs in the Service, where the play
/// command and the accelerometer samples share one process and one clock. The
/// protocol and the probe are GUI-only and send nothing here.
namespace CustomMessage {

constexpr SDK::MessageType::Type SWEEP_START    = 0x00000101;
constexpr SDK::MessageType::Type SWEEP_STOP     = 0x00000102;
constexpr SDK::MessageType::Type SWEEP_PROGRESS = 0x00000103;

/// Where the sweep writes, and how much of the effect space it covers.
struct SweepStart : public SDK::MessageBase {
    char    runId[20];
    uint8_t firstEffect;
    uint8_t lastEffect;
    uint8_t repeats;

    SweepStart()
        : SDK::MessageBase(SWEEP_START)
        , runId {}
        , firstEffect(1)
        , lastEffect(127)
        , repeats(2)
    {}
};

struct SweepStop : public SDK::MessageBase {
    SweepStop()
        : SDK::MessageBase(SWEEP_STOP)
    {}
};

/// What the sweep screen shows. Sent once per effect rather than per sample:
/// the GUI's app-specific queue holds ten and discards the oldest when full.
struct SweepProgress : public SDK::MessageBase {
    uint16_t effect;
    uint16_t total;
    uint8_t  repeat;
    uint8_t  finished;
    /// Samples in the window just captured; zero means the sensor answered
    /// nothing, which is the difference between a quiet effect and no data.
    uint16_t samples;

    SweepProgress()
        : SDK::MessageBase(SWEEP_PROGRESS)
        , effect(0)
        , total(0)
        , repeat(0)
        , finished(0)
        , samples(0)
    {}
};

static_assert(sizeof(SweepStart) <= 256, "the largest IPC pool block is 256 bytes");
static_assert(sizeof(SweepProgress) <= 256, "the largest IPC pool block is 256 bytes");

} // namespace CustomMessage

#pragma pack(pop)

#endif // HAPTICLAB_COMMANDS_HPP
