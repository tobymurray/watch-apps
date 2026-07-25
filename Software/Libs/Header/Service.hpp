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

#include "SDK/Kernel/Kernel.hpp"

#include "Commands.hpp"
#include "Barcode.hpp"

/**
 * @class Service
 * @brief Background half of the app.
 *
 * There is nothing to time or animate here, so unlike Stopwatch the service
 * does not need to outlive the GUI: it exits as soon as the GUI is gone.
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

    void handleCommand(SDK::MessageBase *msg);

    void publish();
};

#endif // SERVICE_HPP
