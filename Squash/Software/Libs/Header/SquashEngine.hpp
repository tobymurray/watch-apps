/**
 ******************************************************************************
 * @file    SquashEngine.hpp
 * @brief   C ABI over the Rust epoch accumulator in Software/Libs/rust.
 ******************************************************************************
 *
 * The Service owns the clock, the sensor and the filesystem; the crate owns the
 * arithmetic. The features drawn on the watch and the distributions `phase-a`
 * prints off the recording come from one implementation, EffortKit's
 * `epoch` module, so the screen cannot disagree with the analysis.
 *
 ******************************************************************************
 */

#ifndef SQUASH_ENGINE_HPP
#define SQUASH_ENGINE_HPP

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One completed epoch, in the units the screen draws rather than the floats the
   features are computed in. */
typedef struct {
    uint32_t gyro_mag;      /* mean gyroscope vector magnitude, raw LSB */
    /* Variance of accelerometer magnitude, LSB^2 / 1000. Scaled because the raw
       figure reaches 2.5e8 on a real stroke. */
    uint32_t accel_var_k;
    uint32_t index;         /* epoch index from the recording's origin */
    uint16_t samples;       /* samples that arrived; 100 is nominal at 100 Hz */
    uint8_t  sat_accel_pct; /* % of samples with any accelerometer axis railed */
    uint8_t  sat_gyro_pct;  /* % of samples with any gyroscope axis railed */
} squash_epoch;

uint32_t squash_engine_abi_fingerprint(void);

/* Begin a recording; drops any epoch in progress. */
void squash_engine_reset(void);

/* Feed one sample on the recording's own clock. Returns 1 when this sample
   completed an epoch, at which point squash_engine_last_epoch() has the new
   features. Pass the sensor's timestamp, never a loop-local now: a batch
   carries ~10 samples and collapsing them onto one instant changes every
   feature. */
uint8_t squash_engine_push(uint32_t t_ms, int16_t ax, int16_t ay, int16_t az,
                           int16_t gx, int16_t gy, int16_t gz);

/* Close out the epoch in progress if it has any samples; 1 if one was made. */
uint8_t squash_engine_flush(void);

void squash_engine_last_epoch(squash_epoch *out);

/* Defined by the Service and called by the crate's panic handler. Without it a
   panic stops the Service silently and the recording is lost with no reason. */
void squash_engine_host_panic(const uint8_t *msg, uint32_t len);

#ifdef __cplusplus
} // extern "C"

namespace squash_engine_abi
{

constexpr uint32_t kFnvOffsetBasis = 0x811C9DC5u;
constexpr uint32_t kFnvPrime       = 0x01000193u;

constexpr uint32_t fnv1a(uint32_t hash, size_t byte)
{
    return (hash ^ (static_cast<uint32_t>(byte) & 0xFFu)) * kFnvPrime;
}

/* Walks this compiler's own layout, in the same order as `abi_fingerprint()` in
   the crate. A literal copied from the Rust side would agree forever and catch
   nothing; this disagrees the moment either struct drifts. */
constexpr uint32_t fingerprint()
{
    uint32_t h = kFnvOffsetBasis;
    h = fnv1a(h, sizeof(squash_epoch));
    h = fnv1a(h, alignof(squash_epoch));
    h = fnv1a(h, offsetof(squash_epoch, gyro_mag));
    h = fnv1a(h, offsetof(squash_epoch, accel_var_k));
    h = fnv1a(h, offsetof(squash_epoch, index));
    h = fnv1a(h, offsetof(squash_epoch, samples));
    h = fnv1a(h, offsetof(squash_epoch, sat_accel_pct));
    return fnv1a(h, offsetof(squash_epoch, sat_gyro_pct));
}

} // namespace squash_engine_abi
#endif // __cplusplus

#endif // SQUASH_ENGINE_HPP
