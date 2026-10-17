#include "CMonitorAppGlfw.h"

int main(int /*argc*/, char** /*argv*/)
{
    CMonitorAppGlfw l_app;
    if(!l_app.Init()) return 1;
    while(l_app.Update())
    {
    }
    l_app.Shutdown();
    return 0;
}
