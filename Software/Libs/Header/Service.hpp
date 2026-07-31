/**
 ******************************************************************************
 * @file    Service.hpp
 * @brief   Minimal Service half of the RustGuiPoc app.
 *
 * Every .uapp has a Service ELF as well as a GUI ELF. This PoC is a pure
 * frontend demo, so the service holds no state — it just stays alive while the
 * app is up and exits when the GUI goes away.
 ******************************************************************************
 */
#ifndef SERVICE_HPP
#define SERVICE_HPP

#include "SDK/Kernel/Kernel.hpp"

class Service
{
public:
    explicit Service(SDK::Kernel &kernel);
    virtual ~Service() = default;

    void run();

private:
    SDK::Kernel &mKernel;
};

#endif // SERVICE_HPP
