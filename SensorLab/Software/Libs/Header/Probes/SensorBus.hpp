/**
 ******************************************************************************
 * @file    SensorBus.hpp
 * @date    21-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   The sensor layer at full width, including the two requests nobody
 *          has ever sent.
 ******************************************************************************
 *
 * ---------------------------------------------------------------------------
 * Why this app does not use `SDK::Sensor::Connection`
 *
 * Three reasons, and each of them is a finding in its own right.
 *
 * **1. `Connection` stores the handle as `uint8_t`.**
 * `RequestDefault::handle` is a `uint32_t` and `matchesDriver()` takes a
 * `uint16_t`. Any handle above 255 truncates silently on the way in, and the
 * app then routes every arriving batch by comparing a truncated handle against
 * an untruncated one -- which either drops every sample or, worse, attributes
 * one sensor's samples to another. On this firmware the handles observed so far
 * are small, so nothing has hit it. A profiler is the first app that will
 * subscribe to *all thirty-seven types at once*, which is exactly the
 * circumstance that produces a large handle. So this app records every handle at
 * full 32-bit width, from the raw message, and never lets `Connection` be its
 * only record of one.
 *
 * **2. `Connection` only ever sends `RequestDefault`.**
 * `RequestList` (up to ten handles per type) and `RequestGetDesc` (a 32-char
 * descriptor string) are declared in `SensorLayerMessages.hpp`, implemented in
 * the simulator's dispatcher, and **used by no app in either repository**. They
 * are the cheapest existence and identity probe available, and layer 1 is what
 * they were for. `RequestGetDesc` in particular is the kernel naming its own
 * driver -- the closest thing to an authoritative part identification an app can
 * obtain, and the only app-side check on the hardware inventory's I2C sweeps.
 *
 * **3. `Connection::connect(period, latency)` rejects parameter updates while
 * connected.** A period sweep must therefore disconnect between points, and each
 * point includes a resubscribe. That is part of the method rather than an
 * implementation detail, so it belongs in the run manifest -- and a wrapper that
 * hid the disconnect would have hidden it.
 *
 * There is also a fourth, quieter reason: `Connection` resolves its kernel
 * through `SDK::KernelProviderService::GetInstance()`, a singleton. Talking to
 * the sensor layer through an injected `SDK::Kernel&` instead is what lets the
 * whole subscription path run under `RunHarness` with a scripted queue.
 *
 * ---------------------------------------------------------------------------
 * What this is not
 *
 * Not a replacement for `Connection` and not a proposal to change it. It is one
 * app reaching past a convenience wrapper because it needs the full contract.
 * The handle-truncation and unused-request findings belong upstream as issues;
 * see `SensorLab/Docs/FINDINGS.md`, and note that this repository does not post
 * them.
 *
 ******************************************************************************
 */

#ifndef SENSORLAB_SENSORBUS_HPP
#define SENSORLAB_SENSORBUS_HPP

#include <cstddef>
#include <cstdint>

#include "SDK/Kernel/Kernel.hpp"
#include "SDK/SensorLayer/SensorTypes.hpp"

namespace SensorLab::Probes
{

/// Handles `RequestList` can return per type. The message's own array width.
constexpr size_t kMaxHandlesPerType = 10;

/// `RequestGetDesc::desc` plus a terminator.
constexpr size_t kDescriptorLen = 33;

/// Response timeout for every sensor-layer request, milliseconds.
///
/// A hundred, which is what `SDK::Sensor::Connection` uses for its own
/// resolve and connect. Matched deliberately: a profiler that used a different
/// timeout from every other app on the device would be measuring its own
/// patience rather than the kernel's latency.
constexpr uint32_t kRequestTimeoutMs = 100;

/// What layer 1 learned about one type, at full width.
struct Identity
{
    /// `RequestDefault` returned SUCCESS with a non-zero handle.
    bool     resolved = false;
    /// The default handle, at the message's own 32-bit width.
    uint32_t handle   = 0;

    /// `RequestList` was answered. False means the kernel does not implement it
    /// -- which would itself be news, since it is declared in the app-facing
    /// header.
    bool     listAnswered = false;
    uint32_t handleCount  = 0;
    uint32_t handles[kMaxHandlesPerType] {};

    /// `RequestGetDesc` was answered with a non-empty string.
    bool descriptorAnswered = false;
    char descriptor[kDescriptorLen] {};

    /// `RequestConnect` succeeded. A separate question from resolving, and the
    /// distinction matters: a type can resolve a handle and refuse a connection.
    bool connected = false;
};

/**
 * @brief Raw sensor-layer access with full-width handles.
 *
 * Holds no per-type state beyond the connected-handle table, because the claim
 * store is where state lives. Every method is a bounded request/response: with a
 * non-zero timeout `sendMessage` returns only once the kernel has filled the
 * message in place, so none of these can hang.
 */
class SensorBus
{
public:
    explicit SensorBus(const SDK::Kernel &kernel);

    /// `RequestDefault`. Fills @p out's `resolved` and `handle`.
    bool requestDefault(SDK::Sensor::Type type, Identity &out);

    /// `RequestList`. Never sent by any app before this one.
    bool requestList(SDK::Sensor::Type type, Identity &out);

    /// `RequestGetDesc` for @p handle. Never sent by any app before this one.
    bool requestDescriptor(uint32_t handle, Identity &out);

    /// `RequestConnect`. @p periodMs and @p latencyMs are what is *asked for*;
    /// what is delivered is layer 6's whole subject.
    bool connect(uint32_t handle, float periodMs, uint32_t latencyMs);

    /// `RequestDisconnect`. Safe on a handle that was never connected: the
    /// kernel-side no-op is documented in `SensorConnection.cpp`.
    void disconnect(uint32_t handle);

    /// Everything layer 1 can learn about one type, in one call: resolve,
    /// enumerate, name, then connect and immediately disconnect.
    ///
    /// The connect-and-drop is deliberate. "Does `connect()` then succeed" is a
    /// separate question from "does a handle resolve", and the only way to
    /// answer it is to try -- but leaving thirty-seven connections open while
    /// the sweep runs would make the sweep itself the heaviest thing the device
    /// has ever done, and would make every subsequent measurement a measurement
    /// of that. So the sweep connects, records, and lets go.
    Identity probe(SDK::Sensor::Type type);

private:
    const SDK::Kernel &mKernel;
};

} // namespace SensorLab::Probes

#endif // SENSORLAB_SENSORBUS_HPP
