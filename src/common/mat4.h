#pragma once

#include <cmath>
#include <cstring>

// Row-major 4x4 matrices in the engine's convention: row vectors (v' = v * M),
// rows 0-2 are the basis axes and row 3 is the translation. A * B applies A first, then B.
//
// The engine treats them as affine 3x4: column 3 (m[3], m[7], m[11], m[15]) carries unrelated
// values (e.g. the HUD's scale and distance) and is ignored by its multiplies, so ours ignore it too.
namespace bvh::mat4
{
    inline void Identity(float* out)
    {
        static const float kIdentity[16] = {
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1,
        };
        std::memcpy(out, kIdentity, sizeof(kIdentity));
    }

    // Affine product (column 3 of the inputs ignored, output column 3 = 0,0,0,1).
    inline void MultiplyAffine(const float* a, const float* b, float* out)
    {
        float result[16];
        for (int row = 0; row < 4; ++row)
        {
            for (int col = 0; col < 3; ++col)
            {
                result[row * 4 + col] =
                    a[row * 4 + 0] * b[0 * 4 + col] +
                    a[row * 4 + 1] * b[1 * 4 + col] +
                    a[row * 4 + 2] * b[2 * 4 + col] +
                    (row == 3 ? b[3 * 4 + col] : 0.0f);
            }
        }
        result[3] = result[7] = result[11] = 0.0f;
        result[15] = 1.0f;
        std::memcpy(out, result, sizeof(result));
    }

    // Affine inverse (column 3 of the input ignored). Handles scale and shear in the 3x3 part.
    inline bool InverseAffine(const float* m, float* out)
    {
        const float a = m[0], b = m[1], c = m[2];
        const float d = m[4], e = m[5], f = m[6];
        const float g = m[8], h = m[9], i = m[10];

        const float c00 = e * i - f * h;
        const float c01 = f * g - d * i;
        const float c02 = d * h - e * g;
        const float det = a * c00 + b * c01 + c * c02;
        if (!(std::fabs(det) > 1e-12f))
            return false;

        const float s = 1.0f / det;
        float r[16] = {
            c00 * s, (c * h - b * i) * s, (b * f - c * e) * s, 0,
            c01 * s, (a * i - c * g) * s, (c * d - a * f) * s, 0,
            c02 * s, (b * g - a * h) * s, (a * e - b * d) * s, 0,
            0, 0, 0, 1,
        };

        const float tx = m[12], ty = m[13], tz = m[14];
        r[12] = -(tx * r[0] + ty * r[4] + tz * r[8]);
        r[13] = -(tx * r[1] + ty * r[5] + tz * r[9]);
        r[14] = -(tx * r[2] + ty * r[6] + tz * r[10]);

        std::memcpy(out, r, sizeof(r));
        return true;
    }

    // OpenVR HmdMatrix34_t (column-vector convention, m[row][3] = translation) to engine convention.
    inline void FromOpenVr34(const float m[3][4], float* out)
    {
        float r[16] = {
            m[0][0], m[1][0], m[2][0], 0,
            m[0][1], m[1][1], m[2][1], 0,
            m[0][2], m[1][2], m[2][2], 0,
            m[0][3], m[1][3], m[2][3], 1,
        };
        std::memcpy(out, r, sizeof(r));
    }

    inline float TranslationLength(const float* m)
    {
        return std::sqrt(m[12] * m[12] + m[13] * m[13] + m[14] * m[14]);
    }

    inline float TranslationDistance(const float* a, const float* b)
    {
        float dx = a[12] - b[12];
        float dy = a[13] - b[13];
        float dz = a[14] - b[14];
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    // True when the 3x3 part is the identity (within tolerance).
    inline bool IsPureTranslation(const float* m, float tolerance)
    {
        for (int row = 0; row < 3; ++row)
        {
            for (int col = 0; col < 3; ++col)
            {
                float expected = row == col ? 1.0f : 0.0f;
                if (std::fabs(m[row * 4 + col] - expected) > tolerance)
                    return false;
            }
        }
        return true;
    }

    inline bool IsFinite(const float* m)
    {
        for (int i = 0; i < 16; ++i)
        {
            if (!std::isfinite(m[i]))
                return false;
        }
        return true;
    }
}
