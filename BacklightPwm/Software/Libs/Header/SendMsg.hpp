/**
 ******************************************************************************
 * @file    SendMsg.hpp
 * @brief   One-shot command send, for an SDK that does not have one yet.
 ******************************************************************************
 *
 * The SDK grew `SDK::send_msg` after the 1.3 release. This app targets 1.3 (see
 * the README), where `MessageGuard` and the nullary `make_msg` exist but that
 * one-shot does not, so it carries its own. Same header FwDump, MapManager and
 * BacklightProbe carry, for the same reason.
 *
 * If this app is ever moved to an SDK of 1.4 or later, delete this and point its
 * call sites at `SDK::send_msg`.
 *
 ******************************************************************************
 */

#ifndef SENDMSG_HPP
#define SENDMSG_HPP

#include "SDK/Kernel/Kernel.hpp"
#include "SDK/Messages/MessageGuard.hpp"

#include "Commands.hpp"

namespace Pwm
{

/**
 * @brief Send a command to the service.
 * @return Whether the message was accepted for delivery.
 *
 * Fire-and-forget. A dropped Start is not silently fatal: the button is still
 * there and the screen will not have moved off Idle to suggest otherwise.
 *
 * A dropped **Stop** matters more, since it is the control that gives the pin
 * back. It is not the only thing that does, though: finishing the ladder and
 * `COMMAND_APP_STOP` both hand the pin over too, so a lost Stop delays the
 * handover rather than preventing it.
 */
inline bool sendCommand(const SDK::Kernel& kernel, CustomMessage::Command command)
{
    auto guard = SDK::make_msg<CustomMessage::PwmCommand>(kernel);
    if (!guard) {
        return false;
    }

    guard->command = static_cast<uint8_t>(command);
    return guard.send();
}

} // namespace Pwm

#endif // SENDMSG_HPP
