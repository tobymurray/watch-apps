/**
 ******************************************************************************
 * @file    Sampler.cpp
 * @date    24-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   Poll every GPIO input register, and write down what moved.
 ******************************************************************************
 */

#include "Sampler.hpp"

#include <cstdio>

namespace Probe
{

Sampler::Sampler(SDK::Kernel &kernel, Log &log)
    : mKernel(kernel)
    , mLog(log)
{
}

bool Sampler::available()
{
    return kHasRegisters;
}

void Sampler::logMask(uint32_t watched)
{
    mLog.line("calibrated: watching %u pins", static_cast<unsigned>(watched));

    for (uint32_t port = 0; port < kPortCount; ++port) {
        const uint32_t mask = mWatch.maskOf(port);
        if (mask == 0u) {
            // Either the port never answered or every one of its pins was busy.
            // Both are worth one line: a run that watched nothing on the port
            // the buttons turn out to be on is a run whose empty result means
            // nothing.
            mLog.line("  GPIO%c %08lX: nothing to watch",
                      portName(port), static_cast<unsigned long>(portBase(port)));
            continue;
        }
        mLog.line("  GPIO%c %08lX: mask %04lX",
                  portName(port), static_cast<unsigned long>(portBase(port)),
                  static_cast<unsigned long>(mask));
    }
}

uint32_t Sampler::poll()
{
    uint32_t snapshot[kPortCount] = {};

    if (!readIdrs(snapshot)) {
        return 0;
    }

    const uint32_t now = mKernel.sys.getTimeMs();

    if (!mHaveStart) {
        mStartMs   = now;
        mHaveStart = true;
        mLog.line("calibrating for %u ms -- do not touch the watch",
                  static_cast<unsigned>(kCalibrateMs));
    }

    if (!mWatch.settled()) {
        // Unsigned throughout, so the millisecond counter wrapping mid-window
        // ends the calibration early rather than never.
        if ((now - mStartMs) < kCalibrateMs) {
            mWatch.observe(snapshot);
            return 0;
        }

        logMask(mWatch.settle(snapshot));
        return 0;
    }

    Transition   changes[kMaxTransitions];
    const uint32_t count = mWatch.update(snapshot, changes, kMaxTransitions);

    for (uint32_t i = 0; i < count; ++i) {
        // One line per edge, with the millisecond it was seen at. Correlating
        // these against the button events the GUI logs into the same file is
        // the whole method, and it is done by timestamp rather than by anything
        // this app decides -- so the file can be re-read later by somebody who
        // disagrees about what it means.
        mLog.line("t=%lu pin P%c%u -> %u",
                  static_cast<unsigned long>(now),
                  portName(changes[i].port),
                  static_cast<unsigned>(changes[i].pin),
                  static_cast<unsigned>(changes[i].level));
    }

    if (count > 0) {
        const Transition &latest = changes[count - 1];
        std::snprintf(mLast, sizeof mLast, "P%c%u>%u",
                      portName(latest.port),
                      static_cast<unsigned>(latest.pin),
                      static_cast<unsigned>(latest.level));
    }

    mSeen += count;
    return count;
}

} // namespace Probe
