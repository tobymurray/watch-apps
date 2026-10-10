#ifndef SLUMBER_CARD_HPP
#define SLUMBER_CARD_HPP

#include <cstddef>
#include <cstdint>

/// The record Slumber publishes to ../SharedData/slumber/card for its glance.
/// Header-only and SDK-free, so the writer, the reader and the host tests all
/// encode it with the same code.
namespace SlumberCard
{

constexpr uint32_t kMagic  = 0x424D4C53u;  ///< "SLMB" little-endian
/// Bumped when a field changes meaning; a reader refuses any other value.
constexpr uint16_t kSchema = 1;
constexpr size_t   kSize   = 48;
/// How often the writer refreshes `heartbeatUtc`; readers judge staleness by it.
constexpr uint32_t kHeartbeatS = 5u * 60u;

enum class State : uint8_t {
    None        = 0,  ///< nothing recorded yet
    Probe       = 1,  ///< a lifecycle probe build, no sleep data
    Recording   = 2,
    Report      = 3,
    NotWorn     = 4,
    Interrupted = 5,
};

struct Card {
    uint32_t seq             = 0;  ///< +1 per write
    uint32_t writtenUtc      = 0;  ///< s; 0 = clock unknown
    uint32_t heartbeatUtc    = 0;  ///< s; when the writer last proved it was running
    uint32_t validUntilUtc   = 0;  ///< s; 0 = no expiry
    uint32_t serviceStartUtc = 0;  ///< s; when the writer's process started
    uint32_t launchNo        = 0;  ///< writer process starts ever
    State    state           = State::None;
    uint16_t sleepMin        = 0;
    uint16_t stillMin        = 0;
    int16_t  deltaVsUsualMin = 0;
    uint16_t onsetMinOfDay   = 0;  ///< local
    uint16_t wakeMinOfDay    = 0;  ///< local
};

/// CRC-32 (IEEE 802.3, reflected), the same polynomial zlib uses.
inline uint32_t crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

namespace detail
{

inline void put16(uint8_t *p, uint16_t v)
{
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
}

inline void put32(uint8_t *p, uint32_t v)
{
    put16(p, static_cast<uint16_t>(v));
    put16(p + 2, static_cast<uint16_t>(v >> 16));
}

inline uint16_t get16(const uint8_t *p)
{
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

inline uint32_t get32(const uint8_t *p)
{
    return static_cast<uint32_t>(get16(p)) | (static_cast<uint32_t>(get16(p + 2)) << 16);
}

// Byte offsets of the frozen v1 layout.
constexpr size_t kOffMagic     = 0;
constexpr size_t kOffSchema    = 4;
constexpr size_t kOffLength    = 6;
constexpr size_t kOffSeq       = 8;
constexpr size_t kOffWritten   = 12;
constexpr size_t kOffHeartbeat = 16;
constexpr size_t kOffValid     = 20;
constexpr size_t kOffStart     = 24;
constexpr size_t kOffLaunch    = 28;
constexpr size_t kOffState     = 32;
constexpr size_t kOffSleep     = 34;
constexpr size_t kOffStill     = 36;
constexpr size_t kOffDelta     = 38;
constexpr size_t kOffOnset     = 40;
constexpr size_t kOffWake      = 42;
constexpr size_t kOffCrc       = 44;
static_assert(kOffCrc + 4 == kSize, "the CRC ends the record");

} // namespace detail

inline void encode(const Card &c, uint8_t (&out)[kSize])
{
    using namespace detail;
    for (auto &b : out) {
        b = 0;
    }
    put32(out + kOffMagic, kMagic);
    put16(out + kOffSchema, kSchema);
    put16(out + kOffLength, static_cast<uint16_t>(kSize));
    put32(out + kOffSeq, c.seq);
    put32(out + kOffWritten, c.writtenUtc);
    put32(out + kOffHeartbeat, c.heartbeatUtc);
    put32(out + kOffValid, c.validUntilUtc);
    put32(out + kOffStart, c.serviceStartUtc);
    put32(out + kOffLaunch, c.launchNo);
    out[kOffState] = static_cast<uint8_t>(c.state);
    put16(out + kOffSleep, c.sleepMin);
    put16(out + kOffStill, c.stillMin);
    put16(out + kOffDelta, static_cast<uint16_t>(c.deltaVsUsualMin));
    put16(out + kOffOnset, c.onsetMinOfDay);
    put16(out + kOffWake, c.wakeMinOfDay);
    put32(out + kOffCrc, crc32(out, kOffCrc));
}

enum class Decode : uint8_t {
    Ok,
    Short,      ///< fewer than kSize bytes: torn, or not this file
    BadMagic,
    BadSchema,  ///< written by a Slumber this reader does not understand
    BadCrc,     ///< torn mid-write, or corrupt
};

inline Decode decode(const uint8_t *in, size_t len, Card &out)
{
    using namespace detail;
    if (in == nullptr || len < kSize) {
        return Decode::Short;
    }
    if (get32(in + kOffMagic) != kMagic) {
        return Decode::BadMagic;
    }
    if (get16(in + kOffSchema) != kSchema || get16(in + kOffLength) != kSize) {
        return Decode::BadSchema;
    }
    if (get32(in + kOffCrc) != crc32(in, kOffCrc)) {
        return Decode::BadCrc;
    }
    Card c;
    c.seq             = get32(in + kOffSeq);
    c.writtenUtc      = get32(in + kOffWritten);
    c.heartbeatUtc    = get32(in + kOffHeartbeat);
    c.validUntilUtc   = get32(in + kOffValid);
    c.serviceStartUtc = get32(in + kOffStart);
    c.launchNo        = get32(in + kOffLaunch);
    c.state           = static_cast<State>(in[kOffState]);
    c.sleepMin        = get16(in + kOffSleep);
    c.stillMin        = get16(in + kOffStill);
    c.deltaVsUsualMin = static_cast<int16_t>(get16(in + kOffDelta));
    c.onsetMinOfDay   = get16(in + kOffOnset);
    c.wakeMinOfDay    = get16(in + kOffWake);
    out               = c;
    return Decode::Ok;
}

} // namespace SlumberCard

#endif // SLUMBER_CARD_HPP
