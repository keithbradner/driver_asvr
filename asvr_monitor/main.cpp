#include "stdafx.h"

#include "CMonitorApp.h"

// Debug builds are console apps (stdout logging), Release is a windowed app
#ifdef _DEBUG
int main(void)
#else
int APIENTRY wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int)
#endif
{
    CMonitorApp l_app;
    if(l_app.Init())
    {
        while(l_app.Update())
        {
        }
        l_app.Shutdown();
    }
    return EXIT_SUCCESS;
}
