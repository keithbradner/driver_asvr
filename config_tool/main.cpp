#include "CConfigApp.h"

static int RunApp()
{
    CConfigApp l_app;
    if(!l_app.Init())
    {
        l_app.Shutdown();
        return 1;
    }
    while(l_app.Update())
    {
    }
    l_app.Shutdown();
    return 0;
}

// On Windows, Debug builds are console apps (stdout logging), Release is a
// windowed app; elsewhere a plain main always works
#if defined(_WIN32) && !defined(_DEBUG)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    return RunApp();
}
#else
int main(int, char**)
{
    return RunApp();
}
#endif
