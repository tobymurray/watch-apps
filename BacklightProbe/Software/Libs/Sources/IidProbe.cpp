/**
 ******************************************************************************
 * @file    IidProbe.cpp
 * @brief   Six queries, six pointers logged, nothing called.
 ******************************************************************************
 */

#include "IidProbe.hpp"

#include "SDK/Interfaces/IKIP.hpp"

#define LOG_MODULE_PRX      "Iid"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

namespace IidProbe
{

Result run(SDK::Interface::IKIP& kip)
{
    Result result;

    for (size_t i = 0; i < kCount; ++i) {
        const uint32_t iid = kFirst + static_cast<uint32_t>(i) * kStep;

        // The enum is the parameter type and its enumerators do not cover this
        // range, which is the entire point: these are the identifiers the SDK
        // does not name. A scoped enum with a uint32_t underlying type converts
        // back from any uint32_t, so this is well defined: the value is simply
        // not one of the named enumerators.
        void* p = kip.queryInterface(static_cast<SDK::Interface::IKIP::IntfID>(iid));

        result.answers[i].iid     = iid;
        result.answers[i].nonNull = (p != nullptr);
        result.answers[i].value   = reinterpret_cast<uintptr_t>(p);

        if (p != nullptr) {
            ++result.nonNullCount;
            // Logged and left alone. The vtable layout of whatever lives here is
            // unknown; calling slot 0 because IBacklight::on happens to be at
            // slot 0 of IBacklight is how a curiosity becomes a HardFault.
            LOG_INFO("IID %08lX -> %08lX  (NOT called through)\n",
                     static_cast<unsigned long>(iid), static_cast<unsigned long>(result.answers[i].value));
        } else {
            LOG_INFO("IID %08lX -> null\n", static_cast<unsigned long>(iid));
        }
    }

#if defined(SIMULATOR) || !defined(__ARM_ARCH)
    // The simulator's queryInterface is a switch over the five identifiers it
    // implements and returns nullptr for everything else. Six nulls from it is
    // a statement about the simulator and about nothing else, and must not be
    // written down as though it closed Q7.
    result.meaningful = false;
#else
    result.meaningful = true;
#endif

    LOG_INFO("%u of %u unallocated IIDs answered%s\n", static_cast<unsigned>(result.nonNullCount),
             static_cast<unsigned>(kCount), result.meaningful ? "" : " (simulator: means nothing)");

    return result;
}

} // namespace IidProbe
