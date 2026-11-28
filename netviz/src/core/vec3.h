// vec3.h - tiny 3D vector helpers (no raylib dependency, so the core stays testable headless).
#pragma once
#include <cmath>

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

inline Vec3  V3(float x, float y, float z) { Vec3 v; v.x = x; v.y = y; v.z = z; return v; }
inline Vec3  V3Add(Vec3 a, Vec3 b)   { return V3(a.x + b.x, a.y + b.y, a.z + b.z); }
inline Vec3  V3Sub(Vec3 a, Vec3 b)   { return V3(a.x - b.x, a.y - b.y, a.z - b.z); }
inline Vec3  V3Scale(Vec3 a, float s){ return V3(a.x * s, a.y * s, a.z * s); }
inline float V3Dot(Vec3 a, Vec3 b)   { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3  V3Cross(Vec3 a, Vec3 b) {
    return V3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
inline float V3Length(Vec3 a) { return std::sqrt(V3Dot(a, a)); }
inline float V3Dist(Vec3 a, Vec3 b) { return V3Length(V3Sub(a, b)); }
inline Vec3  V3Normalize(Vec3 a) {
    float len = V3Length(a);
    return len > 1e-12f ? V3Scale(a, 1.0f / len) : V3(0, 0, 0);
}
