#ifndef HAPTICLAB_SESSIONCONFIG_HPP
#define HAPTICLAB_SESSIONCONFIG_HPP

#include <cstdint>
#include <cstring>

#include "SDK/JSON/JsonStreamReader.hpp"

#include "Patterns.hpp"
#include "Records.hpp"
#include "Schedule.hpp"

/// What the operator sets before a session, read from a JSON file written into
/// the app's folder over USB. Nothing reaches a running app from the phone, and
/// a condition is not something to leave to a default: the menu draws it in
/// amber before the run starts, because a no-contact control recorded as a
/// wrist run is a ruined session that looks like a real one.
namespace Haptic {

constexpr char kConfigPath[] = "/config.json";

struct SessionConfig {
    uint32_t seed        = 0;
    SetId    set         = SetId::RhythmAndFamily;
    uint8_t  testPasses  = kDefaultTestPasses;
    Contact  contact     = Contact::Wrist;
    Masking  masking     = Masking::Unmasked;

    char wrist[kNoteLength + 1]          = "unrecorded";
    char watchPosition[kNoteLength + 1]  = "unrecorded";
    char strapTightness[kNoteLength + 1] = "unrecorded";
    char armPosture[kNoteLength + 1]     = "unrecorded";

    /// Sentinel rather than a plausible room temperature: nobody said.
    int16_t ambientTempC = -32768;

    uint8_t sweepRepeats     = 2;
    uint8_t sweepFirstEffect = kMinEffect;
    uint8_t sweepLastEffect  = kMaxEffect;

    /// False when the file was missing or unparseable, which `session.json`
    /// records so a run whose condition came from a default is identifiable.
    bool fromFile = false;
};

namespace detail {

inline void copyNote(char (&dest)[kNoteLength + 1], const char *src, size_t len)
{
    const size_t copy = len < kNoteLength ? len : kNoteLength;
    std::memcpy(dest, src, copy);
    dest[copy] = '\0';
    // A comma or a quote in an operator note would break the row it lands in,
    // so they are replaced rather than escaped.
    for (size_t i = 0; i < copy; i++) {
        if (dest[i] == ',' || dest[i] == '"' || dest[i] == '\n' || dest[i] == '\r') {
            dest[i] = ' ';
        }
    }
}

inline bool readNote(const SDK::JsonStreamReader &json, const char *key,
                     char (&dest)[kNoteLength + 1])
{
    const char *value = nullptr;
    size_t      len   = 0;
    if (!json.get(key, value, len) || value == nullptr) {
        return false;
    }
    copyNote(dest, value, len);
    return true;
}

inline bool matches(const char *value, size_t len, const char *expected)
{
    return len == std::strlen(expected) && std::strncmp(value, expected, len) == 0;
}

} // namespace detail

/// Fills `out` from a JSON document, leaving any field the document does not
/// mention at its default. Returns false when the document is not valid JSON,
/// in which case `out` is untouched.
inline bool parseConfig(const char *data, size_t len, SessionConfig &out)
{
    SDK::JsonStreamReader json(data, len);
    if (!json.validate()) {
        return false;
    }

    SessionConfig parsed;

    json.get("seed", parsed.seed);
    json.get("test_passes", parsed.testPasses);
    json.get("sweep_repeats", parsed.sweepRepeats);
    json.get("sweep_first_effect", parsed.sweepFirstEffect);
    json.get("sweep_last_effect", parsed.sweepLastEffect);
    json.get("ambient_temp_c", parsed.ambientTempC);

    const char *value = nullptr;
    size_t      valueLen = 0;
    if (json.get("set", value, valueLen) && value != nullptr) {
        parsed.set = detail::matches(value, valueLen, setName(SetId::WithAmplitude))
                         ? SetId::WithAmplitude
                         : SetId::RhythmAndFamily;
    }
    if (json.get("contact", value, valueLen) && value != nullptr) {
        parsed.contact = detail::matches(value, valueLen, contactName(Contact::Table))
                             ? Contact::Table
                             : Contact::Wrist;
    }
    if (json.get("masking", value, valueLen) && value != nullptr) {
        parsed.masking = detail::matches(value, valueLen, maskingName(Masking::Masked))
                             ? Masking::Masked
                             : Masking::Unmasked;
    }

    detail::readNote(json, "wrist", parsed.wrist);
    detail::readNote(json, "watch_position", parsed.watchPosition);
    detail::readNote(json, "strap_tightness", parsed.strapTightness);
    detail::readNote(json, "arm_posture", parsed.armPosture);

    if (parsed.testPasses == 0) {
        parsed.testPasses = 1;
    }
    if (parsed.sweepRepeats == 0) {
        parsed.sweepRepeats = 1;
    }

    parsed.fromFile = true;
    out             = parsed;
    return true;
}

} // namespace Haptic

#endif // HAPTICLAB_SESSIONCONFIG_HPP
