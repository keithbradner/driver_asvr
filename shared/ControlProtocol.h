#pragma once

#include <cstdint>

// Control channel between external tools (asvr_ctl) and the driver's primary
// instance inside vrserver. Byte-mode named pipe, newline-delimited ASCII
// commands, one reply line per command: "ok", "ok <data>" or "err <reason>".
// Commands are queued by the pipe thread and executed on the vrserver thread
// in CServerDriver::RunFrame, so device state only mutates there.
//
// Units: meters and degrees. OpenVR standing universe: +X right, +Y up,
// -Z forward. Poses are "x y z qx qy qz qw". Positive yaw turns right,
// positive pitch looks up.
//
// Commands:
//   ping
//   status
//   hmd_pose x y z qx qy qz qw
//   hmd_rotate yaw pitch [roll]
//   hmd_translate x y z [world|local]
//   ctrl <left|right> offset x y z qx qy qz qw
//   ctrl <left|right> aim yaw pitch
//   ctrl <left|right> button <trigger|grip|system|a|b|x|y> <press|release|click>
//   ctrl <left|right> trigger <0..1>
//   ctrl <left|right> joystick x y
constexpr char g_controlPipeName[] = "\\\\.\\pipe\\driver_asvr_control_v1";
constexpr uint32_t g_controlProtocolVersion = 1U;
constexpr uint32_t g_controlMaxLineLength = 512U;
constexpr uint32_t g_clickReleaseDelayMs = 150U; // "click" auto-release delay

#ifndef _WIN32
#include <cstddef>
#include <cstring>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

// Same protocol over an abstract unix domain socket. Abstract (no filesystem
// entry) because vrserver runs inside the Steam Linux Runtime container with a
// private $XDG_RUNTIME_DIR and /tmp - a path-bound socket created there is
// invisible to the host. The network namespace is shared, abstract sockets
// cross it, and there is no stale file to clean up. Per-uid name so multiple
// users cannot collide.
inline std::string GetControlSocketName()
{
    return "driver_asvr_control_v1." + std::to_string(getuid());
}

// False when the name does not fit (cannot happen with the fixed name above)
inline bool MakeControlSocketAddress(sockaddr_un &f_address, socklen_t &f_length)
{
    const std::string l_name = GetControlSocketName();
    memset(&f_address, 0, sizeof(f_address));
    f_address.sun_family = AF_UNIX;
    if(l_name.size() + 1U > sizeof(f_address.sun_path)) return false;
    // sun_path[0] == '\0' selects the abstract namespace; the name follows
    memcpy(f_address.sun_path + 1, l_name.c_str(), l_name.size());
    f_length = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + 1U + l_name.size());
    return true;
}
#endif
