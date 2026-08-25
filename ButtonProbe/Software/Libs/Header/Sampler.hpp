/**
 ******************************************************************************
 * @file    Sampler.hpp
 * @date    24-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   Poll every GPIO input register, and write down what moved.
 ******************************************************************************
 *
 * Shared by both halves of the app, because the sampling is the experiment and
 * running two versions of it would mean the two halves could disagree for
 * reasons that had nothing to do with the question.
 *
 * ## Poll, rather than wait
 *
 * `RunGuiProbe` measured the glance tick at roughly **1 Hz** -- not the 60 Hz
 * `CommandMessages.hpp` gives as its example. A button press and release is
 * over inside one of those intervals, so anything that samples on the tick
 * would miss most presses and could not tell a miss from an absence.
 *
 * So neither half samples on a tick. Both block on
 * `getMessage(msg, kPollMs)` instead, which returns either a message or a
 * timeout, and poll on every turn of that loop. The queue still drains normally;
 * the timeout just puts a floor under how often the pins are looked at.
 *
 * ## Calibration
 *
 * The first `kCalibrateMs` of every run is spent deciding which pins are worth
 * watching at all -- see PinWatch.hpp, which is where that idea lives. Nothing
 * is reported during it, so the watch must be left alone while it happens; each
 * half says on its screen when it is over.
 *
 ******************************************************************************
 */

#ifndef SAMPLER_HPP
#define SAMPLER_HPP

#include <cstdint>

#include "SDK/Kernel/Kernel.hpp"

#include "PinWatch.hpp"
#include "ProbeLog.hpp"

namespace Probe
{

class Sampler
{
public:
    Sampler(SDK::Kernel &kernel, Log &log);

    /// How long to block for a message before polling anyway. Short enough that
    /// a press lasting a tenth of a second is sampled several times, long
    /// enough that the loop is not a spin: the service only runs while its card
    /// is on screen or its GUI is up, but "only while you are looking" is not a
    /// licence to burn the battery while you are.
    static constexpr uint32_t kPollMs = 8;

    /// How long to watch the pins before deciding which ones are still. Half a
    /// second is many thousands of samples at kPollMs, which is ample for a
    /// signal to show itself, and short enough that nobody has pressed anything
    /// yet.
    static constexpr uint32_t kCalibrateMs = 500;

    /// Read the ports once. During calibration this only observes; afterwards
    /// it logs every transition on a watched pin. Returns how many it logged.
    uint32_t poll();

    /// True until the calibration window has closed.
    bool calibrating() const { return !mWatch.settled(); }

    /// Transitions logged since the sampler started. The number both halves put
    /// on the screen, so there is live feedback that the thing is working
    /// without having to unplug and read a file.
    uint32_t seen() const { return mSeen; }

    /// Whether this build can read registers at all. False on a host build.
    static bool available();

    /// The most recent transition, as short text -- "PD4>0" -- or an empty
    /// string before there has been one. Short because its destination is a
    /// glance line beside a counter, and because a pin is five characters.
    const char *last() const { return mLast; }

private:
    void logMask(uint32_t watched);

    /// More than the pins that exist, so a full buffer never silently drops an
    /// edge: kPortCount * kPinsPerPort is 128.
    static constexpr uint32_t kMaxTransitions = kPortCount * kPinsPerPort;

    SDK::Kernel &mKernel;
    Log         &mLog;
    PinWatch     mWatch;

    uint32_t mStartMs  = 0;
    bool     mHaveStart = false;
    uint32_t mSeen      = 0;

    /// "P" + letter + two digits + ">" + digit + NUL, with room to spare.
    char mLast[12] = {};
};

} // namespace Probe

#endif // SAMPLER_HPP
