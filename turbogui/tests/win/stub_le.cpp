// Stub named FCLiveEditor.DLL for the Turbo.dll smoke test. It is NOT Live Editor and does nothing;
// it only lets Turbo.dll see a module with that name in the test process.
#include <windows.h>

extern "C" BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }
