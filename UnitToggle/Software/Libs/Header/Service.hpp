#ifndef SERVICE_HPP
#define SERVICE_HPP

#include "SDK/Kernel/Kernel.hpp"

// This half exists only because the SDK's entry point requires both halves.
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
