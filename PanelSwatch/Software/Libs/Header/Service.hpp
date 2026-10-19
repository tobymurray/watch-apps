#ifndef SERVICE_HPP
#define SERVICE_HPP

#include "SDK/Kernel/Kernel.hpp"

/// Does nothing but answer the kernel; the pages are all in the GUI.
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
