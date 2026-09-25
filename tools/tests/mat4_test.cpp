// Checks the affine helpers used for head-locking, including that column 3 (engine-specific values) is ignored.

#include <cmath>
#include <cstdio>
#include <cstring>

#include "common/mat4.h"

namespace
{
    int g_failures = 0;

    void Expect(bool condition, const char* what)
    {
        if (!condition)
        {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    bool Near(const float* a, const float* b, float tolerance)
    {
        for (int row = 0; row < 4; ++row)
        {
            for (int col = 0; col < 3; ++col)
            {
                if (std::fabs(a[row * 4 + col] - b[row * 4 + col]) > tolerance)
                    return false;
            }
        }
        return true;
    }

    // Rotation about an arbitrary axis, scaled, translated; column 3 filled with junk like the engine's.
    void MakeTransform(float angle, float ax, float ay, float az, float scale, float tx, float ty, float tz, float* out)
    {
        float len = std::sqrt(ax * ax + ay * ay + az * az);
        ax /= len;
        ay /= len;
        az /= len;
        float c = std::cos(angle);
        float s = std::sin(angle);
        float t = 1 - c;
        float m[16] = {
            (t * ax * ax + c) * scale, (t * ax * ay + s * az) * scale, (t * ax * az - s * ay) * scale, 7.0f,
            (t * ax * ay - s * az) * scale, (t * ay * ay + c) * scale, (t * ay * az + s * ax) * scale, -3.0f,
            (t * ax * az + s * ay) * scale, (t * ay * az - s * ax) * scale, (t * az * az + c) * scale, 1024.0f,
            tx, ty, tz, 2.5f,
        };
        std::memcpy(out, m, sizeof(m));
    }
}

int main()
{
    using namespace bvh::mat4;

    float identity[16];
    Identity(identity);

    float offset[16];
    float camera[16];
    float anchor[16];
    MakeTransform(0.0f, 0, 1, 0, 1.2f, 0.0f, 0.0f, -2.5f, offset);
    MakeTransform(0.7f, 0.2f, 1.0f, 0.1f, 1.0f, -474.05f, -274.78f, 354.53f, camera);
    MakeTransform(-1.9f, 0.1f, 0.9f, -0.3f, 1.0f, -474.1f, -274.7f, 354.6f, anchor);

    float inverse[16];
    Expect(InverseAffine(anchor, inverse), "anchor invertible");

    float product[16];
    MultiplyAffine(anchor, inverse, product);
    Expect(Near(product, identity, 1e-4f), "anchor * inverse(anchor) == identity despite junk column 3");

    // Head-lock formula: local = offset * camera * inverse(anchor); then local * anchor must equal offset * camera.
    float desired[16];
    float local[16];
    float rebuilt[16];
    MultiplyAffine(offset, camera, desired);
    MultiplyAffine(desired, inverse, local);
    MultiplyAffine(local, anchor, rebuilt);
    Expect(Near(rebuilt, desired, 1e-3f), "local * anchor == offset * camera");
    Expect(std::fabs(TranslationLength(local) - 2.5f * 1.0f) < 0.2f, "locked HUD stays ~2.5 m from the anchor");

    float scaled[16];
    MakeTransform(0.3f, 1, 0, 0, 2.0f, 1, 2, 3, scaled);
    Expect(!IsPureTranslation(scaled, 1e-3f), "rotated matrix is not a pure translation");
    float shifted[16];
    MakeTransform(0.0f, 1, 0, 0, 1.0f, -10240.0f, 6144.0f, 142336.0f, shifted);
    Expect(IsPureTranslation(shifted, 1e-3f), "origin node is a pure translation");

    std::printf("%s (%d failure(s))\n", g_failures ? "FAILED" : "passed", g_failures);
    return g_failures ? 1 : 0;
}
