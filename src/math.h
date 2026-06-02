#pragma once
/*
 * math.h — UE5 Double-Precision Math Types
 *
 * CRITICAL: UE5 Large World Coordinates use doubles for all world-space
 * types. FVector=24 bytes, FMatrix=128 bytes, FTransform=96 bytes.
 */

#include <cmath>
#include <cstdint>
#include <cstring>

#ifndef NW_PI
#define NW_PI 3.14159265358979323846
#endif

// ─── FVector2D (screen space, float) ──────────────────────────────────
struct FVector2D {
    float X = 0.f, Y = 0.f;
    FVector2D() = default;
    FVector2D(float x, float y) : X(x), Y(y) {}
};

// ─── FVector (UE5 world space, double) ────────────────────────────────
struct FVector {
    double X = 0.0, Y = 0.0, Z = 0.0;
    FVector() = default;
    FVector(double x, double y, double z) : X(x), Y(y), Z(z) {}

    FVector operator-(const FVector& o) const { return { X - o.X, Y - o.Y, Z - o.Z }; }
    FVector operator+(const FVector& o) const { return { X + o.X, Y + o.Y, Z + o.Z }; }
    FVector operator*(double s)         const { return { X * s, Y * s, Z * s }; }

    double Dot(const FVector& o)  const { return X * o.X + Y * o.Y + Z * o.Z; }
    double Length()               const { return sqrt(X * X + Y * Y + Z * Z); }
    double Length2D()             const { return sqrt(X * X + Y * Y); }
    double DistTo(const FVector& o) const { return (*this - o).Length(); }

    bool IsZero() const { return X == 0.0 && Y == 0.0 && Z == 0.0; }
    bool IsValid() const {
        return !isnan(X) && !isnan(Y) && !isnan(Z) &&
               !isinf(X) && !isinf(Y) && !isinf(Z) &&
               fabs(X) < 1e10 && fabs(Y) < 1e10 && fabs(Z) < 1e10;
    }
};

// ─── FRotator (UE5 world space, double, degrees) ─────────────────────
struct FRotator {
    double Pitch = 0.0, Yaw = 0.0, Roll = 0.0;
    FRotator() = default;
    FRotator(double p, double y, double r) : Pitch(p), Yaw(y), Roll(r) {}
};

// ─── FMatrix (4x4 doubles = 128 bytes) ───────────────────────────────
struct FMatrix {
    double M[4][4] = {};
};

// ─── FTransform (UE5 layout: 96 bytes / 0x60) ────────────────────────
//  Offset  Field               Size
//  0x00    Rotation (FQuat4d)  32 bytes (X,Y,Z,W doubles)
//  0x20    Translation         24 bytes (3 doubles)
//  0x38    _pad0               8  bytes
//  0x40    Scale3D             24 bytes (3 doubles)
//  0x58    _pad1               8  bytes
struct FTransform {
    // Quaternion rotation (X, Y, Z, W)
    double RotX = 0.0, RotY = 0.0, RotZ = 0.0, RotW = 1.0;
    // Translation
    double TransX = 0.0, TransY = 0.0, TransZ = 0.0;
    double _pad0 = 0.0;
    // Scale
    double ScaleX = 1.0, ScaleY = 1.0, ScaleZ = 1.0;
    double _pad1 = 0.0;

    // Convert quaternion+scale to 4x4 matrix
    FMatrix ToMatrixWithScale() const {
        FMatrix m = {};
        double x2 = RotX + RotX, y2 = RotY + RotY, z2 = RotZ + RotZ;
        double xx = RotX * x2,   xy = RotX * y2,   xz = RotX * z2;
        double yy = RotY * y2,   yz = RotY * z2,   zz = RotZ * z2;
        double wx = RotW * x2,   wy = RotW * y2,   wz = RotW * z2;

        m.M[0][0] = (1.0 - (yy + zz)) * ScaleX;
        m.M[0][1] = (xy + wz) * ScaleX;
        m.M[0][2] = (xz - wy) * ScaleX;
        m.M[0][3] = 0.0;

        m.M[1][0] = (xy - wz) * ScaleY;
        m.M[1][1] = (1.0 - (xx + zz)) * ScaleY;
        m.M[1][2] = (yz + wx) * ScaleY;
        m.M[1][3] = 0.0;

        m.M[2][0] = (xz + wy) * ScaleZ;
        m.M[2][1] = (yz - wx) * ScaleZ;
        m.M[2][2] = (1.0 - (xx + yy)) * ScaleZ;
        m.M[2][3] = 0.0;

        m.M[3][0] = TransX;
        m.M[3][1] = TransY;
        m.M[3][2] = TransZ;
        m.M[3][3] = 1.0;
        return m;
    }
};
static_assert(sizeof(FTransform) == 96, "FTransform must be 96 bytes (0x60)");

// ─── Matrix multiply ─────────────────────────────────────────────────
inline FMatrix MatrixMultiply(const FMatrix& a, const FMatrix& b) {
    FMatrix r = {};
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            for (int k = 0; k < 4; k++)
                r.M[i][j] += a.M[i][k] * b.M[k][j];
    return r;
}

// ─── WorldToScreen ────────────────────────────────────────────────────
// Manual projection using camera rotation matrix.
// All math in double precision, output in float for screen coords.
inline bool WorldToScreen(
    const FVector& worldPos,
    const FVector& cameraPos,
    const FRotator& cameraRot,
    float cameraFOV,
    float screenW, float screenH,
    FVector2D& outScreen)
{
    // Convert degrees to radians
    constexpr double DEG2RAD = NW_PI / 180.0;
    double pitch = cameraRot.Pitch * DEG2RAD;
    double yaw   = cameraRot.Yaw   * DEG2RAD;
    double roll  = cameraRot.Roll  * DEG2RAD;

    double sp = sin(pitch), cp = cos(pitch);
    double sy = sin(yaw),   cy = cos(yaw);
    double sr = sin(roll),  cr = cos(roll);

    // Forward axis
    FVector ax = { cp * cy, cp * sy, sp };
    // Right axis
    FVector ay = { sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp };
    // Up axis
    FVector az = { -(cr * sp * cy + sr * sy), cy * sr - cr * sp * sy, cr * cp };

    // Delta from camera to target
    FVector delta = worldPos - cameraPos;

    // Dot products (project onto camera axes)
    double dotForward = delta.Dot(ax);
    double dotRight   = delta.Dot(ay);
    double dotUp      = delta.Dot(az);

    // Behind camera check
    if (dotForward < 1.0)
        return false;

    // FOV tangent
    double fovRad = (double)cameraFOV * DEG2RAD;
    double tanHalfFov = tan(fovRad * 0.5);

    // Screen projection
    float halfW = screenW * 0.5f;
    float halfH = screenH * 0.5f;

    outScreen.X = halfW + (float)(dotRight / (dotForward * tanHalfFov)) * halfW;
    outScreen.Y = halfH - (float)(dotUp    / (dotForward * tanHalfFov)) * halfW;  // halfW for both (aspect ratio)

    return true;
}
