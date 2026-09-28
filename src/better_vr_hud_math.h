#pragma once

#include <cmath>
#include <cstring>

struct Vec3
{
    float x;
    float y;
    float z;
};

struct HudMatrixState
{
    float latest[16] = {};
    bool hasLatest = false;
};

static inline bool IsFinite(float value)
{
    return std::isfinite(value);
}

static inline Vec3 MakeVec3(float x, float y, float z)
{
    return {x, y, z};
}

static inline Vec3 Add(Vec3 a, Vec3 b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

static inline Vec3 Scale(Vec3 v, float scale)
{
    return {v.x * scale, v.y * scale, v.z * scale};
}

static inline float Dot(Vec3 a, Vec3 b)
{
    return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
}

static inline Vec3 Cross(Vec3 a, Vec3 b)
{
    return {
        (a.y * b.z) - (a.z * b.y),
        (a.z * b.x) - (a.x * b.z),
        (a.x * b.y) - (a.y * b.x)
    };
}

static inline float Length(Vec3 v)
{
    return std::sqrt(Dot(v, v));
}

static inline bool Normalize(Vec3 v, Vec3* out)
{
    float length = Length(v);
    if (!IsFinite(length) || length < 0.0001f)
        return false;

    *out = Scale(v, 1.0f / length);
    return true;
}

static inline Vec3 Row3(const float* matrix, int row)
{
    int base = row * 4;
    return {matrix[base], matrix[base + 1], matrix[base + 2]};
}

static inline bool IsFiniteMatrix(const float* matrix)
{
    for (int i = 0; i < 16; ++i)
    {
        if (!IsFinite(matrix[i]))
            return false;
    }

    return true;
}

static inline bool IsOrthonormalRotation(const float* matrix)
{
    Vec3 right = Row3(matrix, 0);
    Vec3 up = Row3(matrix, 1);
    Vec3 forward = Row3(matrix, 2);

    float rightLength = Length(right);
    float upLength = Length(up);
    float forwardLength = Length(forward);

    if (!IsFinite(rightLength) || !IsFinite(upLength) || !IsFinite(forwardLength))
        return false;

    if (std::fabs(rightLength - 1.0f) > 0.15f)
        return false;

    if (std::fabs(upLength - 1.0f) > 0.15f)
        return false;

    if (std::fabs(forwardLength - 1.0f) > 0.15f)
        return false;

    if (std::fabs(Dot(right, up)) > 0.20f)
        return false;

    if (std::fabs(Dot(right, forward)) > 0.20f)
        return false;

    if (std::fabs(Dot(up, forward)) > 0.20f)
        return false;

    return true;
}

static inline bool ValidateTransform(const float* matrix)
{
    return IsFiniteMatrix(matrix) && IsOrthonormalRotation(matrix);
}

static inline void InvertOrthonormalAffineRowMajor(const float* matrix, float* inverse)
{
    inverse[0] = matrix[0];
    inverse[1] = matrix[4];
    inverse[2] = matrix[8];
    inverse[3] = 0.0f;

    inverse[4] = matrix[1];
    inverse[5] = matrix[5];
    inverse[6] = matrix[9];
    inverse[7] = 0.0f;

    inverse[8] = matrix[2];
    inverse[9] = matrix[6];
    inverse[10] = matrix[10];
    inverse[11] = 0.0f;

    Vec3 pos = MakeVec3(matrix[12], matrix[13], matrix[14]);
    inverse[12] = -Dot(pos, Row3(inverse, 0));
    inverse[13] = -Dot(pos, Row3(inverse, 1));
    inverse[14] = -Dot(pos, Row3(inverse, 2));
    inverse[15] = 1.0f;
}

static inline void MultiplyRowMajor4x4(const float* a, const float* b, float* out)
{
    float result[16] = {};

    for (int row = 0; row < 4; ++row)
    {
        for (int col = 0; col < 4; ++col)
        {
            float value = 0.0f;
            for (int k = 0; k < 4; ++k)
                value += a[(row * 4) + k] * b[(k * 4) + col];

            result[(row * 4) + col] = value;
        }
    }

    std::memcpy(out, result, sizeof(result));
}

static inline bool BuildRelativeTransform(const float* camera, const float* body, float* relative)
{
    if (!camera || !body || !relative)
        return false;

    if (!ValidateTransform(camera) || !ValidateTransform(body))
        return false;

    float bodyInverse[16] = {};
    InvertOrthonormalAffineRowMajor(body, bodyInverse);
    MultiplyRowMajor4x4(camera, bodyInverse, relative);

    return ValidateTransform(relative);
}

static inline bool BuildHudMatrixFromBasis(
    Vec3 right,
    Vec3 up,
    Vec3 forward,
    float* hud,
    float offsetX,
    float offsetY,
    float offsetZ
)
{
    if (!hud)
        return false;

    Vec3 hudPos = Add(
        Add(Scale(right, offsetX), Scale(up, offsetY)),
        Scale(forward, offsetZ)
    );

    hud[0] = right.x;
    hud[1] = right.y;
    hud[2] = right.z;
    hud[3] = 0.0f;

    hud[4] = up.x;
    hud[5] = up.y;
    hud[6] = up.z;
    hud[7] = 0.0f;

    hud[8] = forward.x;
    hud[9] = forward.y;
    hud[10] = forward.z;
    hud[11] = 0.0f;

    hud[12] = hudPos.x;
    hud[13] = hudPos.y;
    hud[14] = hudPos.z;
    hud[15] = 1.0f;

    return ValidateTransform(hud);
}

static inline bool BuildCameraRelativeHudMatrix(
    const float* camera,
    const float* body,
    float* hud,
    float offsetX,
    float offsetY,
    float offsetZ
)
{
    float relative[16] = {};
    if (!BuildRelativeTransform(camera, body, relative))
        return false;

    Vec3 forward = {};
    if (!Normalize(Row3(relative, 2), &forward))
        return false;

    Vec3 upRef = Row3(relative, 1);
    Vec3 right = {};
    if (!Normalize(Cross(upRef, forward), &right))
        return false;

    Vec3 up = Cross(forward, right);
    if (!IsFinite(up.x) || !IsFinite(up.y) || !IsFinite(up.z))
        return false;

    return BuildHudMatrixFromBasis(right, up, forward, hud, offsetX, offsetY, offsetZ);
}

static inline bool BuildLevelCameraRelativeHudMatrix(
    const float* camera,
    const float* body,
    float* hud,
    float offsetX,
    float offsetY,
    float offsetZ
)
{
    float relative[16] = {};
    if (!BuildRelativeTransform(camera, body, relative))
        return false;

    Vec3 levelForward = Row3(relative, 2);
    levelForward.y = 0.0f;

    Vec3 forward = {};
    if (!Normalize(levelForward, &forward))
        return false;

    Vec3 upRef = MakeVec3(0.0f, 1.0f, 0.0f);
    Vec3 right = {};
    if (!Normalize(Cross(upRef, forward), &right))
        return false;

    Vec3 up = Cross(forward, right);
    if (!IsFinite(up.x) || !IsFinite(up.y) || !IsFinite(up.z))
        return false;

    return BuildHudMatrixFromBasis(right, up, forward, hud, offsetX, offsetY, offsetZ);
}

static inline bool BuildCameraRelativeHudMatrixWithFallback(
    const float* camera,
    const float* body,
    HudMatrixState* state,
    float* hud,
    float offsetX,
    float offsetY,
    float offsetZ
)
{
    if (!state || !hud)
        return false;

    if (BuildLevelCameraRelativeHudMatrix(camera, body, hud, offsetX, offsetY, offsetZ))
    {
        std::memcpy(state->latest, hud, sizeof(state->latest));
        state->hasLatest = true;
        return true;
    }

    if (!state->hasLatest)
        return false;

    std::memcpy(hud, state->latest, sizeof(state->latest));
    return true;
}
