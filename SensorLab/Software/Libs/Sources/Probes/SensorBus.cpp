/**
 ******************************************************************************
 * @file    SensorBus.cpp
 * @date    21-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   Raw sensor-layer access. Rationale is in the header.
 ******************************************************************************
 */

#include "Probes/SensorBus.hpp"

#include "SDK/Messages/MessageGuard.hpp"
#include "SDK/Messages/SensorLayerMessages.hpp"

#define LOG_MODULE_PRX      "Bus"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

namespace SensorLab::Probes
{

SensorBus::SensorBus(const SDK::Kernel &kernel)
    : mKernel(kernel)
{
}

bool SensorBus::requestDefault(SDK::Sensor::Type type, Identity &out)
{
    auto req = SDK::make_msg<SDK::Message::Sensor::RequestDefault>(mKernel);
    if (!req) {
        return false;
    }
    req->id = type;

    // `send()` returns true even when only the *response* timed out, so the
    // result has to be checked separately -- the mistake `SensorConnection.cpp`
    // records having made and fixed.
    if (!req.send(kRequestTimeoutMs) || !req.ok()) {
        return false;
    }

    // Full 32-bit width, straight off the message. A handle above 255 is the
    // case `SDK::Sensor::Connection` truncates, and a profiler subscribing to
    // thirty-seven types is the app most likely to see one.
    out.handle   = req->handle;
    out.resolved = (req->handle != 0);

    // SUCCESS with a zero handle is not a resolution. Recorded as unresolved,
    // because "the kernel said yes and gave me nothing to subscribe to" is the
    // same finding as "the kernel said no" from the app's point of view, and
    // treating it as resolved would produce a connect attempt on handle 0.
    return out.resolved;
}

bool SensorBus::requestList(SDK::Sensor::Type type, Identity &out)
{
    auto req = SDK::make_msg<SDK::Message::Sensor::RequestList>(mKernel);
    if (!req) {
        return false;
    }
    req->id = type;

    if (!req.send(kRequestTimeoutMs) || !req.ok()) {
        // Not answered. Left as `listAnswered == false` rather than as a count
        // of zero: "the kernel does not implement this request" and "this type
        // has no drivers" are different findings and the profile keeps them
        // apart.
        return false;
    }

    out.listAnswered = true;
    out.handleCount  = req->handlesCount;
    // Clamp: the field is a `uint32_t` and the array is ten wide, so a kernel
    // reporting more than it can carry would overrun. Clamped rather than
    // trusted, and the profile still records the *reported* count so the
    // discrepancy is visible.
    const uint32_t copy = (out.handleCount < kMaxHandlesPerType)
                              ? out.handleCount
                              : static_cast<uint32_t>(kMaxHandlesPerType);
    for (uint32_t i = 0; i < copy; i++) {
        out.handles[i] = req->handles[i];
    }
    return true;
}

bool SensorBus::requestDescriptor(uint32_t handle, Identity &out)
{
    auto req = SDK::make_msg<SDK::Message::Sensor::RequestGetDesc>(mKernel);
    if (!req) {
        return false;
    }
    req->handle = handle;

    if (!req.send(kRequestTimeoutMs) || !req.ok()) {
        return false;
    }

    // `desc` is `char[32]` with no guarantee of a terminator, so copy at most
    // 32 and terminate here. A driver name that filled the field exactly would
    // otherwise run into whatever follows it in the pool block.
    size_t i = 0;
    for (; i + 1 < kDescriptorLen && i < sizeof(req->desc)
           && req->desc[i] != '\0'; i++) {
        out.descriptor[i] = req->desc[i];
    }
    out.descriptor[i] = '\0';

    out.descriptorAnswered = (i > 0);
    return out.descriptorAnswered;
}

bool SensorBus::connect(uint32_t handle, float periodMs, uint32_t latencyMs)
{
    if (handle == 0) {
        return false;
    }
    auto req = SDK::make_msg<SDK::Message::Sensor::RequestConnect>(mKernel);
    if (!req) {
        return false;
    }
    req->handle  = handle;
    req->period  = periodMs;
    req->latency = latencyMs;

    return req.send(kRequestTimeoutMs) && req.ok();
}

void SensorBus::disconnect(uint32_t handle)
{
    if (handle == 0) {
        return;
    }
    auto req = SDK::make_msg<SDK::Message::Sensor::RequestDisconnect>(mKernel);
    if (!req) {
        return;
    }
    req->handle = handle;
    // Fire and forget. A connect whose ack timed out can still have registered
    // the listener kernel-side, so disconnecting a handle this app is not sure
    // about is the safe direction -- and disconnecting an unregistered listener
    // is a kernel-side no-op.
    req.send();
}

Identity SensorBus::probe(SDK::Sensor::Type type)
{
    Identity id {};

    if (!requestDefault(type, id)) {
        // No producer. Nothing further to ask: `RequestList` on a type with no
        // default is not obviously meaningful, but it is cheap and it is the
        // only way to find out whether a type has a *non*-default driver -- so
        // it is still sent.
        requestList(type, id);
        return id;
    }

    requestList(type, id);
    requestDescriptor(id.handle, id);

    // Whether `connect()` succeeds is a separate question from whether a handle
    // resolves. Asked, recorded, and immediately let go -- see the header for
    // why the sweep does not hold thirty-seven connections open.
    //
    // Period 0 and latency 0: the sensor layer's own defaults, so this probe
    // asks for nothing in particular and cannot be accused of having changed
    // the sensor's configuration on its way past. What a *requested* period
    // does is layer 6's subject and it gets its own run.
    id.connected = connect(id.handle, 0.0f, 0);
    if (id.connected) {
        disconnect(id.handle);
    }

    return id;
}

} // namespace SensorLab::Probes
