#ifndef MAG_FRAME_HPP
#define MAG_FRAME_HPP

#include <cstdint>

namespace Mag {

/// Did the sensor layer resolve a driver for this type at all?
///
/// This is a separate question from whether anything arrives, and keeping them
/// apart is the whole reason this app has a taxonomy instead of a boolean.
/// SleepLab asked both of the platform and got two different answers from two
/// types that look identical in `SensorTypes.hpp`: `SPO2` resolved no driver at
/// all (ledger row S4), and `TOUCH_DETECT` resolved one and then delivered
/// nothing for a minute because it is an event sensor (row S12). Reading the
/// second as the first cost SleepLab every night it would ever have recorded.
enum class Resolve : uint8_t {
    NotAsked = 0,  ///< connect() has not been called yet.
    NoProducer,    ///< connect() returned false. There is nothing to subscribe to.
    Resolved,      ///< A driver handle came back.
};

/// Given a driver, is it speaking?
enum class Delivery : uint8_t {
    Unknown = 0,   ///< No driver, so the question does not apply.
    Silent,        ///< Resolved, and not one sample has ever arrived.
    Delivering,    ///< A sample arrived recently.
    Stalled,       ///< Samples arrived once and then stopped.
};

/// The frame's width, which for a parser-less type has to be discovered.
///
/// `MAGNETIC_FIELD` ships no parser, so there is no `isDataValid()` to ask and
/// no documented field count to compare against. Three axes is the hypothesis;
/// anything else is the finding.
enum class FrameShape : uint8_t {
    Unknown = 0,   ///< No frame seen yet.
    Empty,         ///< A batch arrived carrying no fields.
    TooNarrow,     ///< Fewer than three fields. Not a 3-axis reading.
    ThreeAxis,     ///< Exactly three, as a magnetometer should be.
    Wider,         ///< More than three. Extra fields, recorded not discarded.
};

inline FrameShape classifyFields(uint16_t fieldCount)
{
    if (fieldCount == 0) {
        return FrameShape::Empty;
    }
    if (fieldCount < 3) {
        return FrameShape::TooNarrow;
    }
    if (fieldCount == 3) {
        return FrameShape::ThreeAxis;
    }
    return FrameShape::Wider;
}

/// `staleAfterMs` is a liveness threshold, not a sample period. It has to be
/// well above the delivery interval, and the delivery interval here is not
/// known: the sensor layer aggregates on a ~1 s timer that no app-side setting
/// moves (RustGuiPoc's finding), and the accelerometer ignores the period it is
/// asked for outright (SleepLab row S3). So this distinguishes "stopped" from
/// "slow", and nothing finer.
inline Delivery classifyDelivery(Resolve  resolve,
                                 uint32_t samples,
                                 uint32_t ageMs,
                                 uint32_t staleAfterMs)
{
    if (resolve != Resolve::Resolved) {
        return Delivery::Unknown;
    }
    if (samples == 0) {
        return Delivery::Silent;
    }
    return (ageMs <= staleAfterMs) ? Delivery::Delivering : Delivery::Stalled;
}

inline const char* name(Resolve r)
{
    switch (r) {
        case Resolve::NotAsked:   return "NOT ASKED";
        case Resolve::NoProducer: return "NO PRODUCER";
        case Resolve::Resolved:   return "RESOLVED";
    }
    return "NOT ASKED";
}

inline const char* name(Delivery d)
{
    switch (d) {
        case Delivery::Unknown:    return "-";
        case Delivery::Silent:     return "SILENT";
        case Delivery::Delivering: return "LIVE";
        case Delivery::Stalled:    return "STALLED";
    }
    return "-";
}

inline const char* name(FrameShape s)
{
    switch (s) {
        case FrameShape::Unknown:   return "NO FRAME";
        case FrameShape::Empty:     return "EMPTY";
        case FrameShape::TooNarrow: return "TOO NARROW";
        case FrameShape::ThreeAxis: return "3 AXIS";
        case FrameShape::Wider:     return "WIDER";
    }
    return "NO FRAME";
}

/// The verdict this app exists to produce, split into a headline and its
/// reason.
///
/// Split because the headline has to be readable at arm's length on a round
/// 240 px display and the reason does not. One combined string cannot be both:
/// "NO COMPASS: NO PRODUCER" is 274 px wide at double size, which is wider than
/// the display and far wider than the circle at the row it lands on.
struct Verdict {
    const char* headline;  ///< Short enough for double size. The answer.
    const char* reason;    ///< Why. Single size.
};

/// Ordered so that the first thing that is wrong is the thing reported, because
/// a heading computed from a frame that never arrived is worse than no heading.
inline Verdict verdict(Resolve r, Delivery d, FrameShape s)
{
    if (r == Resolve::NotAsked) {
        return Verdict{"PENDING", "NOT ASKED YET"};
    }
    if (r == Resolve::NoProducer) {
        return Verdict{"NO COMPASS", "NO PRODUCER"};
    }
    if (d == Delivery::Silent) {
        return Verdict{"NO COMPASS", "RESOLVED BUT SILENT"};
    }
    if (d == Delivery::Stalled) {
        return Verdict{"STALLED", "WAS DELIVERING"};
    }
    if (s == FrameShape::Empty || s == FrameShape::TooNarrow) {
        return Verdict{"NO COMPASS", "FRAME TOO NARROW"};
    }
    return Verdict{"DELIVERING", "FIELD IS ARRIVING"};
}

} // namespace Mag

#endif // MAG_FRAME_HPP
