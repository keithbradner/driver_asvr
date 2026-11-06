Artificially Simulated Virtual Reality (ASVR) - virtual SteamVR headset for testing
===================================================================================

Turns any PC into a SteamVR test rig: a virtual headset plus a virtual
controller for each hand, driven with mouse and keyboard from a desktop
window that shows a live view of the VR scene. Works on headless machines
and over Remote Desktop - no monitor, no real headset, no hardware vsync.

Quickstart
----------
1. Extract this whole folder somewhere permanent and user-writable
   (e.g. Documents\driver_asvr; any home directory location on Linux).
   Not inside Program Files.
2. Run asvr_monitor.exe ("./asvr_monitor" on Linux). The ASVR window
   opens with its Settings panel open on first start: click "Install".
3. Click "Start SteamVR" on the main screen - the window offers one
   button per SteamVR install listed in openvrpaths.vrpath - or start
   SteamVR from Steam as usual; the driver then opens the ASVR window
   itself if it is not already open (never a second copy). Click inside
   the view, look around with the mouse, move with WASD.

asvr_monitor.exe is the only program. Its Settings panel, opened with
the hamburger (three-line) button in the top bar, holds Install and
Uninstall, the big driver enable switch, the SteamVR flags, resolution
and refresh rate, the virtual trackers, the key bindings and "Reset
Saved Pose". While SteamVR is not running the main screen says so and
offers the Start buttons; when SteamVR quits, a window the driver opened
closes with it and a window you opened goes back to that screen.

Requirements: Windows 10/11 64-bit, or 64-bit Linux with SteamVR 2.5+
(a desktop session is needed for the window). Steam + SteamVR installed
and run at least once.

If another headset driver claims the HMD slot first (Steam Link does this
even when nothing is streaming), ASVR stays out of its way: the driver
starts neither the virtual headset nor its controllers and trackers, and
it does not open the window. A window you open yourself says which
headset owns the slot and points at the forcedDriver checkbox in
Settings: tick it and restart SteamVR; untick it to hand the slot back.

On Linux asvr_ctl lives in asvr/bin/linux64, the window log is
~/.local/state/asvr/asvr_monitor.log and driver problems are logged in
~/.local/share/Steam/logs/vrserver.txt.

Install registers the asvr driver with SteamVR and applies the
steamvr.vrsettings values headless operation needs (your settings file is
backed up first as steamvr.vrsettings.driver_asvr.bak).

Controls
--------
The controller keys drive one ACTIVE controller at a time - right hand by
default, Tab to switch. The bar at the top of the window always tells you
which hand is active and what the mouse is doing, and the bottom of the
window draws the current bindings on a keyboard (plus the mouse),
colour-coded by what they do, with pressed keys lit up. All of these are rebindable in the
Settings panel; the defaults:

  Mouse          look around
  W A S D        move the head
  PgUp / PgDn    move up / down
  Numpad 4/6 8/2 1/3   head yaw / pitch / roll
  Home           reset the head: origin, looking straight ahead, no roll
  End            reset both controllers to their default offsets
  Tab            switch the active controller (right <-> left)
  Backspace      toggle mouse between head look and aiming the controller
  Left click     trigger
  Right click    grip
  Arrow keys     joystick
  N / M          A / B buttons (X / Y when the left controller is active)
  I J K L        move the active controller forward/left/back/right
  Space          system button (opens/closes the SteamVR dashboard)
  Esc            release the mouse

The controllers follow your head at an offset, like wands held out in
front of you. The headset pose is saved on SteamVR shutdown and restored
next launch; Home resets it while SteamVR is running, and "Reset Saved
Pose" in the Settings panel clears the saved one so the next launch
starts at the origin.

The key bindings strip and the Settings panel can each be collapsed; the
window remembers both states (and its size) across runs in
%LOCALAPPDATA%\asvr\asvr_monitor.json
(Linux: ~/.local/state/asvr/asvr_monitor.json).

Scripting / automation
----------------------
asvr\bin\win64\asvr_ctl.exe drives the headset from the command line
without touching the window: move and rotate the HMD, aim the controllers,
press buttons, and save PNG screenshots of the VR view. Run
"asvr_ctl --help" for the full command list. Requires SteamVR to be
running with the driver enabled.

Notes / troubleshooting
-----------------------
* Want your real headset back? Click the big "DRIVER ENABLED" switch in
  the Settings panel to disable the driver - no need to uninstall.
  (SteamVR's Manage Add-Ons page does the same thing.)
* If the view is a teal void even though a game is running, open the
  Settings panel and make sure allowDisplayLockedMode is checked. RDP
  sessions count as a "locked" computer and SteamVR refuses to show app
  frames without it.
* Mouse look works over Remote Desktop and Chrome Remote Desktop - the
  window handles their absolute-position mouse input automatically (hold
  the pointer against a window edge to keep turning), and the frames reach it through
  shared memory rather than the GPU, so the remote display adapter does
  not matter either. Just keep the RDP session connected while you use
  the window; Windows tears down the remote display when you disconnect
  (the VR side keeps running).
* Driver load problems are logged in Steam\logs\vrserver.txt; window and
  renderer problems in %TEMP%\asvr_monitor.log.

Uninstall
---------
Run asvr_monitor.exe, open Settings (the hamburger button), click
Uninstall, then delete this folder.
