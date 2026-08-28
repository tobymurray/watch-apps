/**
 ******************************************************************************
 * @file    Commands.hpp
 * @date    28-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   The messages between the Service and the GUI.
 ******************************************************************************
 *
 * The kernel's largest message-pool block is 256 bytes. A message that
 * overflows it is not a compile error by itself: it silently falls out of the
 * pool at runtime. So every type here is `#pragma pack`ed and carries a
 * `static_assert`, which is the only thing that catches it. That limit and that
 * mitigation are SleepLab's, established on hardware.
 *
 * Only the Service can reach a sensor: `SDK::Sensor::Connection` works from the
 * Service half alone, because of what `SDK/Kernel/Kernel.hpp` hands a GUI
 * process. So every number on the screen travels across this boundary, and the
 * buttons travel back the other way.
 *
 * The GUI's incoming queue is ten deep and discards the oldest when it is full
 * (SensorLab Docs/FINDINGS.md section 12). One status message per sample would
 * overrun that: the sensor layer aggregates on a ~1 s timer but delivers a
 * whole batch at once, and the accelerometer ignores the period it is asked for
 * and runs at about 48 Hz regardless (SleepLab ledger row S3). So the Service
 * aggregates and sends on a timer, and this file carries no per-sample type.
 */

#ifndef COMMANDS_HPP
#define COMMANDS_HPP

#include "SDK/Messages/MessageBase.hpp"
#include "SDK/Messages/MessageTypes.hpp"

#include <cstdint>

#pragma pack(push, 4)

namespace CustomMessage {

/// Service --> GUI, on a timer.
constexpr SDK::MessageType::Type MAG_STATUS = 0x00000001;

/// Service --> GUI, when the calibration state changes.
constexpr SDK::MessageType::Type MAG_CALIBRATION = 0x00000002;

/// GUI --> Service, on a button.
constexpr SDK::MessageType::Type MAG_CONTROL = 0x00000003;

/// The kernel's largest pool block. Nothing here may exceed it.
constexpr size_t kMaxMessageBytes = 256;

/// Bit flags in `MagStatus::flags`.
namespace StatusFlag {
constexpr uint8_t kHeadingValid = 0x01;  ///< The heading and dip are meaningful.
constexpr uint8_t kLevelled     = 0x02;  ///< The device was plausibly at rest.
constexpr uint8_t kAccelFresh   = 0x04;  ///< An accelerometer sample is recent.
constexpr uint8_t kCalibrating  = 0x08;  ///< A hard-iron sweep is running.
constexpr uint8_t kSawNonFinite = 0x10;  ///< At least one field was NaN or inf.
} // namespace StatusFlag

/**
 * @brief Service --> GUI. Everything the verdict, frame and compass screens draw.
 *
 * Floats rather than fixed-point integers: the concern with floats on this
 * platform is that the watch's newlib may not link floating-point `printf`, and
 * that is a formatting problem rather than an arithmetic one. Formatting goes
 * through `Fmt.hpp`, which is integer-only, so a float can cross this boundary
 * safely and arrive without having lost a digit to a scale factor chosen here.
 */
struct MagStatus : public SDK::MessageBase {
    uint32_t samples;   ///< Magnetometer samples received in total.
    uint32_t batches;   ///< EVENT_SENSOR_LAYER_DATA batches for this handle.
    uint32_t ageMs;     ///< Since the newest magnetometer sample.
    uint32_t uptimeMs;  ///< For the liveness marker.

    uint32_t rawBits0;  ///< First field read as a u32, not reinterpreted.
    int32_t  rawInt0;   ///< The same field read as an i32.

    float rawX;         ///< As delivered, no offset removed.
    float rawY;
    float rawZ;
    float magnitude;    ///< Of the corrected vector when calibrated, else raw.
    float spread;       ///< (max-min)/mean of the magnitude, or negative.
    float headingDeg;
    float dipDeg;

    uint16_t fieldCount;  ///< Discovered, because there is no parser to ask.
    uint16_t stride;      ///< As the batch reported it.

    uint8_t resolve;       ///< Mag::Resolve
    uint8_t delivery;      ///< Mag::Delivery
    uint8_t shape;         ///< Mag::FrameShape
    uint8_t units;         ///< Mag::Units
    uint8_t accelResolve;  ///< Mag::Resolve, for the accelerometer
    uint8_t calQuality;    ///< Mag::HardIron::Quality
    uint8_t flags;         ///< StatusFlag bits
    uint8_t reserved;

    MagStatus()
        : SDK::MessageBase(MAG_STATUS)
        , samples(0)
        , batches(0)
        , ageMs(0)
        , uptimeMs(0)
        , rawBits0(0)
        , rawInt0(0)
        , rawX(0.0f)
        , rawY(0.0f)
        , rawZ(0.0f)
        , magnitude(0.0f)
        , spread(-1.0f)
        , headingDeg(0.0f)
        , dipDeg(0.0f)
        , fieldCount(0)
        , stride(0)
        , resolve(0)
        , delivery(0)
        , shape(0)
        , units(0)
        , accelResolve(0)
        , calQuality(0)
        , flags(0)
        , reserved(0)
    {}
};
static_assert(sizeof(MagStatus) <= kMaxMessageBytes,
              "MagStatus exceeds the kernel's largest pool block, and that "
              "failure is silent at runtime");

/**
 * @brief Service --> GUI. The hard-iron sweep, sent when it changes.
 *
 * Separate from the status because it changes on a different clock: a sweep runs
 * for tens of seconds and then holds still, while the status repaints
 * continuously. Sending the spans in every status frame would cost bytes in the
 * message that is sent most often.
 */
struct MagCalibration : public SDK::MessageBase {
    uint32_t samples;
    float    offsetX;
    float    offsetY;
    float    offsetZ;
    float    spanX;
    float    spanY;
    float    spanZ;
    uint8_t  quality;  ///< Mag::HardIron::Quality
    uint8_t  reserved[3];

    MagCalibration()
        : SDK::MessageBase(MAG_CALIBRATION)
        , samples(0)
        , offsetX(0.0f)
        , offsetY(0.0f)
        , offsetZ(0.0f)
        , spanX(0.0f)
        , spanY(0.0f)
        , spanZ(0.0f)
        , quality(0)
        , reserved{0, 0, 0}
    {}
};
static_assert(sizeof(MagCalibration) <= kMaxMessageBytes,
              "MagCalibration exceeds the kernel's largest pool block");

/// What the GUI can ask the Service to do.
enum class Action : uint8_t {
    None = 0,
    CalibrationStart,
    CalibrationStop,
    CalibrationReset,
    /// Flip the assumed accelerometer sign convention. Which way the vector
    /// points at rest is not written down for this watch, and one press with the
    /// watch flat on a table settles it without a rebuild.
    ToggleUpConvention,
};

/// GUI --> Service.
struct MagControl : public SDK::MessageBase {
    uint8_t action;  ///< Action
    uint8_t reserved[3];

    MagControl()
        : SDK::MessageBase(MAG_CONTROL)
        , action(static_cast<uint8_t>(Action::None))
        , reserved{0, 0, 0}
    {}

    explicit MagControl(Action a)
        : MagControl()
    {
        action = static_cast<uint8_t>(a);
    }
};
static_assert(sizeof(MagControl) <= kMaxMessageBytes,
              "MagControl exceeds the kernel's largest pool block");

} // namespace CustomMessage

#pragma pack(pop)

#endif // COMMANDS_HPP
