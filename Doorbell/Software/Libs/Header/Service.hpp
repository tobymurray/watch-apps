#ifndef SERVICE_HPP
#define SERVICE_HPP

#include "SDK/Kernel/Kernel.hpp"

/**
 * @brief The half that rings on the GUI's behalf.
 *
 * Every SDK example sends CommandAppNewActivity from its service after writing
 * a .fit, so the service path is the one with known-good precedent. It exists
 * here so a run can compare it against the GUI sending the same message itself.
 */
class Service
{
public:
    explicit Service(SDK::Kernel &kernel);
    virtual ~Service() = default;

    void run();

private:
    void ring(uint32_t seq);

    SDK::Kernel &mKernel;
};

#endif // SERVICE_HPP
