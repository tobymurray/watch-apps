/**
 ******************************************************************************
 * @file    SendMsg.hpp
 * @brief   One-shot message send, for an SDK that does not have one yet.
 ******************************************************************************
 *
 * The SDK grew `SDK::send_msg` -- allocate a message, send it, release it on
 * return -- after the 1.3 release, along with the variadic forwarding in
 * `make_msg` and `IAppComm::allocateMessage` that lets a message be built with
 * constructor arguments. FwDump targets 1.3 (see the README), where
 * `MessageGuard` and the nullary `make_msg` exist but that last step does not,
 * so it carries its own. Same header MapManager carries, for the same reason.
 *
 * If FwDump is ever moved to an SDK of 1.4 or later, this header can be deleted
 * and the one call site pointed at `SDK::send_msg`.
 *
 ******************************************************************************
 */

#ifndef SENDMSG_HPP
#define SENDMSG_HPP

#include "SDK/Kernel/Kernel.hpp"
#include "SDK/Messages/MessageGuard.hpp"

#include "Commands.hpp"

namespace FwDump
{

/**
 * @brief Allocate a command message, fill it in, send it, release it on return.
 * @return Whether the message was accepted for delivery; false if the kernel
 *         pool had nothing to give.
 *
 * Fire-and-forget: it posts with a zero timeout and does not wait for a reply.
 * A dropped Start is not silently fatal -- the button is still there to press
 * again, and the screen will not have moved off Idle to suggest otherwise.
 */
inline bool sendCommand(const SDK::Kernel& kernel, CustomMessage::DumpCommand command)
{
    auto guard = SDK::make_msg<CustomMessage::FwDumpCommand>(kernel);
    if (!guard) {
        return false;
    }

    guard->command = static_cast<uint8_t>(command);
    return guard.send();
}

} // namespace FwDump

#endif // SENDMSG_HPP
