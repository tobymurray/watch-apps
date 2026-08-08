/**
 ******************************************************************************
 * @file    SendMsg.hpp
 * @brief   One-shot message send, for an SDK that does not have one yet.
 ******************************************************************************
 *
 * The SDK grew `SDK::send_msg` -- allocate a message, send it, release it on
 * return -- after the 1.3 release, along with the variadic forwarding in
 * `make_msg` and `IAppComm::allocateMessage` that lets a message be built with
 * constructor arguments. MapManager targets 1.3, where `MessageGuard` and the
 * nullary `make_msg` exist but that last step does not, so it carries its own.
 *
 * `MapManager::sendMsg<T>(kernel)` is the whole of what this app needs from that
 * SDK addition; a message with a payload is allocated through `make_msg` and
 * filled in through the guard, which is the 1.3 way of saying the same thing.
 *
 * If MapManager is ever moved to an SDK of 1.4 or later, this header can be deleted
 * and the call sites pointed at `SDK::send_msg`, which behaves identically.
 *
 ******************************************************************************
 */

#ifndef SENDMSG_HPP
#define SENDMSG_HPP

#include "SDK/Kernel/Kernel.hpp"
#include "SDK/Messages/MessageGuard.hpp"

namespace MapManager
{

/**
 * @brief Allocate a default-constructed T, send it, and release it on return.
 * @return Whether the message was accepted for delivery; false if the kernel
 *         pool had nothing to give.
 *
 * Fire-and-forget: it posts with a zero timeout and does not wait for a reply.
 */
template<typename T>
bool sendMsg(const SDK::Kernel &kernel)
{
    auto guard = SDK::make_msg<T>(kernel);
    if (!guard) {
        return false;
    }

    return guard.send();
}

} // namespace MapManager

#endif // SENDMSG_HPP
