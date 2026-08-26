/**
 ******************************************************************************
 * @file    Service.hpp
 * @date    25-07-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   Barcode service: owns the id, the GUI only renders it.
 ******************************************************************************
 */

#ifndef SERVICE_HPP
#define SERVICE_HPP

#include <memory>

#include "SDK/AppConfig/AppConfig.hpp"
#include "SDK/Kernel/Kernel.hpp"

#include "Commands.hpp"
#include "Barcode.hpp"

/**
 * @class Service
 * @brief Background half of the app.
 *
 * There is nothing to time or animate here, so unlike Stopwatch the service
 * does not need to outlive the GUI: it exits as soon as the GUI is gone.
 *
 * It is also the only holder of SDK::AppConfig. That class is documented as
 * one instance per app on one thread -- two of them would share one
 * `<configFile>.tmp` with no locking -- so the GUI never reads the
 * configuration itself; it renders the snapshot this thread sends it.
 */
class Service
{
public:
    Service(SDK::Kernel &kernel);

    virtual ~Service() = default;

    void run();

private:
    SDK::Kernel           &mKernel;
    CustomMessage::Sender  mSender;
    Barcode::State         mState;

    /// Held by pointer, and never built in the constructor: see load().
    std::unique_ptr<SDK::AppConfig> mConfig;

    void handleCommand(SDK::MessageBase *msg);

    /**
     * @brief (Re)read the values file by building a fresh SDK::AppConfig.
     *
     * SDK::AppConfig reads its file once, in its constructor, and exposes no
     * way to re-read; the SDK's own answer is that a change on the phone
     * applies the next time the app is opened. Replacing the object is how
     * this app keeps the behaviour it had before -- edit the file, come back
     * to the screen, see the new id -- using only the public API.
     *
     * The cost is a full open-read-parse where this app used to compare a
     * (size, mtime) stamp and usually do nothing. On a file of a few dozen
     * bytes, on a GUI resume, that is not worth a stat to avoid.
     */
    void load();

    /// Turn whatever the configuration currently holds into a publishable state.
    void adopt();

    void publish();
};

#endif // SERVICE_HPP
