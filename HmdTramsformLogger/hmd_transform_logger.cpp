  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  #include <cstdint>
  #include <cstdio>

  #include "openvr.h"

  static constexpr uintptr_t kCompositorGlobalOffset = 0x70F45E8;

  using WaitGetPosesFn = vr::EVRCompositorError(__fastcall*)(
      void* self,
      vr::TrackedDevicePose_t* renderPoses,
      uint32_t renderPoseCount,
      vr::TrackedDevicePose_t* gamePoses,
      uint32_t gamePoseCount
  );

  static WaitGetPosesFn g_originalWaitGetPoses = nullptr;
  static vr::HmdMatrix34_t g_hmdTransform = {};
  static volatile LONG g_hasHmdTransform = 0;

  static void LogLine(const char* text)
  {
      FILE* file = nullptr;
      fopen_s(&file, "MyNmsMod-hmd.txt", "a");
      if (!file)
          return;

      fputs(text, file);
      fclose(file);
  }

  static vr::EVRCompositorError __fastcall HookWaitGetPoses(
      void* self,
      vr::TrackedDevicePose_t* renderPoses,
      uint32_t renderPoseCount,
      vr::TrackedDevicePose_t* gamePoses,
      uint32_t gamePoseCount
  )
  {
      vr::EVRCompositorError result = g_originalWaitGetPoses(
          self,
          renderPoses,
          renderPoseCount,
          gamePoses,
          gamePoseCount
      );

      if (renderPoses && renderPoseCount > 0 && renderPoses[0].bPoseIsValid)
      {
          g_hmdTransform = renderPoses[0].mDeviceToAbsoluteTracking;
          InterlockedExchange(&g_hasHmdTransform, 1);
      }

      return result;
  }

  static DWORD WINAPI WorkerThread(LPVOID)
  {
      LogLine("ASI loaded; waiting for IVRCompositor...\n");

      uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
      auto compositorSlot = reinterpret_cast<void**>(base + kCompositorGlobalOffset);

      void* compositor = nullptr;

      while (!compositor)
      {
          compositor = *compositorSlot;
          Sleep(100);
      }

      void** vtable = *reinterpret_cast<void***>(compositor);
      void** waitGetPosesSlot = &vtable[2];

      g_originalWaitGetPoses =
          reinterpret_cast<WaitGetPosesFn>(*waitGetPosesSlot);

      DWORD oldProtect = 0;
      VirtualProtect(waitGetPosesSlot, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect);
      *waitGetPosesSlot = reinterpret_cast<void*>(&HookWaitGetPoses);
      VirtualProtect(waitGetPosesSlot, sizeof(void*), oldProtect, &oldProtect);

      LogLine("Hooked IVRCompositor::WaitGetPoses.\n");

      for (;;)
      {
          if (g_hasHmdTransform)
          {
              char line[512];

              std::snprintf(
                  line,
                  sizeof(line),
                  "HMD matrix:\n"
                  "%f %f %f %f\n"
                  "%f %f %f %f\n"
                  "%f %f %f %f\n\n",
                  g_hmdTransform.m[0][0], g_hmdTransform.m[0][1], g_hmdTransform.m[0][2], g_hmdTransform.m[0][3],
                  g_hmdTransform.m[1][0], g_hmdTransform.m[1][1], g_hmdTransform.m[1][2], g_hmdTransform.m[1][3],
                  g_hmdTransform.m[2][0], g_hmdTransform.m[2][1], g_hmdTransform.m[2][2], g_hmdTransform.m[2][3]
              );

              LogLine(line);
          }

          Sleep(500);
      }
  }

  BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID)
  {
      if (reason == DLL_PROCESS_ATTACH)
      {
          DisableThreadLibraryCalls(module);

          HANDLE thread = CreateThread(
              nullptr,
              0,
              WorkerThread,
              nullptr,
              0,
              nullptr
          );

          if (thread)
              CloseHandle(thread);
      }

      return TRUE;
  }