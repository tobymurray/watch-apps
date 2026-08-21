/**
 ******************************************************************************
 * @file    SensorTypeTable.generated.hpp
 * @brief   GENERATED. Every sensor type the SDK declares, and its parser.
 ******************************************************************************
 *
 * Do not edit. Regenerate with:
 *
 *     UNA_SDK=/path/to/una-sdk python3 SensorLab/Tools/gen_catalogue.py \
 *         --out SensorLab/Software/Libs/Header/Catalogue/SensorTypeTable.generated.hpp
 *
 * Generated from:
 *   Libs/Header/SDK/SensorLayer/SensorTypes.hpp
 *   Libs/Header/SDK/SensorLayer/DataParsers/  (29 parsers)
 *
 * Why generated rather than typed: `Docs/SensorsLayer.md` is six types behind
 * `SensorTypes.hpp`, and an app that typed its own table would inherit
 * whichever of the two its author happened to read. See Tools/gen_catalogue.py.
 *
 * This file states what the SDK *claims*. It is the `expected` column of every
 * conformance row and never the measured one -- reading a header can refute a
 * claim about the device but can never confirm one.
 *
 ******************************************************************************
 */

#ifndef SENSORLAB_SENSORTYPETABLE_GENERATED_HPP
#define SENSORLAB_SENSORTYPETABLE_GENERATED_HPP

#include <cstddef>
#include <cstdint>

