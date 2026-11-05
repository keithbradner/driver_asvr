#pragma once

// OS seams for the driver: module location, primary-instance election, and
// launching the monitor binary that ships next to the driver module.
namespace Platform
{
    // Directory containing the driver module, without a trailing separator;
    // empty until HmdDriverFactory has run
    std::string GetModuleDirectory();

    // True on the instance that owns the monitor, the control server and pose
    // persistence. On Windows both vrserver and vrcompositor load a driver with
    // a DisplayRedirect device, so the first process to take the named mutex
    // wins; on Linux only vrserver loads the driver and every instance is primary.
    bool AcquirePrimaryGuard();
    void ReleasePrimaryGuard();

    void LaunchMonitor(const std::string &f_directory);
}
