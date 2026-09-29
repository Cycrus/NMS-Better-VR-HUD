#include "better_vr_hud_math.h"

#include <cmath>
#include <cstdio>

static bool Near(float actual, float expected)
{
    return std::fabs(actual - expected) < 0.0001f;
}

static bool MatrixNear(const float* actual, const float* expected)
{
    for (int i = 0; i < 16; ++i)
    {
        if (!Near(actual[i], expected[i]))
        {
            std::printf(
                "matrix[%d] expected %.6f but got %.6f\n",
                i,
                expected[i],
                actual[i]
            );
            return false;
        }
    }

    return true;
}

static bool NeutralCameraBodyProducesDefaultHud()
{
    const float identity[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };

    float hud[16] = {};
    if (!BuildCameraRelativeHudMatrix(identity, identity, hud, 0.0f, 0.0f, -2.5f))
        return false;

    const float expected[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, -2.5f, 1.0f
    };

    return MatrixNear(hud, expected);
}

static bool CameraYawRelativeToBodyMovesHudAlongCameraNegativeZ()
{
    const float body[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };

    const float camera[16] = {
        0.0f, 0.0f, -1.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };

    float hud[16] = {};
    if (!BuildCameraRelativeHudMatrix(camera, body, hud, 0.0f, 0.0f, -2.5f))
        return false;

    const float expected[16] = {
        0.0f, -0.0f, -1.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f, 0.0f,
        -2.5f, -0.0f, -0.0f, 1.0f
    };

    return MatrixNear(hud, expected);
}

static bool LocalHudOffsetMovesAlongHudAxes()
{
    const float identity[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };

    float hud[16] = {};
    if (!BuildCameraRelativeHudMatrix(identity, identity, hud, 1.0f, 0.5f, -2.5f))
        return false;

    const float expected[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        1.0f, 0.5f, -2.5f, 1.0f
    };

    return MatrixNear(hud, expected);
}

static bool LevelHudIgnoresCameraPitchForVerticalPlacement()
{
    const float body[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };

    const float pitchedCamera[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.70710678f, -0.70710678f, 0.0f,
        0.0f, 0.70710678f, 0.70710678f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };

    float hud[16] = {};
    if (!BuildLevelCameraRelativeHudMatrix(pitchedCamera, body, hud, 0.0f, 0.0f, -2.5f))
        return false;

    const float expected[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, -2.5f, 1.0f
    };

    return MatrixNear(hud, expected);
}

static bool ExtractLocalZOffsetUsesMatrixForwardAxis()
{
    const float matrix[16] = {
        0.0f, -0.0f, -1.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f, 0.0f,
        -2.5f, 0.0f, 0.0f, 1.0f
    };

    float offsetZ = 0.0f;
    if (!ExtractLocalHudOffsetZ(matrix, &offsetZ))
        return false;

    return Near(offsetZ, -2.5f);
}

static bool HudMatrixFallbackUsesLastValidMatrix()
{
    HudMatrixState state = {};

    const float identity[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };

    float first[16] = {};
    if (!BuildCameraRelativeHudMatrixWithFallback(identity, identity, &state, first, 0.0f, 0.0f, -2.5f))
        return false;

    const float invalid[16] = {};
    float fallback[16] = {};
    if (!BuildCameraRelativeHudMatrixWithFallback(invalid, invalid, &state, fallback, 0.0f, 0.0f, -2.5f))
        return false;

    return MatrixNear(fallback, first);
}

int main()
{
    if (!NeutralCameraBodyProducesDefaultHud())
        return 1;

    if (!CameraYawRelativeToBodyMovesHudAlongCameraNegativeZ())
        return 1;

    if (!LocalHudOffsetMovesAlongHudAxes())
        return 1;

    if (!LevelHudIgnoresCameraPitchForVerticalPlacement())
        return 1;

    if (!ExtractLocalZOffsetUsesMatrixForwardAxis())
        return 1;

    if (!HudMatrixFallbackUsesLastValidMatrix())
        return 1;

    return 0;
}
