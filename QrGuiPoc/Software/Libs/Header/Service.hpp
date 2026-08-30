#ifndef SERVICE_HPP
#define SERVICE_HPP

#include "SDK/Kernel/Kernel.hpp"

// No sensor, no config, no Service -> GUI messages: the id the GUI process
// draws is a compile-time constant (see Gui.cpp), so there is nothing for this
// half to compute or forward. It exists only because the SDK's entry point
// requires both halves to be present.
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
