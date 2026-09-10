#ifndef PANICKIT_HOSTPANIC_HPP
#define PANICKIT_HOSTPANIC_HPP

// The C++ half of PanicKit: the symbol the Rust panic handler calls, and one
// macro that defines it.
//
// Header-only so it type-checks on a host with
//   clang++ -fsyntax-only -std=c++17 \
//       -I"$UNA_SDK/Libs/Header" -I PanicKit/Header
// which catches a rename across the C ABI without an ARM toolchain.

#include <cstdint>

#include "SDK/Kernel/Kernel.hpp"
#include "SDK/Kernel/KernelProviderGUI.hpp"

extern "C" {

/// Called by PanicKit's Rust panic handler. Must not return normally.
void panickit_host_panic(const uint8_t* msg, uint32_t len);
}

/// Defines `panickit_host_panic` to log and exit, for a GUI process.
///
/// A macro because exactly one translation unit must define it. A Service half
/// reaches a different kernel provider, so it defines the symbol itself rather
/// than taking this.
#define PANICKIT_DEFINE_HOST_PANIC(LOG_ERROR_FN)                                  \
    extern "C" void panickit_host_panic(const uint8_t* msg, uint32_t len)         \
    {                                                                             \
        LOG_ERROR_FN("Rust panic: %.*s\n", static_cast<int>(len),                 \
                     reinterpret_cast<const char*>(msg));                         \
        SDK::KernelProviderGUI::GetInstance().getKernel().sys.exit(1);             \
        for (;;) {                                                                \
        }                                                                         \
    }

#endif  // PANICKIT_HOSTPANIC_HPP
