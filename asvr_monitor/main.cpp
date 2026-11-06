#include "CMonitorApp.h"
#include "CPlatform.h"

#include <cstring>

// Entry point on both platforms. The Windows Release build is a GUI-subsystem
// executable that still enters here (/ENTRY:mainCRTStartup), so there is one
// main and one argv handling. "--launched-by-driver" marks the copy the driver
// starts with SteamVR: it closes again when SteamVR does, while a window the
// user opened stays and goes back to the start screen.
int main(int argc, char **argv)
{
    bool l_launchedByDriver = false;
    for(int i = 1; i < argc; i++)
    {
        if(!strcmp(argv[i], "--launched-by-driver")) l_launchedByDriver = true;
    }

    // One window per user; a second copy (the driver's, or a double-click)
    // only brings the first one forward. A double-click also tells a window
    // the driver opened that the user wants it, so it outlives SteamVR
    if(!CPlatform::AcquireSingleInstance())
    {
        if(!l_launchedByDriver) CPlatform::SignalExistingInstance();
        CPlatform::RaiseExistingInstance();
        return 0;
    }

    int l_result = 0;
    {
        CMonitorApp l_app(l_launchedByDriver);
        if(l_app.Init())
        {
            while(l_app.Update())
            {
            }
        }
        else l_result = 1;
        l_app.Shutdown();
    }
    CPlatform::ReleaseSingleInstance();
    return l_result;
}
