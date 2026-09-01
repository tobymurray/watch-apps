/**
 ******************************************************************************
 * @file    Commands.hpp
 * @brief   The whole Service <-> GUI protocol for a stationary ride.
 *
 * Six messages, because a ride that measures nothing but time and heart rate
 * has six things to say. The GUI owns no timer and no sensor: it draws the
 * last snapshot it was handed and sends button presses back, so the number on
 * the screen and the number in the FIT file can never be two different
 * measurements of the same second.
 ******************************************************************************
 */

#ifndef COMMANDS_HPP
#define COMMANDS_HPP

#include <cstring>

#include "SDK/Messages/MessageBase.hpp"
#include "SDK/Messages/MessageTypes.hpp"
#include "SDK/Kernel/Kernel.hpp"

#include "Track.hpp"

// Force 4-byte alignment for all message structures
#pragma pack(push, 4)

namespace CustomMessage {

    // Service --> GUI
    constexpr SDK::MessageType::Type TRACK_STATE_UPDATE = 0x00000001;
    constexpr SDK::MessageType::Type TRACK_DATA_UPDATE  = 0x00000002;
    constexpr SDK::MessageType::Type ACCESSORY_STATUS   = 0x00000003;
    constexpr SDK::MessageType::Type RIDE_SAVED         = 0x00000004;
    constexpr SDK::MessageType::Type RIDE_CONFIG        = 0x00000005;

    // GUI --> Service
    constexpr SDK::MessageType::Type TRACK_START        = 0x00000010;
    constexpr SDK::MessageType::Type TRACK_STOP         = 0x00000011;
    constexpr SDK::MessageType::Type TRACK_PAUSE        = 0x00000012;
    constexpr SDK::MessageType::Type TRACK_RESUME       = 0x00000013;

    // -- Service --> GUI ------------------------------------------------------

    struct TrackStateUpd : public SDK::MessageBase {
        Track::State state;
        TrackStateUpd()
            : SDK::MessageBase(TRACK_STATE_UPDATE)
            , state(Track::State::INACTIVE)
        {}
    };

    struct TrackDataUpd : public SDK::MessageBase {
        Track::Data data;
        TrackDataUpd()
            : SDK::MessageBase(TRACK_DATA_UPDATE)
            , data{}
        {}
    };

    /// External-accessory link status, forwarded from the kernel's
    /// EVENT_ACCESSORY_STATUS. This is the strap's BLE link, not the heart rate
    /// itself — a connected strap that has not produced a beat yet is still
    /// CONNECTED here, which is exactly what the pre-ride screen wants to say.
    struct AccessoryStatusUpd : public SDK::MessageBase {
        uint8_t state;     ///< SDK::Accessory::State
        char    name[24];  ///< device name (may be empty)
        AccessoryStatusUpd()
            : SDK::MessageBase(ACCESSORY_STATUS)
            , state(0)
            , name{}
        {}
    };

    /// Sent once, after the FIT file is durably closed. `ok` is the durability
    /// contract from ActivityWriter::stop(), so the GUI can tell the wearer the
    /// ride was saved without having to believe it on the Service's word alone.
    struct RideSaved : public SDK::MessageBase {
        std::time_t duration;   ///< active seconds
        float       avgHr;      ///< bpm over the ride (0 when never measured)
        float       calories;   ///< kcal, active; the GUI converts for display
        bool        ok;         ///< the .fit is on disk and registered
        /// The wearer threw the ride away. A different thing from `!ok`, which
        /// is a ride that was meant to be kept and could not be written -- the
        /// screen must not apologise for something that was asked for.
        bool        discarded;
        RideSaved()
            : SDK::MessageBase(RIDE_SAVED)
            , duration(0)
            , avgHr(0.0f)
            , calories(0.0f)
            , ok(false)
            , discarded(false)
        {}
    };

    /// The parts of the app's configuration the screen has to show. Sent when
    /// the GUI starts and again whenever the values are re-read, so a change
    /// made on the phone shows up without reinstalling anything.
    ///
    /// Only the target is here. Auto-lap and the backlight setting change what
    /// the watch *does*, not what it draws, so sending them would be a field
    /// the GUI never reads.
    struct RideConfigUpd : public SDK::MessageBase {
        uint16_t targetMinutes;        ///< 0 = no target
        bool     energyInKilojoules;   ///< display unit only; the file is kcal
        RideConfigUpd()
            : SDK::MessageBase(RIDE_CONFIG)
            , targetMinutes(0)
            , energyInKilojoules(false)
        {}
    };

    // -- GUI --> Service ------------------------------------------------------

    struct TrackStart : public SDK::MessageBase {
        TrackStart() : SDK::MessageBase(TRACK_START) {}
    };

    struct TrackStop : public SDK::MessageBase {
        bool discard;   ///< true: throw the ride away; false: save it
        TrackStop()
            : SDK::MessageBase(TRACK_STOP)
            , discard(false)
        {}
    };

    struct TrackPause : public SDK::MessageBase {
        TrackPause() : SDK::MessageBase(TRACK_PAUSE) {}
    };

    struct TrackResume : public SDK::MessageBase {
        TrackResume() : SDK::MessageBase(TRACK_RESUME) {}
    };

/// Allocate/send/release, so neither half ever writes that dance out by hand.
class Sender {
public:
    explicit Sender(const SDK::Kernel &kernel) : mKernel(kernel) {}
    virtual ~Sender() = default;

    // Service --> GUI
    bool trackState(Track::State state)
    {
        return send<TrackStateUpd>([&](TrackStateUpd *m) { m->state = state; });
    }

    bool trackData(const Track::Data &data)
    {
        return send<TrackDataUpd>([&](TrackDataUpd *m) { m->data = data; });
    }

    bool accessoryStatus(uint8_t state, const char *name)
    {
        return send<AccessoryStatusUpd>([&](AccessoryStatusUpd *m) {
            m->state = state;
            if (name) {
                std::strncpy(m->name, name, sizeof(m->name) - 1);
            }
        });
    }

    bool rideConfig(uint16_t targetMinutes, bool energyInKilojoules)
    {
        return send<RideConfigUpd>([&](RideConfigUpd *m) {
            m->targetMinutes      = targetMinutes;
            m->energyInKilojoules = energyInKilojoules;
        });
    }

    bool rideSaved(std::time_t duration, float avgHr, float calories, bool ok, bool discarded)
    {
        return send<RideSaved>([&](RideSaved *m) {
            m->duration  = duration;
            m->avgHr     = avgHr;
            m->calories  = calories;
            m->ok        = ok;
            m->discarded = discarded;
        });
    }

    // GUI --> Service
    bool trackStart()  { return send<TrackStart>([](TrackStart *) {}); }
    bool trackPause()  { return send<TrackPause>([](TrackPause *) {}); }
    bool trackResume() { return send<TrackResume>([](TrackResume *) {}); }

    bool trackStop(bool discard)
    {
        return send<TrackStop>([&](TrackStop *m) { m->discard = discard; });
    }

private:
    template <typename Msg, typename Fill>
    bool send(Fill fill)
    {
        auto *msg = mKernel.comm.allocateMessage<Msg>();
        if (!msg) {
            return false;
        }
        fill(msg);
        const bool status = mKernel.comm.sendMessage(msg);
        mKernel.comm.releaseMessage(msg);
        return status;
    }

    const SDK::Kernel &mKernel;
};

} // namespace CustomMessage

#pragma pack(pop)

#endif // COMMANDS_HPP
