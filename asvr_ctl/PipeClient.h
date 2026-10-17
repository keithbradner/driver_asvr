#pragma once

#include <string>
#include <vector>

// Control-channel transport, implemented per platform (named pipe on Windows,
// unix socket on Linux). Sends each line, prints each reply.
// Returns a process exit code: 0 ok, 1 driver replied "err ...", 2 transport failure.
int RunPipeSession(const std::vector<std::string> &f_lines);

// "frame" - prints the shared frame block's counters. 0 ok, 2 failure.
int RunFrameInfo();
