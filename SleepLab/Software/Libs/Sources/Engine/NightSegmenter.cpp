/**
 ******************************************************************************
 * @file    NightSegmenter.cpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   When a night starts and ends. Rationale is in the header.
 ******************************************************************************
 */

#include "Engine/NightSegmenter.hpp"

#include "Engine/Epoch.hpp"

namespace Engine
{

bool inWindow(int16_t t, int16_t startMin, int16_t endMin)
{
    if (t < 0 || t >= kMinutesPerDay) {
        return false;
    }
    if (startMin == endMin) {
        // A zero-width window is almost certainly a configuration mistake, and
        // the two readings of it -- "never" and "always" -- are wildly
        // different. "Never" is chosen because it fails visibly: no night is
        // ever recorded and the user goes and looks at the setting. "Always"
        // would record continuously and look like it was working.
        return false;
    }
    if (startMin < endMin) {
        return t >= startMin && t < endMin;
    }
    // Wrapped: 21:00-11:00 is "at or after 21:00, or before 11:00".
    return t >= startMin || t < endMin;
}

NightSegmenter::Update NightSegmenter::closeNow(bool clockJumped)
{
    Update u;
    // Carried through rather than rebuilt. A jump that *also* closes the
    // session -- the clock moving forward past the end of the window is
    // exactly that -- is the case where the flag matters most, and an Update
    // constructed fresh in here would silently drop it.
    u.clockJumped   = clockJumped;
    u.sessionEpochs = mSessionEpochs;
    u.event = (mSessionEpochs >= mCfg.minSessionMin) ? Event::Closed
                                                     : Event::Discarded;
    mState         = State::Idle;
    mStillWindow   = 0;
    mStillSeen     = 0;
    mActiveRun     = 0;
    mSessionEpochs = 0;
    u.state = mState;
    return u;
}

void NightSegmenter::resumeOpen(uint16_t epochsSoFar)
{
    mState         = State::Open;
    // Clamped, because this number comes off disk. `night_state.txt` is parsed
    // with an unbounded `%lu` and is rewritten 1 900 times a night, so a corrupt
    // count is reachable -- and 65 535 would take one increment to wrap the
    // counter to zero, after which the session is below `minSessionMin` and cannot
    // close on activity, cannot close on the length bound, and runs until the
    // clock takes it out of the window. If it ever does.
    mSessionEpochs = (mCfg.maxSessionMin > 0 && epochsSoFar > mCfg.maxSessionMin)
                         ? mCfg.maxSessionMin
                         : epochsSoFar;
    mStillWindow   = 0;
    mStillSeen     = 0;
    // Deliberately zero, not carried: whatever the wearer was doing before the
    // restart is not evidence about now, and a resumed session that inherited a
    // nine-epoch activity run would close on its very first active epoch.
    mActiveRun     = 0;
}

NightSegmenter::Update NightSegmenter::finish()
{
    if (mState != State::Open) {
        Update u;
        u.state = mState;
        return u;
    }
    return closeNow(false);
}

NightSegmenter::Update NightSegmenter::update(int16_t localMin, bool worn,
                                              uint32_t count, int32_t stepDelta)
{
    Update u;

    // A jump is any move other than the one epoch this update represents.
    // Detected against the previous reading rather than against uptime,
    // because that is what makes it a *clock* jump rather than a scheduling
    // delay -- the caller marks the night interrupted either way.
    if (mLastLocalMin >= 0 && localMin >= 0) {
        int16_t step = static_cast<int16_t>(localMin - mLastLocalMin);
        if (step < 0) {
            step = static_cast<int16_t>(step + kMinutesPerDay); // midnight
        }
        // One minute is expected; two tolerates an epoch that landed late.
        u.clockJumped = (step > 2);
    }
    mLastLocalMin = localMin;

    const bool still  = worn && count <= mCfg.stillnessCountMax;
    const bool active = count >= mCfg.activityCountMin;
    const bool walked = stepDelta != kAbsent &&
                        stepDelta >= static_cast<int32_t>(mCfg.stepsToCloseInEpoch);

    if (mState == State::Idle) {
        // No clock, no bedtime. Opening a session needs to know it is night,
        // and without a wall clock there is no such thing.
        if (localMin < 0 || !inWindow(localMin, mCfg.windowStartMin, mCfg.windowEndMin)) {
            mStillWindow = 0;
            mStillSeen   = 0;
            u.state = mState;
            return u;
        }

        // A window, not a run. Shift the newest epoch in at bit 0 and keep only
        // the last `stillnessToOpenMin` bits.
        const uint16_t win = (mCfg.stillnessToOpenMin > kMaxStillWindow)
                                 ? kMaxStillWindow
                                 : mCfg.stillnessToOpenMin;
        mStillWindow = (mStillWindow << 1) | (still ? 1u : 0u);
        if (win < 32u) {
            mStillWindow &= (1u << win) - 1u;
        }
        if (mStillSeen < win) { mStillSeen++; }

        uint16_t stillInWindow = 0;
        for (uint32_t bits = mStillWindow; bits != 0u; bits >>= 1) {
            stillInWindow = static_cast<uint16_t>(stillInWindow + (bits & 1u));
        }

        const uint16_t tolerance = (mCfg.stillnessToleranceEpochs >= win)
                                       ? static_cast<uint16_t>(win - 1)
                                       : mCfg.stillnessToleranceEpochs;
        const uint16_t needed = static_cast<uint16_t>(win - tolerance);

        // The window has to be full before it can be judged, or a night opens on
        // three still epochs and a tolerance of one.
        if (mStillSeen >= win && stillInWindow >= needed) {
            mState = State::Open;

            // Backdate to the oldest *still* epoch in the window, not to the
            // window's own start.
            //
            // The two differ exactly when the window's leading edge is one of
            // the epochs the tolerance forgave, and backdating past it would
            // pull a minute the wearer was moving into the night -- shifting
            // reported onset earlier than anything observed. The oldest still
            // epoch is the highest set bit; bit 0 is the epoch just closed, so a
            // highest bit at position h means h + 1 epochs to carry back.
            uint16_t oldestStill = 0;
            for (uint16_t b = 0; b < win; ++b) {
                if ((mStillWindow >> b) & 1u) { oldestStill = b; }
            }
            const uint16_t backdate = static_cast<uint16_t>(oldestStill + 1);

            u.event          = Event::Opened;
            u.backdateEpochs = backdate;
            mSessionEpochs   = backdate;
            mStillWindow     = 0;
            mStillSeen       = 0;
            mActiveRun       = 0;
        }

        u.state = mState;
        return u;
    }

    // -- Open ----------------------------------------------------------------

    // Saturating, so the length bound below is reached rather than jumped over.
    if (mSessionEpochs < 0xFFFFu) {
        mSessionEpochs++;
    }

    // Leaving the window ends the session unconditionally. A session still
    // open at 11:00 has stopped being a night whatever the accelerometer says,
    // and the most likely cause is a watch left on a desk.
    //
    // A missing clock deliberately does not close it: the session is already
    // known to be a night, and losing real data over a clock the app does not
    // control would be the wrong trade.
    if (localMin >= 0 &&
        !inWindow(localMin, mCfg.windowStartMin, mCfg.windowEndMin)) {
        return closeNow(u.clockJumped);
    }

    // The backstop, before anything that can be defeated by an unreadable clock
    // or by a wrist that never moves. A session at this length has stopped being
    // a night whatever every other signal says, and the alternative to ending it
    // is a counter that wraps -- see maxSessionMin.
    if (mCfg.maxSessionMin > 0 && mSessionEpochs >= mCfg.maxSessionMin) {
        return closeNow(u.clockJumped);
    }

    if (mSessionEpochs >= mCfg.minSessionMin) {
        // Steps are unambiguous: a sleeping wrist does not accumulate them.
        if (walked) {
            return closeNow(u.clockJumped);
        }

        if (active) {
            mActiveRun++;
            if (mActiveRun >= mCfg.activityToCloseMin) {
                return closeNow(u.clockJumped);
            }
        } else {
            mActiveRun = 0;
        }
    } else {
        // Before the minimum duration, activity is tracked but cannot close
        // the session -- this is what stops a bathroom trip at 00:30 splitting
        // one night into two, which would produce two short nights that both
        // then fail the minimum and vanish entirely.
        mActiveRun = active ? static_cast<uint16_t>(mActiveRun + 1) : 0;
    }

    u.state         = mState;
    u.sessionEpochs = mSessionEpochs;
    return u;
}

} // namespace Engine
