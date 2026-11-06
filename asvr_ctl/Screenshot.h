#pragma once

// "screenshot <out.png> [--both] [--wait-new-frame]" - argv points past the
// "screenshot" token. "--adapter N" is obsolete but still accepted and ignored.
// Returns a process exit code (0 ok, 2 failure).
int RunScreenshot(int argc, char *argv[]);
