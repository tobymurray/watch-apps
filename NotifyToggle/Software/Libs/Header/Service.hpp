#ifndef SERVICE_HPP
#define SERVICE_HPP

#include "SDK/Kernel/Kernel.hpp"

// The toggle is read, drawn, flipped, persisted and applied entirely from the
// GUI process -- there is no sensor and nothing for a Service->GUI message to
// carry. This half exists only because the SDK's entry point requires both
// halves to be present.
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
