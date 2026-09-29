#pragma once

#include "signature_scanner.h"

struct HookTargets
{
    void* applyMatrix;
    void* bodyCapturePoint;
    void* cameraCapturePoint;
    void* vrUpdate;
};

bool ResolveHookTargets(
    HookTargets* targets,
    SignatureScannerLogFn log = nullptr
);
