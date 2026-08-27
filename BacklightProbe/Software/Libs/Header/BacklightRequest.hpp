/**
 ******************************************************************************
 * @file    BacklightRequest.hpp
 * @brief   REQUEST_BACKLIGHT_SET behind a named call, and its result captured.
 ******************************************************************************
 *
 * Two things this does that no shipped caller does.
 *
 * **It waits.** `IAppComm::sendMessage(msg, timeoutMs)` with a non-zero timeout
 * arms the message's completion semaphore, blocks until the kernel has filled
 * the message in place, and leaves `getResult()` meaningful. With a zero timeout
 * it is fire-and-forget and the result stays `PENDING` whatever happened.
 * `GpsLab` and `Squash` both pass zero, which is why neither can tell you
 * whether the kernel has a handler for this message at all. That is Q1, and this
 * is the whole instrument for it.
 *
 * **It reports.** The caller gets back what was sent, what came back, and how
 * long it took, as a struct that goes straight into the results file. A probe
 * that logged only "sent" would be no better than the two callers it exists to
 * improve on.
 *
 * ## The shape is deliberate
 *
 * This is what the SDK should eventually offer: a small header-only class over
 * the message with named methods, the way `SDK::HomeWidget` wraps
 * `REQUEST_WIDGET_*`. Not a new kernel interface: `IBacklight` already exists
 * and no app can obtain it, and adding a seventh unreachable interface would
 * help nobody.
 *
 * Note what a real SDK wrapper should NOT offer: `brightness` as a percentage.
 * The field is inert on device, and `IBacklight`, the interface the kernel's own
 * driver implements, has no level parameter for it to travel through. A wrapper
 * that published `setBrightness(70)` would be repeating the mistake this app
 * exists to document. Here it is exposed only because measuring it is the point.
 *
 ******************************************************************************
 */

#ifndef BACKLIGHT_REQUEST_HPP
#define BACKLIGHT_REQUEST_HPP

#include <cstdint>

#include "SDK/Kernel/Kernel.hpp"
#include "SDK/Messages/CommandMessages.hpp"
#include "SDK/Messages/MessageBase.hpp"
#include "SDK/Messages/MessageGuard.hpp"

namespace Backlight
{

/// What one request did, in the form the results file wants it.
struct Outcome {
    uint8_t  brightness    = 0;
    uint32_t autoOffMs     = 0;
    uint32_t sendTimeoutMs = 0;

    /// What `sendMessage` returned. False means the message never left: a full
    /// queue or a failed allocation, which is a different failure from the
    /// kernel rejecting it, and the two must not be conflated in the record.
    bool sent = false;

    /// True if the message pool had nothing to give. `sent` is then also false.
    bool allocationFailed = false;

    /// `getResult()` after the call returned. PENDING here with a non-zero send
    /// timeout is the interesting negative: it means nothing signalled
    /// completion, i.e. no handler ran.
    SDK::MessageResult result = SDK::MessageResult::PENDING;

    /// `isCompleted()` after the call returned.
    bool completed = false;

    /// Wall-clock across the send call. With a non-zero send timeout this is
    /// how long the kernel took to answer, and a value at the timeout is a
    /// kernel that never did.
    uint32_t elapsedMs = 0;
};

/// The result codes as the SDK spells them, for the results file.
inline const char* resultName(SDK::MessageResult result)
{
    switch (result) {
        case SDK::MessageResult::PENDING: return "PENDING";
        case SDK::MessageResult::SUCCESS: return "SUCCESS";
        case SDK::MessageResult::FAIL:    return "FAIL";
        case SDK::MessageResult::TIMEOUT: return "TIMEOUT";
    }
    return "UNKNOWN";
}

/**
 * @brief Send one REQUEST_BACKLIGHT_SET and report everything observable.
 *
 * @param brightness    0-100 as the message documents it. Known inert on device.
 * @param autoOffMs     The message's own `autoOffTimeoutMs`. 0 is documented as
 *                      "disabled" and is exactly what Suite 2 is testing.
 * @param sendTimeoutMs Handed to `sendMessage`. Non-zero to make `result`
 *                      meaningful. NOT the auto-off; conflating the two is the
 *                      easiest mistake to make with this message.
 *
 * Blocks for up to `sendTimeoutMs`. Called from the service's poll, which is
 * bounded by design, so the caller must keep that value small: see the plan's
 * `kSendTimeoutMs`.
 */
inline Outcome request(const SDK::Kernel& kernel, uint8_t brightness, uint32_t autoOffMs,
                       uint32_t sendTimeoutMs)
{
    Outcome outcome;
    outcome.brightness    = brightness;
    outcome.autoOffMs     = autoOffMs;
    outcome.sendTimeoutMs = sendTimeoutMs;

    auto guard = SDK::make_msg<SDK::Message::RequestBacklightSet>(kernel);
    if (!guard) {
        outcome.allocationFailed = true;
        return outcome;
    }

    guard->brightness       = brightness;
    guard->autoOffTimeoutMs = autoOffMs;

    const uint32_t startedMs = kernel.sys.getTimeMs();
    outcome.sent      = guard.send(sendTimeoutMs);
    outcome.elapsedMs = kernel.sys.getTimeMs() - startedMs;

    // Read back through the guard while it still owns the message: the pool
    // reclaims it on destruction, and reading a released message is reading
    // whatever the next allocation put there.
    outcome.result    = guard->getResult();
    outcome.completed = guard->isCompleted();

    return outcome;
}

} // namespace Backlight

#endif // BACKLIGHT_REQUEST_HPP
