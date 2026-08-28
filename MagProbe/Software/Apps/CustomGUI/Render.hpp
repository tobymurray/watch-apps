#ifndef RENDER_HPP
#define RENDER_HPP

#include "Canvas.hpp"

#include "Mag/Frame.hpp"
#include "Mag/HardIron.hpp"
#include "Mag/Units.hpp"
#include "Mag/Vec3.hpp"

#include <cstdint>

namespace Render {

/// Everything the screens draw, and nothing that only the service needs.
///
/// A plain struct with no kernel in it, so the renderer is a pure function of
/// this and can be driven from a host test. That matters more here than usual:
/// the simulator resolves no sensor drivers for a service, so a screen fed by
/// real samples can only ever be seen on the watch. This struct is the seam
/// where that stops being true.
struct View {
    // What the sensor layer said.
    Mag::Resolve    resolve{Mag::Resolve::NotAsked};
    Mag::Delivery   delivery{Mag::Delivery::Unknown};
    Mag::FrameShape shape{Mag::FrameShape::Unknown};
    uint16_t        fieldCount{0};
    uint16_t        stride{0};
    uint32_t        samples{0};
    uint32_t        batches{0};
    uint32_t        ageMs{0};

    // What the numbers look like.
    Mag::Units units{Mag::Units::Unknown};
    Mag::Vec3  raw{};
    Mag::Vec3  corrected{};
    float      magnitude{0.0f};
    float      spreadFraction{-1.0f};

    /// The first field's bit pattern read as an unsigned integer rather than as
    /// a float. The frame does not say which member of the union it is, and for
    /// a parser-less type nobody has established it, so both readings go on
    /// screen and the operator decides.
    uint32_t rawBits0{0};
    int32_t  rawInt0{0};

    // Calibration.
    Mag::HardIron::Quality calQuality{Mag::HardIron::Quality::Empty};
    uint32_t               calSamples{0};
    Mag::Vec3              calOffsets{};
    Mag::Vec3              calSpans{};
    bool                   calibrating{false};

    // Derived orientation.
    bool  haveHeading{false};
    float headingDeg{0.0f};
    float dipDeg{0.0f};
    bool  levelled{false};

    // Accelerometer, which tilt compensation cannot work without.
    Mag::Resolve accelResolve{Mag::Resolve::NotAsked};
    bool         accelFresh{false};
    Mag::Vec3    accel{};

    uint32_t uptimeMs{0};
    uint32_t frames{0};
};

enum class Screen : uint8_t {
    Verdict = 0,  ///< Does a compass work on this watch at all.
    Frame,        ///< What arrived, and what the bytes could mean.
    Compass,      ///< The needle, once there is something to point.
    Calibration,  ///< Hard iron: sweep it, and see whether the sweep was good.
    Count,
};

constexpr uint8_t kScreenCount = static_cast<uint8_t>(Screen::Count);

/// Draw one screen. Clears the canvas first, so a caller never has to.
void render(Canvas& canvas, Screen screen, const View& view);

/// Screen name, for the header and for a test to key on.
const char* name(Screen screen);

} // namespace Render

#endif // RENDER_HPP
