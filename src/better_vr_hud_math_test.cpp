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
    if (!BuildCameraRelativeHudMatrix(identity, identity, hud))
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
    if (!BuildCameraRelativeHudMatrix(camera, body, hud))
        return false;

    const float expected[16] = {
        0.0f, -0.0f, -1.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f, 0.0f,
        -2.5f, -0.0f, -0.0f, 1.0f
    };

    return MatrixNear(hud, expected);
}

int main()
{
    if (!NeutralCameraBodyProducesDefaultHud())
        return 1;

    if (!CameraYawRelativeToBodyMovesHudAlongCameraNegativeZ())
        return 1;

    return 0;
}
