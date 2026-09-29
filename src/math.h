#pragma once

enum HudType {
    NONE,
    LEVEL,
    FULL
};

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

bool IsFinite(float value);
Vec3 MakeVec3(float x, float y, float z);
Vec3 Add(Vec3 a, Vec3 b);
Vec3 Scale(Vec3 v, float scale);
float Dot(Vec3 a, Vec3 b);
Vec3 Cross(Vec3 a, Vec3 b);
float Length(Vec3 v);
bool Normalize(Vec3 v, Vec3* out);
Vec3 Row3(const float* matrix, int row);
bool IsFiniteMatrix(const float* matrix);
bool IsOrthonormalRotation(const float* matrix);
bool ValidateTransform(const float* matrix);
void InvertOrthonormalAffineRowMajor(const float* matrix, float* inverse);
void MultiplyRowMajor4x4(const float* a, const float* b, float* out);
bool BuildRelativeTransform(const float* camera, const float* body, float* relative);
bool BuildHudMatrixFromBasis(
    Vec3 right,
    Vec3 up,
    Vec3 forward,
    float* hud,
    float offsetX,
    float offsetY,
    float offsetZ
);
bool BuildCameraRelativeHudMatrix(
    const float* camera,
    const float* body,
    float* hud,
    float offsetX,
    float offsetY,
    float offsetZ
);
bool BuildLevelCameraRelativeHudMatrix(
    const float* camera,
    const float* body,
    float* hud,
    float offsetX,
    float offsetY,
    float offsetZ
);
bool ExtractLocalHudOffsetZ(const float* matrix, float* offsetZ);
bool BuildCameraRelativeHudMatrixWithFallback(
    const float* camera,
    const float* body,
    HudMatrixState* state,
    float* hud,
    float offsetX,
    float offsetY,
    float offsetZ,
    HudType hud_type
);
