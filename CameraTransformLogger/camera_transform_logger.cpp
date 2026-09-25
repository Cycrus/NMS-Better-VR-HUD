#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "MinHook.h"

static constexpr uintptr_t CAPTURE_POINT_OFFSET = 0x337927;
static constexpr uintptr_t PLAYER_CAMERA_MATRIX_OFFSET = 0x6E7CA30;
static constexpr ULONGLONG SAMPLE_INTERVAL_MS = 100;

using CapturePointFn = void (*)();

extern "C" void* g_originalCapturePoint;
void* g_originalCapturePoint = nullptr;

static HINSTANCE g_module = nullptr;
static uintptr_t g_moduleBase = 0;
static ULONGLONG g_startTick = 0;
static ULONGLONG g_lastSampleTick = 0;
static volatile LONG g_sampleCount = 0;

static void AppendFileLine(const char* line)
{
    char path[MAX_PATH] = "/home/cyril/Downloads/camera_transform_readings.csv\0";

    HANDLE file = CreateFileA(
        path,
        FILE_APPEND_DATA,
        FILE_SHARE_READ,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (file == INVALID_HANDLE_VALUE)
        return;

    DWORD written = 0;
    WriteFile(file, line, static_cast<DWORD>(std::strlen(line)), &written, nullptr);
    CloseHandle(file);
}

static void EnsureCsvHeader()
{
    char path[MAX_PATH] = "/home/cyril/Downloads/camera_transform_readings.csv\0";

    HANDLE file = CreateFileA(
        path,
        FILE_APPEND_DATA,
        FILE_SHARE_READ,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (file == INVALID_HANDLE_VALUE)
        return;

    LARGE_INTEGER size = {};
    if (GetFileSizeEx(file, &size) && size.QuadPart == 0)
    {
        const char header[] =
            "elapsed_ms,sample,label,self,m00,m01,m02,m03,m10,m11,m12,m13,m20,m21,m22,m23,m30,m31,m32,m33\r\n";
        DWORD written = 0;
        WriteFile(file, header, sizeof(header) - 1, &written, nullptr);
    }

    CloseHandle(file);
}

static void AppendMatrixCsv(
    ULONGLONG elapsed,
    LONG sample,
    const char* label,
    const void* source,
    const float* matrix
)
{
    if (!matrix)
        return;

    char line[512];
    std::snprintf(
        line,
        sizeof(line),
        "%llu,%ld,%s,%p,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g\r\n",
        elapsed,
        sample,
        label,
        source,
        matrix[0],
        matrix[1],
        matrix[2],
        matrix[3],
        matrix[4],
        matrix[5],
        matrix[6],
        matrix[7],
        matrix[8],
        matrix[9],
        matrix[10],
        matrix[11],
        matrix[12],
        matrix[13],
        matrix[14],
        matrix[15]
    );

    AppendFileLine(line);
}

extern "C" void CapturePlayerCameraMatrix()
{
    ULONGLONG now = GetTickCount64();
    if (now - g_lastSampleTick < SAMPLE_INTERVAL_MS)
        return;

    g_lastSampleTick = now;

    const float* matrix = reinterpret_cast<const float*>(g_moduleBase + PLAYER_CAMERA_MATRIX_OFFSET);
    ULONGLONG elapsed = now - g_startTick;
    LONG sample = InterlockedIncrement(&g_sampleCount);

    AppendMatrixCsv(
        elapsed,
        sample,
        "player_camera_matrix_6e7ca30",
        matrix,
        matrix
    );
}

extern "C" __attribute__((naked)) void HookCapturePoint()
{
    __asm__ __volatile__(
        ".intel_syntax noprefix\n"
        "pushfq\n"
        "push rax\n"
        "push rcx\n"
        "push rdx\n"
        "push rbx\n"
        "push rbp\n"
        "push rsi\n"
        "push rdi\n"
        "push r8\n"
        "push r9\n"
        "push r10\n"
        "push r11\n"
        "push r12\n"
        "push r13\n"
        "push r14\n"
        "push r15\n"
        "sub rsp, 0x100\n"
        "movdqu [rsp + 0x00], xmm0\n"
        "movdqu [rsp + 0x10], xmm1\n"
        "movdqu [rsp + 0x20], xmm2\n"
        "movdqu [rsp + 0x30], xmm3\n"
        "movdqu [rsp + 0x40], xmm4\n"
        "movdqu [rsp + 0x50], xmm5\n"
        "movdqu [rsp + 0x60], xmm6\n"
        "movdqu [rsp + 0x70], xmm7\n"
        "movdqu [rsp + 0x80], xmm8\n"
        "movdqu [rsp + 0x90], xmm9\n"
        "movdqu [rsp + 0xa0], xmm10\n"
        "movdqu [rsp + 0xb0], xmm11\n"
        "movdqu [rsp + 0xc0], xmm12\n"
        "movdqu [rsp + 0xd0], xmm13\n"
        "movdqu [rsp + 0xe0], xmm14\n"
        "movdqu [rsp + 0xf0], xmm15\n"
        "sub rsp, 0x20\n"
        "call CapturePlayerCameraMatrix\n"
        "add rsp, 0x20\n"
        "movdqu xmm0, [rsp + 0x00]\n"
        "movdqu xmm1, [rsp + 0x10]\n"
        "movdqu xmm2, [rsp + 0x20]\n"
        "movdqu xmm3, [rsp + 0x30]\n"
        "movdqu xmm4, [rsp + 0x40]\n"
        "movdqu xmm5, [rsp + 0x50]\n"
        "movdqu xmm6, [rsp + 0x60]\n"
        "movdqu xmm7, [rsp + 0x70]\n"
        "movdqu xmm8, [rsp + 0x80]\n"
        "movdqu xmm9, [rsp + 0x90]\n"
        "movdqu xmm10, [rsp + 0xa0]\n"
        "movdqu xmm11, [rsp + 0xb0]\n"
        "movdqu xmm12, [rsp + 0xc0]\n"
        "movdqu xmm13, [rsp + 0xd0]\n"
        "movdqu xmm14, [rsp + 0xe0]\n"
        "movdqu xmm15, [rsp + 0xf0]\n"
        "add rsp, 0x100\n"
        "pop r15\n"
        "pop r14\n"
        "pop r13\n"
        "pop r12\n"
        "pop r11\n"
        "pop r10\n"
        "pop r9\n"
        "pop r8\n"
        "pop rdi\n"
        "pop rsi\n"
        "pop rbp\n"
        "pop rbx\n"
        "pop rdx\n"
        "pop rcx\n"
        "pop rax\n"
        "popfq\n"
        "jmp qword ptr [rip + g_originalCapturePoint]\n"
        ".att_syntax prefix\n"
    );
}

static DWORD WINAPI InstallHookThread(LPVOID)
{
    Sleep(2000);

    g_moduleBase = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    void* target = reinterpret_cast<void*>(g_moduleBase + CAPTURE_POINT_OFFSET);

    MH_STATUS status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED)
        return 0;

    status = MH_CreateHook(
        target,
        reinterpret_cast<void*>(&HookCapturePoint),
        reinterpret_cast<void**>(&g_originalCapturePoint)
    );

    if (status != MH_OK)
        return 0;

    status = MH_EnableHook(target);
    if (status != MH_OK)
        return 0;

    EnsureCsvHeader();
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_module = module;
        g_startTick = GetTickCount64();
        DisableThreadLibraryCalls(module);

        HANDLE thread = CreateThread(nullptr, 0, InstallHookThread, nullptr, 0, nullptr);
        if (thread)
            CloseHandle(thread);
    }

    return TRUE;
}
