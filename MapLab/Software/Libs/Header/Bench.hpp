/**
 ******************************************************************************
 * @file    Bench.hpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   Timing harness for a device whose only clock counts milliseconds.
 ******************************************************************************
 *
 * `SDK::Interface::ISystem` offers exactly one clock: `getTimeMs()`, a 32-bit
 * millisecond uptime counter. There is no cycle counter exposed, no
 * microsecond timer, and no profiling hook. Everything this app claims to
 * measure has to come out of that, and several of the things worth measuring
 * -- a varint decode, a 3 px brush stamp -- take a few hundred nanoseconds.
 *
 * So nothing here times a single operation. `measure()` runs the subject
 * repeatedly, doubling the count until the elapsed time is long enough for
 * millisecond resolution to be a rounding error rather than the result, and
 * reports the per-operation cost derived from that. At the default 200 ms
 * floor the quantisation error is under 0.5 %, which is far below the
 * variation between runs.
 *
 * Three details that are easy to get wrong and would quietly produce numbers
 * that look fine:
 *
 *   - **The clock wraps** at 2^32 ms (~49.7 days of uptime). Subtracting
 *     unsigned values handles that correctly and comparing signed ones does
 *     not, so the elapsed calculation is deliberately unsigned throughout.
 *   - **The subject must not be optimised away.** A benchmark whose result is
 *     unused is a benchmark of an empty loop. Every subject here writes to
 *     memory the caller can observe, and `measure()` returns a checksum the
 *     caller is expected to fold into its output.
 *   - **One iteration is a legitimate answer.** A full-viewport render is
 *     expected to exceed the floor on its own; the harness reports
 *     `iterations == 1` rather than pretending to average, and the report
 *     script prints that as-is because a single-shot cost is exactly what the
 *     100 ms frame budget is about.
 *
 * Pure: templated on the clock, so host tests drive it with a fake one and the
 * app passes the kernel's. Host-tested.
 ******************************************************************************
 */

#ifndef MAPLAB_BENCH_HPP
#define MAPLAB_BENCH_HPP

#include <cstdint>

namespace MapLab
{

struct BenchResult {
    uint32_t iterations = 0;
    uint32_t elapsedMs  = 0;
    /// Microseconds per operation, rounded to nearest. The unit is us rather
    /// than ns because a uint32 of ns overflows at 4.3 s and some of these
    /// operations are slower than that.
    uint32_t usPerOp    = 0;
    /// Folded return value of the subject, so nothing can be optimised out.
    uint32_t checksum   = 0;
    /// False when the clock never advanced across the whole run: the number is
    /// then a lower bound, not a measurement, and must be reported as such.
    bool     valid      = false;
};

/// Default floor. Long enough that ms quantisation is <0.5 %, short enough
/// that a suite of twenty benches still finishes while somebody is holding
/// the watch.
constexpr uint32_t kDefaultFloorMs = 200;
/// Backstop so a subject that somehow costs nothing cannot spin forever.
constexpr uint32_t kMaxIterations  = 1u << 22;

/**
 * @brief Run `fn` enough times to be measurable and report the cost of one.
 *
 * @param clock  anything with `uint32_t nowMs()`.
 * @param fn     the subject; returns a uint32 the harness folds into
 *               `checksum` so the compiler cannot discard the work.
 */
template <class Clock, class Fn>
BenchResult measure(Clock& clock, Fn&& fn,
                    uint32_t floorMs = kDefaultFloorMs,
                    uint32_t maxIterations = kMaxIterations)
{
    BenchResult r;
    uint32_t iterations = 1;

    for (;;) {
        uint32_t sum = 0;
        const uint32_t t0 = clock.nowMs();
        for (uint32_t i = 0; i < iterations; ++i) {
            sum += fn();
        }
        const uint32_t elapsed = clock.nowMs() - t0;

        r.iterations = iterations;
        r.elapsedMs  = elapsed;
        r.checksum   = sum;

        if (elapsed >= floorMs || iterations >= maxIterations) {
            r.valid   = elapsed > 0;
            // Rounded, in 64-bit: elapsed * 1000 overflows a uint32 at 4.3 s.
            const uint64_t us = static_cast<uint64_t>(elapsed) * 1000u;
            r.usPerOp = static_cast<uint32_t>((us + iterations / 2) / iterations);
            return r;
        }

        // Jump straight to a count that should clear the floor rather than
        // doubling towards it: on a subject costing ~1 us that is the
        // difference between 18 timed passes and 2.
        if (elapsed == 0) {
            iterations *= 8;
        } else {
            const uint64_t want = (static_cast<uint64_t>(iterations) * floorMs * 12) /
                                  (static_cast<uint64_t>(elapsed) * 10);
            iterations = (want > maxIterations) ? maxIterations
                                                : static_cast<uint32_t>(want + 1);
        }
    }
}

} // namespace MapLab

#endif // MAPLAB_BENCH_HPP
