/**
 * A helper module to resolve a set of unique opcode patterns into
 * hookable code addresses.
 */

#pragma once

#include "signature_scanner.h"

/**
 * A struct containing the hookable code addresses after
 * resolving the unique opcode signature patterns.
 */
struct HookTargets
{
    void* applyMatrix;
    void* bodyCapturePoint;
    void* cameraCapturePoint;
    void* vrUpdate;
};

/**
 * The function which actively maps the opcode signatures and the constant offsets
 * to hookable code addresses. The addresses are then stored in the targets struct.
 */
bool ResolveHookTargets(
    HookTargets* targets,
    SignatureScannerLogFn log = nullptr
);
