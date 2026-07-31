#pragma once

// "screenshot <out.png> [--both] [--wait-new-frame] [--adapter N]" - argv points
// past the "screenshot" token. Returns a process exit code (0 ok, 2 failure).
int RunScreenshot(int argc, char *argv[]);
