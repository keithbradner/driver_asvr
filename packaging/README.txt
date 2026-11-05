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
2. Run Config.exe ("./Config" on Linux) and click "Install".
3. Start SteamVR. Click inside the "ASVR" window that opens,
   look around with the mouse, move with WASD.

Requirements: Windows 10/11 64-bit, or 64-bit Linux with SteamVR 2.5+
(a desktop session is needed for the monitor window). Steam + SteamVR
installed and run at least once.

If another headset driver keeps claiming the HMD slot (Steam Link does this
even when nothing is streaming), tick "forcedDriver" in Config and restart
SteamVR; untick it to hand the slot back.

On Linux the tools live in asvr/bin/linux64 (asvr_ctl, asvr_monitor), the
monitor log is ~/.local/state/asvr/asvr_monitor.log and driver problems are
logged in ~/.local/share/Steam/logs/vrserver.txt.

Install registers the asvr driver with SteamVR and applies the
steamvr.vrsettings values headless operation needs (your settings file is
backed up first as steamvr.vrsettings.driver_asvr.bak). Config.exe
is also where everything lives afterwards: a big enable/disable switch for
the driver, the SteamVR flags, resolution and refresh rate, the saved
headset pose, and all the key bindings below.

Controls
--------
The controller keys drive one ACTIVE controller at a time - right hand by
default, Tab to switch. The window title always tells you which hand is
active and what the mouse is doing. All of these are rebindable in
Config.exe; the defaults:

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
Pose" in Config.exe clears the saved one so the next launch starts at the
origin.

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
  Config.exe to disable the driver - no need to uninstall. (SteamVR's
  Manage Add-Ons page does the same thing.)
* If the view is a teal void even though a game is running, open
  Config.exe and make sure allowDisplayLockedMode is checked. RDP
  sessions count as a "locked" computer and SteamVR refuses to show app
  frames without it.
* Mouse look works over Remote Desktop - the monitor handles RDP's
  absolute-position mouse input automatically. Just keep the RDP session
  connected while you use the window; Windows tears down the remote
  display when you disconnect (the VR side keeps running).
* Driver load problems are logged in Steam\logs\vrserver.txt; window and
  renderer problems in %TEMP%\asvr_monitor.log.

Uninstall
---------
Run Config.exe, click Uninstall, then delete this folder.
