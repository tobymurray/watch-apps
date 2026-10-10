#ifndef LIFECYCLE_HPP
#define LIFECYCLE_HPP

#include <cstddef>
#include <cstdint>

#include "SDK/Kernel/Kernel.hpp"

namespace Slumber
{

constexpr char kLogDir[]  = "Debug";
constexpr char kLogPath[] = "Debug/lifecycle.log";
constexpr char kLaunchCountPath[] = "Debug/launches.txt";

/// Past this the log restarts, so an app that runs for the device's life
/// cannot fill the volume.
constexpr size_t kLogMaxBytes = 64u * 1024u;

/// Append-only and holds no handle between calls, so a USB unplug that kills
/// the process loses at most the line being written.
class Lifecycle
{
public:
    explicit Lifecycle(SDK::Kernel &kernel)
        : mKernel(kernel)
    {}

    /// One line: `<uptime_ms> <tag> <detail>`.
    void line(const char *tag, const char *fmt = nullptr, ...);

    /// Reads, increments and rewrites the persisted service-start count.
    uint32_t bumpLaunchCount();

private:
    SDK::Kernel &mKernel;
};

} // namespace Slumber

#endif // LIFECYCLE_HPP