namespace SensorLab::Catalogue
{

/// Bumped by the generator whenever the emitted shape changes, so a profile
/// carries the table version that produced it.
constexpr uint32_t kTypeTableVersion = 1;

/// The SDK tree this table was generated from, for the run manifest.
constexpr char kGeneratedFromSdk[] = "kernel-interface-3";

/// Which union member of `SDK::Sensor::Data::Field` a field is read through.
///
/// Taken from the parser's own accessors, which is the only place in the SDK
/// that states a field's type unambiguously: the doc comments contradict each
/// other and `SensorsLayer.md` contradicts both. `Unread` means the shipped
/// parser declares the field and never reads it.
enum class FieldKind : uint8_t { Unread = 0, Float, U32, I32 };

/// How a parser's `isDataValid()` compares the delivered field count.
///
/// 28 of 29 parsers are `Exact`, so a single appended field silently
/// invalidates every sample. `HeartRateEx` is `AtLeast`, deliberately, so a
/// future kernel can extend the frame without breaking apps. That asymmetry is
/// a conformance finding in its own right -- see Docs/FINDINGS.md.
enum class Validity : uint8_t { Exact = 0, AtLeast };

struct FieldSpec
{
    const char *name;
    FieldKind   kind;
    /// The parser's doc comment for this field, verbatim. Quoted rather than
    /// paraphrased so the report can cite it.
    const char *doc;
};

struct ParserSpec
{
    /// C++ class name under `SDK::SensorDataParser`.
    const char      *cls;
    /// Header path, for `expected_source` citations.
    const char      *header;
    const FieldSpec *fields;
    uint8_t          fieldCount;
    Validity         validity;
    /// `isDataValid()` also range-checks at least one field value, so a frame
    /// with the right shape and an out-of-range value reads as invalid.
    bool             rangeChecked;
    /// `isDataValid()` dereferences a field *before* checking the field count.
    /// On a short frame that is an out-of-bounds read, because `DataView`'s
    /// bounds assert is compiled out at -Os. Only the profiler meets short
    /// frames, so only the profiler must not construct these parsers blind.
    bool             readsBeforeCount;
};

/// Index into `kParsers`, or `kNoParser`.
constexpr uint8_t kNoParser = 0xFF;

struct TypeSpec
{
    /// The enumerator name in `SensorTypes.hpp`, e.g. "ACCELEROMETER".
    const char *name;
    /// Its value, e.g. 0x10. This is what a claim_id is keyed on.
    uint32_t    value;
    /// The enum's own doc comment, verbatim.
    const char *doc;
    /// Index into `kParsers`, or `kNoParser` for the five types the SDK ships
    /// no parser for. For those, a measured frame layout is the only
    /// description of the frame that exists anywhere.
    uint8_t     parser;
    /// True when `Docs/SensorsLayer.md`'s table does not list this type at all.
    bool        missingFromDoc;
};


// Accelerometer -- SDK/SensorLayer/DataParsers/SensorDataParserAccelerometer.hpp
constexpr FieldSpec kFieldsAccelerometer[] = {
    { "X", FieldKind::Float, "X axis" },
    { "Y", FieldKind::Float, "Y axis" },
    { "Z", FieldKind::Float, "Z axis" },
};

// AccelerometerRaw -- SDK/SensorLayer/DataParsers/SensorDataParserAccelerometerRaw.hpp
constexpr FieldSpec kFieldsAccelerometerRaw[] = {
    { "X", FieldKind::I32, "X axis" },
    { "Y", FieldKind::I32, "Y axis" },
    { "Z", FieldKind::I32, "Z axis" },
};

// Activity -- SDK/SensorLayer/DataParsers/SensorDataParserActivity.hpp
constexpr FieldSpec kFieldsActivity[] = {
    { "DURATION", FieldKind::U32, "Activity duration in minutes (uint32_t)" },
};

// ActivityRecognition -- SDK/SensorLayer/DataParsers/SensorDataParserActivityRecognition.hpp
constexpr FieldSpec kFieldsActivityRecognition[] = {
    { "ID", FieldKind::U32, "Activity identifier (see @ref Activity)" },
    { "CONFIDENCE", FieldKind::U32, "Confidence in percent [0..100]" },
};

// Altimeter -- SDK/SensorLayer/DataParsers/SensorDataParserAltimeter.hpp
constexpr FieldSpec kFieldsAltimeter[] = {
    { "ALTITUDE", FieldKind::Float, "Altitude in meters" },
};

// BatteryCharging -- SDK/SensorLayer/DataParsers/SensorDataParserBatteryCharging.hpp
constexpr FieldSpec kFieldsBatteryCharging[] = {
    { "CONNECTED", FieldKind::U32, "USB cable connect status" },
    { "CHARGING", FieldKind::U32, "Charging status" },
};

// BatteryLevel -- SDK/SensorLayer/DataParsers/SensorDataParserBatteryLevel.hpp
constexpr FieldSpec kFieldsBatteryLevel[] = {
    { "LEVEL", FieldKind::Float, "Battery charge level in percent (float, 0..100)" },
};

// BatteryMetrics -- SDK/SensorLayer/DataParsers/SensorDataParserBatteryMetrics.hpp
constexpr FieldSpec kFieldsBatteryMetrics[] = {
    { "VOLTAGE", FieldKind::Float, "Battery voltage (V)" },
    { "CURRENT", FieldKind::Float, "Instantaneous current (mA); sign per firmware contract" },
    { "AVERAGE_CURRENT", FieldKind::Float, "Averaged/filtered current (mA)" },
    { "CAPACITY", FieldKind::Float, "Remaining capacity (mAh)" },
    { "DESIGN_CAPACITY", FieldKind::Float, "Full charge (design) capacity (mAh)" },
};

// FloorCounter -- SDK/SensorLayer/DataParsers/SensorDataParserFloorCounter.hpp
constexpr FieldSpec kFieldsFloorCounter[] = {
    { "FLOORS_UP", FieldKind::I32, "Floors up counter (int32_t)" },
    { "FLOORS_DOWN", FieldKind::I32, "Floors down counter (int32_t)" },
};

// Fusion -- SDK/SensorLayer/DataParsers/SensorDataParserFusion.hpp
constexpr FieldSpec kFieldsFusion[] = {
    { "ACCEL_X", FieldKind::Float, "Accelerometer X axis" },
    { "ACCEL_Y", FieldKind::Float, "Accelerometer Y axis" },
    { "ACCEL_Z", FieldKind::Float, "Accelerometer Z axis" },
    { "GYRO_X", FieldKind::Float, "Gyroscope X axis" },
    { "GYRO_Y", FieldKind::Float, "Gyroscope Y axis" },
    { "GYRO_Z", FieldKind::Float, "Gyroscope Z axis" },
};

// FusionRaw -- SDK/SensorLayer/DataParsers/SensorDataParserFusionRaw.hpp
constexpr FieldSpec kFieldsFusionRaw[] = {
    { "ACCEL_X", FieldKind::I32, "Accelerometer X axis" },
    { "ACCEL_Y", FieldKind::I32, "Accelerometer Y axis" },
    { "ACCEL_Z", FieldKind::I32, "Accelerometer Z axis" },
    { "GYRO_X", FieldKind::I32, "Gyroscope X axis" },
    { "GYRO_Y", FieldKind::I32, "Gyroscope Y axis" },
    { "GYRO_Z", FieldKind::I32, "Gyroscope Z axis" },
};

// GpsDistance -- SDK/SensorLayer/DataParsers/SensorDataParserGpsDistance.hpp
constexpr FieldSpec kFieldsGpsDistance[] = {
    { "DISTANCE", FieldKind::Float, "Distance, m(float)" },
};

// GpsLocation -- SDK/SensorLayer/DataParsers/SensorDataParserGpsLocation.hpp
constexpr FieldSpec kFieldsGpsLocation[] = {
    { "PRECISION", FieldKind::Float, "Precision (in meters)" },
    { "COORDS_VALID", FieldKind::U32, "Coordinates are valid" },
    { "LAT", FieldKind::Float, "Latitude,m (float)" },
    { "LON", FieldKind::Float, "Longitude,m (float)" },
    { "ALT", FieldKind::Float, "Altitude,m (float)" },
};

// GpsSpeed -- SDK/SensorLayer/DataParsers/SensorDataParserGpsSpeed.hpp
constexpr FieldSpec kFieldsGpsSpeed[] = {
    { "SPEED", FieldKind::Float, "Speed, m/s (float)" },
    { "SPEED_VALID", FieldKind::U32, "1 when the fix is current (uint)" },
    { "DEAD_RECKONING", FieldKind::U32, "1 when the fix is estimated / dead-reckoning (uint)" },
};

// Grade -- SDK/SensorLayer/DataParsers/SensorDataParserGrade.hpp
constexpr FieldSpec kFieldsGrade[] = {
    { "GRADE_PCT", FieldKind::Float, "Terrain grade in percent; valid only when GRADE_VALID." },
    { "GRADE_VALID", FieldKind::U32, "1 when grade_pct is a reliable current estimate." },
};

// Gyroscope -- SDK/SensorLayer/DataParsers/SensorDataParserGyroscope.hpp
constexpr FieldSpec kFieldsGyroscope[] = {
    { "X", FieldKind::Float, "X axis" },
    { "Y", FieldKind::Float, "Y axis" },
    { "Z", FieldKind::Float, "Z axis" },
};

// GyroscopeRaw -- SDK/SensorLayer/DataParsers/SensorDataParserGyroscopeRaw.hpp
constexpr FieldSpec kFieldsGyroscopeRaw[] = {
    { "X", FieldKind::I32, "X axis" },
    { "Y", FieldKind::I32, "Y axis" },
    { "Z", FieldKind::I32, "Z axis" },
};

// HeartRate -- SDK/SensorLayer/DataParsers/SensorDataParserHeartRate.hpp
constexpr FieldSpec kFieldsHeartRate[] = {
    { "BPM", FieldKind::Float, "Heart rate in bpm (float)" },
    { "TRUST_LEVEL", FieldKind::Float, "Trust level (float)" },
};

// HeartRateEx -- SDK/SensorLayer/DataParsers/SensorDataParserHeartRateEx.hpp
constexpr FieldSpec kFieldsHeartRateEx[] = {
    { "BPM", FieldKind::Float, "Arbitrated heart rate (bpm)" },
    { "TRUST_LEVEL", FieldKind::Float, "Arbitrated trust level" },
    { "SOURCE", FieldKind::Float, "Which source was chosen (Source)" },
    { "OPTICAL_BPM", FieldKind::Float, "Raw optical (PPG) bpm (0 if none)" },
    { "OPTICAL_TRUST", FieldKind::Float, "Raw optical trust" },
    { "EXTERNAL_BPM", FieldKind::Float, "Raw external (strap) bpm (0 if none)" },
    { "EXTERNAL_TRUST", FieldKind::Float, "Raw external trust" },
};

// HeartRateMetrics -- SDK/SensorLayer/DataParsers/SensorDataParserHeartRateMetrics.hpp
constexpr FieldSpec kFieldsHeartRateMetrics[] = {
    { "AHR", FieldKind::Float, "Average Heart Rate (float, bpm)" },
    { "RHR", FieldKind::Float, "Resting Heart Rate (float, bpm)" },
};

// MotionDetect -- SDK/SensorLayer/DataParsers/SensorDataParserMotionDetect.hpp
constexpr FieldSpec kFieldsMotionDetect[] = {
    { "ID", FieldKind::U32, "Motion identifier (see @ref Motion)" },
};

// Pressure -- SDK/SensorLayer/DataParsers/SensorDataParserPressure.hpp
constexpr FieldSpec kFieldsPressure[] = {
    { "PRESS", FieldKind::Float, "Station pressure, Pa" },
    { "PRESS_SEA_LEVEL", FieldKind::Float, "QNH / sea-level pressure, Pa" },
};

// RunningCadence -- SDK/SensorLayer/DataParsers/SensorDataParserRunningCadence.hpp
constexpr FieldSpec kFieldsRunningCadence[] = {
    { "CADENCE_SPM", FieldKind::Float, "" },
    { "CADENCE_VALID", FieldKind::U32, "" },
};

// Spo2 -- SDK/SensorLayer/DataParsers/SensorDataParserSpo2.hpp
constexpr FieldSpec kFieldsSpo2[] = {
    { "SATURATION", FieldKind::Float, "Blood-oxygen saturation in percent (float)" },
    { "TRUST_LEVEL", FieldKind::Float, "Trust level (float)" },
};

// StepCounter -- SDK/SensorLayer/DataParsers/SensorDataParserStepCounter.hpp
constexpr FieldSpec kFieldsStepCounter[] = {
    { "STEP_COUNT", FieldKind::U32, "Step count (uint32_t)" },
};

// StepDetector -- SDK/SensorLayer/DataParsers/SensorDataParserStepDetector.hpp
constexpr FieldSpec kFieldsStepDetector[] = {
    { "STEP_DETECTED", FieldKind::U32, "Step is detected (always 1)" },
};

// Temperature -- SDK/SensorLayer/DataParsers/SensorDataParserTemperature.hpp
constexpr FieldSpec kFieldsTemperature[] = {
    { "TEMP", FieldKind::Float, "Temperature value (units are device-specific)" },
};

// Touch -- SDK/SensorLayer/DataParsers/SensorDataParserTouch.hpp
constexpr FieldSpec kFieldsTouch[] = {
    { "TOUCH", FieldKind::U32, "Touch flag (expected value: 0 or 1)" },
};

// WristMotion -- SDK/SensorLayer/DataParsers/SensorDataParserWristMotion.hpp
constexpr FieldSpec kFieldsWristMotion[] = {
    { "WRIST_MOTION", FieldKind::U32, "Wrist motion event flag" },
};

constexpr ParserSpec kParsers[] = {
    { "Accelerometer", "SDK/SensorLayer/DataParsers/SensorDataParserAccelerometer.hpp", kFieldsAccelerometer, 3, Validity::Exact, false, false },
    { "AccelerometerRaw", "SDK/SensorLayer/DataParsers/SensorDataParserAccelerometerRaw.hpp", kFieldsAccelerometerRaw, 3, Validity::Exact, false, false },
    { "Activity", "SDK/SensorLayer/DataParsers/SensorDataParserActivity.hpp", kFieldsActivity, 1, Validity::Exact, false, false },
    { "ActivityRecognition", "SDK/SensorLayer/DataParsers/SensorDataParserActivityRecognition.hpp", kFieldsActivityRecognition, 2, Validity::Exact, true, false },
    { "Altimeter", "SDK/SensorLayer/DataParsers/SensorDataParserAltimeter.hpp", kFieldsAltimeter, 1, Validity::Exact, false, false },
    { "BatteryCharging", "SDK/SensorLayer/DataParsers/SensorDataParserBatteryCharging.hpp", kFieldsBatteryCharging, 2, Validity::Exact, true, false },
    { "BatteryLevel", "SDK/SensorLayer/DataParsers/SensorDataParserBatteryLevel.hpp", kFieldsBatteryLevel, 1, Validity::Exact, true, false },
    { "BatteryMetrics", "SDK/SensorLayer/DataParsers/SensorDataParserBatteryMetrics.hpp", kFieldsBatteryMetrics, 5, Validity::Exact, false, false },
    { "FloorCounter", "SDK/SensorLayer/DataParsers/SensorDataParserFloorCounter.hpp", kFieldsFloorCounter, 2, Validity::Exact, false, false },
    { "Fusion", "SDK/SensorLayer/DataParsers/SensorDataParserFusion.hpp", kFieldsFusion, 6, Validity::Exact, false, false },
    { "FusionRaw", "SDK/SensorLayer/DataParsers/SensorDataParserFusionRaw.hpp", kFieldsFusionRaw, 6, Validity::Exact, false, false },
    { "GpsDistance", "SDK/SensorLayer/DataParsers/SensorDataParserGpsDistance.hpp", kFieldsGpsDistance, 1, Validity::Exact, false, false },
    { "GpsLocation", "SDK/SensorLayer/DataParsers/SensorDataParserGpsLocation.hpp", kFieldsGpsLocation, 5, Validity::Exact, true, true },
    { "GpsSpeed", "SDK/SensorLayer/DataParsers/SensorDataParserGpsSpeed.hpp", kFieldsGpsSpeed, 3, Validity::Exact, false, false },
    { "Grade", "SDK/SensorLayer/DataParsers/SensorDataParserGrade.hpp", kFieldsGrade, 2, Validity::Exact, false, false },
    { "Gyroscope", "SDK/SensorLayer/DataParsers/SensorDataParserGyroscope.hpp", kFieldsGyroscope, 3, Validity::Exact, false, false },
    { "GyroscopeRaw", "SDK/SensorLayer/DataParsers/SensorDataParserGyroscopeRaw.hpp", kFieldsGyroscopeRaw, 3, Validity::Exact, false, false },
    { "HeartRate", "SDK/SensorLayer/DataParsers/SensorDataParserHeartRate.hpp", kFieldsHeartRate, 2, Validity::Exact, false, false },
    { "HeartRateEx", "SDK/SensorLayer/DataParsers/SensorDataParserHeartRateEx.hpp", kFieldsHeartRateEx, 7, Validity::AtLeast, false, false },
    { "HeartRateMetrics", "SDK/SensorLayer/DataParsers/SensorDataParserHeartRateMetrics.hpp", kFieldsHeartRateMetrics, 2, Validity::Exact, false, false },
    { "MotionDetect", "SDK/SensorLayer/DataParsers/SensorDataParserMotionDetect.hpp", kFieldsMotionDetect, 1, Validity::Exact, true, false },
    { "Pressure", "SDK/SensorLayer/DataParsers/SensorDataParserPressure.hpp", kFieldsPressure, 2, Validity::Exact, false, false },
    { "RunningCadence", "SDK/SensorLayer/DataParsers/SensorDataParserRunningCadence.hpp", kFieldsRunningCadence, 2, Validity::Exact, false, false },
    { "Spo2", "SDK/SensorLayer/DataParsers/SensorDataParserSpo2.hpp", kFieldsSpo2, 2, Validity::Exact, false, false },
    { "StepCounter", "SDK/SensorLayer/DataParsers/SensorDataParserStepCounter.hpp", kFieldsStepCounter, 1, Validity::Exact, false, false },
    { "StepDetector", "SDK/SensorLayer/DataParsers/SensorDataParserStepDetector.hpp", kFieldsStepDetector, 1, Validity::Exact, true, false },
    { "Temperature", "SDK/SensorLayer/DataParsers/SensorDataParserTemperature.hpp", kFieldsTemperature, 1, Validity::Exact, false, false },
    { "Touch", "SDK/SensorLayer/DataParsers/SensorDataParserTouch.hpp", kFieldsTouch, 1, Validity::Exact, true, false },
    { "WristMotion", "SDK/SensorLayer/DataParsers/SensorDataParserWristMotion.hpp", kFieldsWristMotion, 1, Validity::Exact, true, false },
};
constexpr size_t kParserCount = 29;

constexpr TypeSpec kTypes[] = {
    { "ACCELEROMETER", 0x10u, "Acceleration (3-axis).", 0, false },
    { "ACCELEROMETER_RAW", 0x11u, "Acceleration raw samples (implementation-defined units).", 1, false },
    { "GYROSCOPE", 0x20u, "Angular rate (3-axis).", 15, false },
    { "GYROSCOPE_RAW", 0x21u, "Angular rate raw samples.", 16, false },
    { "MAGNETIC_FIELD", 0x30u, "Magnetic field (3-axis).", kNoParser, false },
    { "HEART_BEAT", 0x40u, "Beat peak event.", kNoParser, false },
    { "HEART_RATE", 0x41u, "Current (arbitrated) heart rate (bpm) + trust. 2 fields.", 17, false },
    { "HEART_RATE_METRICS_DAILY", 0x42u, "Aggregated metrics for the current day (e.g., AHR, RHR).", 19, false },
    { "HEART_RATE_EX", 0x43u, "Opt-in multi-source HR: arbitrated + source + raw optical + raw external. 7 fields.", 18, true },
    { "STEP_DETECTOR", 0x50u, "Step event.", 25, false },
    { "STEP_COUNTER", 0x51u, "Step count since boot (monotonic).", 24, false },
    { "STEP_COUNTER_DAILY", 0x52u, "Step count for the current day.", 24, true },
    { "RUNNING_CADENCE", 0x53u, "Running cadence (steps/min); step length is derived SDK-side.", 22, true },
    { "FLOOR_COUNTER", 0x60u, "Floor counter since boot (monotonic).", 8, false },
    { "FLOOR_COUNTER_DAILY", 0x61u, "Floor counter for the current day.", 8, true },
    { "AMBIENT_TEMPERATURE", 0x70u, "Ambient temperature.", 26, false },
    { "PRESSURE", 0x80u, "Atmospheric pressure.", 21, false },
    { "ALTIMETER", 0x90u, "Altimeter.", 4, false },
    { "WRIST_MOTION", 0xA0u, "Wrist-motion event", 28, false },
    { "MOTION_DETECT", 0xB0u, "Motion state events. NO_MOTION, MOTION, SIG_MOTION", 20, false },
    { "ACTIVITY_RECOGNITION", 0xC0u, "Activity state classification. STILL, WALKING, RUNNING, UNKNOWN", 3, false },
    { "GESTURE_RECOGNITION", 0xD0u, "Discrete gesture events.", kNoParser, false },
    { "ACTIVITY_TIME", 0xE0u, "Active minutes since boot (monotonic).", 2, true },
    { "ACTIVITY_TIME_DAILY", 0xE1u, "Active minutes for the current day.", 2, false },
    { "PPG", 0xF0u, "Photoplethysmogram data.", kNoParser, false },
    { "SPO2", 0xF1u, "Blood-oxygen saturation (SpO2), derived from the optical PPG path (%).", 23, false },
    { "ECG", 0x100u, "Electrocardiogram data.", kNoParser, false },
    { "GPS_LOCATION", 0x110u, "GNSS location.", 12, false },
    { "GPS_SPEED", 0x111u, "GNSS speed.", 13, false },
    { "GPS_DISTANCE", 0x112u, "GNSS distance / odometer.", 11, false },
    { "BATTERY_LEVEL", 0x120u, "Charge level (%).", 6, false },
    { "BATTERY_CHARGING", 0x121u, "Charging state.", 5, false },
    { "BATTERY_METRICS", 0x122u, "Voltage/current/capacity metrics.", 7, false },
    { "FUSION", 0x130u, "Fused IMU (accel+gyro+mag).", 9, false },
    { "FUSION_RAW", 0x131u, "Raw fusion inputs.", 10, false },
    { "TOUCH_DETECT", 0x140u, "Touch detection, worn / unworn", 27, false },
    { "GRADE", 0x150u, "Barometric terrain grade (%) + validity.", 14, true },
};
constexpr size_t kTypeCount = 37;

/// Fields across every type that ships a parser. Layer 5 opens one claim
/// row per field, so this plus `kAssumedFieldsWhenNoParser` per unparsed
/// type is what sizes the store.
constexpr size_t kParsedFieldTotal = 76;
constexpr size_t kTypesWithoutParser = 5;
/// Types `Docs/SensorsLayer.md` does not mention. Finding number one, and
/// the reason this file is generated.
constexpr size_t kTypesMissingFromDoc = 6;

} // namespace SensorLab::Catalogue

#endif // SENSORLAB_SENSORTYPETABLE_GENERATED_HPP
