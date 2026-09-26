/* A small plugin to test DLL side loading using Ultimate ASI loader.
 * When a file called MyNmsMod-loaded.txt appears in the NMS.exe directory after the game starts, the sideloading works.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static void WriteLoadedFile()
{
    const char message[] =
        "My NMS ASI plugin was loaded successfully!\r\n";

    HANDLE file = CreateFileA(
        "MyNmsMod-loaded.txt",
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (file == INVALID_HANDLE_VALUE)
        return;

    DWORD written = 0;

    WriteFile(
        file,
        message,
        sizeof(message) - 1,
        &written,
        nullptr
    );

    CloseHandle(file);
}

BOOL WINAPI DllMain(
    HINSTANCE module,
    DWORD reason,
    LPVOID reserved
)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(module);

        WriteLoadedFile();
    }

    return TRUE;
}