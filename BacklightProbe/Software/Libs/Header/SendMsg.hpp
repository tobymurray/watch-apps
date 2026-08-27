/**
 ******************************************************************************
 * @file    SendMsg.hpp
 * @brief   One-shot command send, for an SDK that does not have one yet.
 ******************************************************************************
 *
 * The SDK grew `SDK::send_msg`; allocate, send, release on return; after the
 * 1.3 release, along with the variadic forwarding in `make_msg` that lets a
 * message be built with constructor arguments. This app targets 1.3 (see the
 * README), where `MessageGuard` and the nullary `make_msg` exist but that last
 * step does not, so it carries its own. Same header FwDump and MapManager carry,
 * for the same reason.
 *
 * If this app is ever moved to an SDK of 1.4 or later, this header can be
 * deleted and its call sites pointed at `SDK::send_msg`.
 *
 ******************************************************************************
 */

#ifndef SENDMSG_HPP
#define SENDMSG_HPP

#include "SDK/Kernel/Kernel.hpp"
#include "SDK/Messages/MessageGuard.hpp"

#include "Commands.hpp"

namespace Probe
{

/**
 * @brief Send a bare command with no payload.
 * @return Whether the message was accepted for delivery.
 *
 * Fire-and-forget. A dropped Start is not silently fatal: the button is still
 * there, and the screen will not have moved off Idle to suggest otherwise.
 */
inline bool sendCommand(const SDK::Kernel& kernel, CustomMessage::Command command)
{
    auto guard = SDK::make_msg<CustomMessage::ProbeCommand>(kernel);
    if (!guard) {
        return false;
    }

    guard->command = static_cast<uint8_t>(command);
    return guard.send();
}

/**
 * @brief Report back what a GUI-sent backlight request did.
 *
 * `seq` echoes the `guiSendSeq` that asked for it, so a reply that arrives after
 * the service has moved on is dropped rather than attributed to a later step.
 */
inline bool sendGuiSendResult(const SDK::Kernel& kernel, uint32_t seq, bool sent,
                              bool allocFailed, bool completed, uint8_t result,
                              uint32_t elapsedMs)
{
    auto guard = SDK::make_msg<CustomMessage::ProbeCommand>(kernel);
    if (!guard) {
        return false;
    }

    guard->command     = static_cast<uint8_t>(CustomMessage::Command::GuiSendResult);
    guard->seq         = seq;
    guard->sent        = sent;
    guard->allocFailed = allocFailed;
    guard->completed   = completed;
    guard->result      = result;
    guard->elapsedMs   = elapsedMs;
    return guard.send();
}

} // namespace Probe

#endif // SENDMSG_HPP
