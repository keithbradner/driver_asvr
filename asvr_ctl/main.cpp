#include "stdafx.h"

#include "Screenshot.h"
#include "PipeClient.h"

// Command-line control for the ASVR driver: sends one command line over the
// control channel and prints the driver's reply. `screenshot` and `frame` are
// handled locally from the shared frame block instead.
// Exit codes: 0 = ok, 1 = driver replied "err ...", 2 = transport/local failure.

static const char g_usage[] =
"asvr_ctl - control the ASVR virtual headset\n"
"\n"
"Usage: asvr_ctl <command> [args...]\n"
"       asvr_ctl -              read commands from stdin, one per line\n"
"\n"
"Units: meters and degrees. OpenVR axes: +X right, +Y up, -Z forward.\n"
"Poses are 'x y z qx qy qz qw'. Positive yaw turns right, positive pitch up.\n"
"\n"
"Driver commands (sent over the control channel):\n"
"  ping                                        liveness check\n"
"  status                                      HMD pose + controller offsets\n"
"  hmd_pose <x y z qx qy qz qw>                absolute HMD pose\n"
"  hmd_rotate <yaw> <pitch> [roll]             relative rotation\n"
"  hmd_translate <x y z> [world|local]         relative move (default local;\n"
"                                              forward = 0 0 -0.5)\n"
"  ctrl <left|right> offset <x y z qx qy qz qw>  head-relative controller pose\n"
"  ctrl <left|right> aim <yaw> <pitch>         aim the controller laser\n"
"  ctrl <left|right> button <name> <press|release|click>\n"
"                                              name: trigger grip system a b x y\n"
"                                              (a/b right hand, x/y left hand)\n"
"  ctrl <left|right> trigger <0..1>            analog trigger\n"
"  ctrl <left|right> joystick <x> <y>          persists until changed\n"
"\n"
"Local commands (read the shared frame block):\n"
"  frame                                       frame counter / resolution\n"
"  screenshot <out.png> [--both] [--wait-new-frame] [--adapter N]\n"
"                                              save the HMD view (left eye;\n"
"                                              --both keeps both eyes)\n";

int main(int argc, char *argv[])
{
    if(argc < 2)
    {
        fputs(g_usage, stdout);
        return 2;
    }

    const std::string l_first(argv[1]);
    if(!l_first.compare("--help") || !l_first.compare("-h") || !l_first.compare("help"))
    {
        fputs(g_usage, stdout);
        return 0;
    }
    if(!l_first.compare("screenshot")) return RunScreenshot(argc - 2, argv + 2);
    if(!l_first.compare("frame")) return RunFrameInfo();

    std::vector<std::string> l_lines;
    if(!l_first.compare("-"))
    {
        char l_buffer[1024];
        bool l_firstLine = true;
        while(fgets(l_buffer, sizeof(l_buffer), stdin))
        {
            std::string l_line(l_buffer);
            // PowerShell pipes tend to prepend a UTF-8 BOM
            if(l_firstLine && (l_line.compare(0U, 3U, "\xEF\xBB\xBF") == 0)) l_line.erase(0U, 3U);
            l_firstLine = false;
            while(!l_line.empty() && ((l_line.back() == '\n') || (l_line.back() == '\r'))) l_line.pop_back();
            if(!l_line.empty()) l_lines.push_back(l_line);
        }
    }
    else
    {
        std::string l_line;
        for(int i = 1; i < argc; i++)
        {
            if(i > 1) l_line.push_back(' ');
            l_line.append(argv[i]);
        }
        l_lines.push_back(l_line);
    }
    return RunPipeSession(l_lines);
}
