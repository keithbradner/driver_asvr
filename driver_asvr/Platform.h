#pragma once

// OS seams for the driver: module location and launching the monitor binary
// that ships with the driver.
namespace Platform
{
    // Directory containing the driver module, without a trailing separator;
    // empty until HmdDriverFactory has run
    std::string GetModuleDirectory();

    // Starts asvr_monitor with "--launched-by-driver", looking next to the
    // driver module first (dev tree: both land in bin/<platform>) and then at
    // the package root three levels up (shipped layout: the monitor sits next
    // to README.txt, the driver in asvr/bin/<platform>). Logs the outcome.
    void LaunchMonitor(const std::string &f_directory);
}
