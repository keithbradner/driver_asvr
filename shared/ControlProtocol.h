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
